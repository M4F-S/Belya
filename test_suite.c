#include "c_harness.h"
#include "telegram_adapter.h"
#include <assert.h>
#include <unistd.h>

void test_dyn_string(void) {
    printf("[Test] DynString Operations...\n");
    DynString ds = dyn_str_new();
    assert(ds.len == 0);
    assert(ds.cap >= 512);

    dyn_str_append(&ds, "Hello");
    assert(ds.len == 5);
    assert(strcmp(ds.data, "Hello") == 0);

    dyn_str_appendf(&ds, " %s %d", "World", 2026);
    assert(strcmp(ds.data, "Hello World 2026") == 0);

    dyn_str_clear(&ds);
    assert(ds.len == 0);
    assert(strcmp(ds.data, "") == 0);

    dyn_str_append_escaped(&ds, "Line 1\nLine \"2\"\tTab");
    assert(strstr(ds.data, "\\n") != NULL);
    assert(strstr(ds.data, "\\\"") != NULL);
    assert(strstr(ds.data, "\\t") != NULL);

    dyn_str_free(&ds);
    assert(ds.data == NULL);
    printf("  -> DynString PASSED\n");
}

void test_minijson(void) {
    printf("[Test] MiniJSON Parser & Serializer...\n");
    const char *json_src = 
        "{"
        "  \"name\": \"CHarness-Agent\",\n"
        "  \"version\": 2.0,\n"
        "  \"active\": true,\n"
        "  \"escape_test\": \"Tab:\\t Slash:\\/ Unicode:\\u0041\\u00e9\",\n"
        "  \"tags\": [\"ai\", \"c99\", \"agent\"],\n"
        "  \"meta\": { \"author\": \"Hermes\", \"score\": 100 }\n"
        "}";

    JsonValue *root = json_parse(json_src);
    assert(root != NULL);
    assert(root->type == JSON_OBJECT);

    assert(strcmp(json_obj_get_str(root, "name"), "CHarness-Agent") == 0);
    assert(json_obj_get_num(root, "version", 0) == 2.0);
    assert(json_obj_get_bool(root, "active", false) == true);

    const char *esc = json_obj_get_str(root, "escape_test");
    assert(esc != NULL);
    assert(strstr(esc, "\t") != NULL);
    assert(strstr(esc, "/") != NULL);
    assert(strstr(esc, "A") != NULL);

    JsonValue *tags = json_obj_get(root, "tags");
    assert(tags != NULL && tags->type == JSON_ARRAY);
    assert(tags->u.array.count == 3);
    assert(strcmp(tags->u.array.items[0]->u.string, "ai") == 0);

    JsonValue *meta = json_obj_get(root, "meta");
    assert(meta != NULL && meta->type == JSON_OBJECT);
    assert(strcmp(json_obj_get_str(meta, "author"), "Hermes") == 0);
    assert(json_obj_get_num(meta, "score", 0) == 100);

    char *serialized = json_serialize(root);
    assert(serialized != NULL);
    assert(strstr(serialized, "\"name\":\"CHarness-Agent\"") != NULL);
    free(serialized);

    json_free(root);
    printf("  -> MiniJSON PASSED\n");
}

void test_agent_memory_and_rules(void) {
    printf("[Test] Agent Memory (FTS5) & Rules Auto-Discovery...\n");

    FILE *rf = fopen(".agentrules", "w");
    if (rf) {
        fprintf(rf, "Always write self-contained C99 code without external dependencies.\n");
        fclose(rf);
    }

    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, "test_agent_memory.sqlite", "Test System Prompt");
    assert(agent != NULL);
    assert(agent->msg_count == 1);

    assert(strstr(agent->messages[0].content, "Always write self-contained C99 code") != NULL);

    c_agent_persist_memory(agent, "POSIX Signals", "Use kill(pid, SIGKILL) to forcibly stop hung processes.");
    c_agent_persist_memory(agent, "SQLite FTS5", "FTS5 allows fast BM25 full-text indexing.");

    char *mem_res1 = c_agent_search_memory(agent, "Signals");
    assert(strstr(mem_res1, "POSIX Signals") != NULL);
    free(mem_res1);

    char *mem_res2 = c_agent_search_memory(agent, "BM25");
    assert(strstr(mem_res2, "SQLite FTS5") != NULL);
    free(mem_res2);

    for (int i = 0; i < 20; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "User turn message %d", i);
        c_agent_add_message(agent, "user", buf);
    }
    assert(agent->msg_count == 21);

    c_agent_compact_history(agent, 5);
    assert(agent->msg_count == 6);
    assert(strcmp(agent->messages[0].role, "system") == 0);
    assert(strcmp(agent->messages[5].content, "User turn message 19") == 0);

    c_agent_clear_history(agent);
    assert(agent->msg_count == 1);
    assert(strcmp(agent->messages[0].role, "system") == 0);

    c_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_agent_memory.sqlite");
    unlink(".agentrules");
    printf("  -> Agent Memory & Rules PASSED\n");
}

void test_harness_tools_and_patches(void) {
    printf("[Test] Harness Tool Suite (12 Tools) & Patch Engine...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    gw->streaming = false;
    CAgent *agent = c_agent_init(gw, "test_harness_mem.sqlite", "Test");
    CHarness *h = c_harness_init(agent);

    assert(h->tool_count >= 12);

    // 1. Write file
    JsonValue *w_args = json_create_object();
    json_obj_add(w_args, "path", json_create_string("test_sample.txt"));
    json_obj_add(w_args, "content", json_create_string("Line 1: Alpha\nLine 2: Beta\nLine 3: Gamma\nLine 4: Delta\n"));
    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "write_file") == 0) {
            char *obs = h->tools[t].callback(agent, w_args);
            assert(strstr(obs, "successfully written") != NULL);
            free(obs);
            break;
        }
    }
    json_free(w_args);

    // 2. Read file with line slicing (lines 2..3)
    JsonValue *r_args = json_create_object();
    json_obj_add(r_args, "path", json_create_string("test_sample.txt"));
    json_obj_add(r_args, "offset", json_create_number(2));
    json_obj_add(r_args, "limit", json_create_number(2));
    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "read_file") == 0) {
            char *obs = h->tools[t].callback(agent, r_args);
            assert(strstr(obs, "Beta") != NULL);
            assert(strstr(obs, "Gamma") != NULL);
            assert(strstr(obs, "Alpha") == NULL);
            free(obs);
            break;
        }
    }
    json_free(r_args);

    // 3. Edit file
    JsonValue *e_args = json_create_object();
    json_obj_add(e_args, "path", json_create_string("test_sample.txt"));
    json_obj_add(e_args, "old_text", json_create_string("Beta"));
    json_obj_add(e_args, "new_text", json_create_string("Bravo"));
    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "edit_file") == 0) {
            char *obs = h->tools[t].callback(agent, e_args);
            assert(strstr(obs, "successfully edited") != NULL);
            free(obs);
            break;
        }
    }
    json_free(e_args);

    // 4. Apply Patch (SEARCH / REPLACE format)
    const char *patch_content = 
        "<<<<<<< SEARCH\n"
        "Line 3: Gamma\n"
        "=======\n"
        "Line 3: Charlie\n"
        ">>>>>>> REPLACE";
    JsonValue *p_args = json_create_object();
    json_obj_add(p_args, "path", json_create_string("test_sample.txt"));
    json_obj_add(p_args, "patch", json_create_string(patch_content));
    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "apply_patch") == 0) {
            char *obs = h->tools[t].callback(agent, p_args);
            assert(strstr(obs, "successfully applied") != NULL);
            free(obs);
            break;
        }
    }
    json_free(p_args);

    // 5. Search files
    JsonValue *s_args = json_create_object();
    json_obj_add(s_args, "pattern", json_create_string("Charlie"));
    json_obj_add(s_args, "path", json_create_string("."));
    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "search_files") == 0) {
            char *obs = h->tools[t].callback(agent, s_args);
            assert(strstr(obs, "test_sample.txt") != NULL);
            assert(strstr(obs, "Charlie") != NULL);
            free(obs);
            break;
        }
    }
    json_free(s_args);

    // 6. Git status tool
    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "git_status") == 0) {
            char *obs = h->tools[t].callback(agent, NULL);
            assert(obs != NULL);
            free(obs);
            break;
        }
    }

    unlink("test_sample.txt");
    c_harness_free(h);
    model_gateway_free(gw);
    unlink("test_harness_mem.sqlite");
    printf("  -> Harness Tools & Patch Engine PASSED\n");
}

void test_telegram_adapter(void) {
    printf("[Test] Telegram Bot Adapter Security & Setup...\n");
    TelegramBot *bot = telegram_bot_init("123456:FAKE_TOKEN_FOR_UNIT_TEST", "999888777,111222333");
    assert(bot != NULL);
    assert(strcmp(bot->bot_token, "123456:FAKE_TOKEN_FOR_UNIT_TEST") == 0);

    // Test stop
    telegram_bot_stop(bot);
    assert(bot->running == false);

    telegram_bot_free(bot);
    printf("  -> Telegram Adapter PASSED\n");
}

int main(void) {
    printf("\n================ Running CHarness & CAgent Test Suite ================\n");
    test_dyn_string();
    test_minijson();
    test_agent_memory_and_rules();
    test_harness_tools_and_patches();
    test_telegram_adapter();
    printf("================ All Tests Passed Successfully (100%%) ================\n\n");
    return 0;
}
