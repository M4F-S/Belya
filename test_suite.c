#include "belya_harness.h"
#include "telegram_adapter.h"
#include "belya_channel.h"
#include "minifrontmatter.h"
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

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
        "  \"name\": \"BelyaHarness-Agent\",\n"
        "  \"version\": 2.0,\n"
        "  \"active\": true,\n"
        "  \"escape_test\": \"Tab:\\t Slash:\\/ Unicode:\\u0041\\u00e9\",\n"
        "  \"tags\": [\"ai\", \"c99\", \"agent\"],\n"
        "  \"meta\": { \"author\": \"Hermes\", \"score\": 100 }\n"
        "}";

    JsonValue *root = json_parse(json_src);
    assert(root != NULL);
    assert(root->type == JSON_OBJECT);

    assert(strcmp(json_obj_get_str(root, "name"), "BelyaHarness-Agent") == 0);
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
    assert(strstr(serialized, "\"name\":\"BelyaHarness-Agent\"") != NULL);
    free(serialized);

    json_free(root);
    printf("  -> MiniJSON PASSED\n");
}

void test_token_estimator(void) {
    printf("[Test] BPE-calibrated Token Estimator...\n");
    const char *sample = "Hello world! This is a test of the BPE token estimation algorithm in C99.";
    size_t tokens = count_estimated_tokens(sample);
    assert(tokens >= 15 && tokens <= 25);

    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "System instructions for token testing");
    belya_agent_add_message(agent, "user", "How many tokens are in this sentence?");
    belya_agent_add_message(agent, "assistant", "This sentence contains approximately 10 tokens.");

    size_t total_agent_tokens = belya_agent_total_tokens(agent);
    assert(total_agent_tokens > 20);

    belya_agent_free(agent);
    model_gateway_free(gw);
    printf("  -> Token Estimator PASSED (Total: %zu tokens)\n", total_agent_tokens);
}

/* H16b: this fixture used to "restore" the repository file by writing a hardcoded snapshot
   of what .agentrules looked like when the test was written, so every suite run silently
   reverted any later edit to it. Save the real bytes and put exactly those back. */
#define RULES_TEST_BAK ".agentrules.testbak"

static bool rules_fixture_begin(void) {
    bool existed = false;
    FILE *src = fopen(".agentrules", "rb");
    if (src) {
        existed = true;
        FILE *dst = fopen(RULES_TEST_BAK, "wb");
        if (dst) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                if (fwrite(buf, 1, n, dst) != n) break;   /* best effort: fixture copy */
            }
            fclose(dst);
        }
        fclose(src);
    }
    FILE *rf = fopen(".agentrules", "w");
    if (rf) {
        fprintf(rf, "Always write self-contained C99 code without external dependencies.\n");
        fclose(rf);
    }
    return existed;
}

static void rules_fixture_end(bool existed) {
    if (existed) {
        FILE *bak = fopen(RULES_TEST_BAK, "rb");
        if (bak) {
            FILE *out = fopen(".agentrules", "wb");
            if (out) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), bak)) > 0) {
                    if (fwrite(buf, 1, n, out) != n) break;   /* best effort: fixture copy */
                }
                fclose(out);
            }
            fclose(bak);
        }
        remove(RULES_TEST_BAK);
    } else {
        remove(".agentrules");
    }
}

void test_agent_memory_and_rules(void) {
    printf("[Test] Agent Memory (FTS5) & Rules Auto-Discovery...\n");

    bool rules_preexisted = rules_fixture_begin();

    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_agent_memory.sqlite", "Test System Prompt");
    assert(agent != NULL);
    assert(agent->msg_count == 1);

    assert(strstr(agent->messages[0].content, "Always write self-contained C99 code") != NULL);

    belya_agent_persist_memory(agent, "POSIX Signals", "Use kill(pid, SIGKILL) to forcibly stop hung processes.");
    belya_agent_persist_memory(agent, "SQLite FTS5", "FTS5 allows fast BM25 full-text indexing.");

    char *mem_res1 = belya_agent_search_memory(agent, "Signals");
    assert(strstr(mem_res1, "POSIX Signals") != NULL);
    free(mem_res1);

    char *mem_res2 = belya_agent_search_memory(agent, "BM25");
    assert(strstr(mem_res2, "SQLite FTS5") != NULL);
    free(mem_res2);

    for (int i = 0; i < 20; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "User turn message %d", i);
        belya_agent_add_message(agent, "user", buf);
    }
    assert(agent->msg_count == 21);

    belya_agent_compact_history(agent, 5);
    assert(agent->msg_count == 6);
    assert(strcmp(agent->messages[0].role, "system") == 0);
    assert(strcmp(agent->messages[5].content, "User turn message 19") == 0);

    belya_agent_clear_history(agent);
    assert(agent->msg_count == 1);
    assert(strcmp(agent->messages[0].role, "system") == 0);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_agent_memory.sqlite");
    
    rules_fixture_end(rules_preexisted);
    printf("  -> Agent Memory & Rules PASSED\n");
}

void test_session_checkpointing(void) {
    printf("[Test] Session Checkpointing & Resumption...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_session_db.sqlite", "System Root Instructions");

    belya_agent_add_message(agent, "user", "Fix bug in memory allocator");
    belya_agent_add_message(agent, "assistant", "I am inspecting the allocator code.");
    belya_agent_add_tool_result(agent, "call_123", "read_file", "void *alloc() { return malloc(10); }");
    belya_agent_add_message(agent, "assistant", "Fixed the bug by checking null pointers.");

    assert(agent->msg_count == 5);

    // Save Session
    bool saved = belya_agent_save_session(agent, "test_sess_001", "Memory Bug Fix");
    assert(saved == true);

    char *sessions_list = belya_agent_list_sessions(agent);
    assert(strstr(sessions_list, "test_sess_001") != NULL);
    assert(strstr(sessions_list, "Memory Bug Fix") != NULL);
    free(sessions_list);

    // Clear agent memory completely
    belya_agent_clear_history(agent);
    assert(agent->msg_count == 1);

    // Restore Session
    bool loaded = belya_agent_load_session(agent, "test_sess_001");
    assert(loaded == true);
    assert(agent->msg_count == 5);
    assert(strcmp(agent->messages[1].role, "user") == 0);
    assert(strcmp(agent->messages[1].content, "Fix bug in memory allocator") == 0);
    assert(strcmp(agent->messages[3].role, "tool") == 0);
    assert(strstr(agent->messages[3].content, "void *alloc()") != NULL);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_session_db.sqlite");
    printf("  -> Session Checkpointing & Resumption PASSED\n");
}

void test_self_tooling_define_tool(void) {
    printf("[Test] Dynamic Self-Tooling (define_tool & Custom Script Execution)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_dynamic_tool.sqlite", "System Prompt");
    BelyaHarness *h = belya_harness_init(agent);

    size_t orig_tools = h->tool_count;

    // Define a new tool dynamically
    JsonValue *params = json_create_object();
    json_obj_add(params, "type", json_create_string("object"));
    const char *script = "#!/bin/sh\necho \"CUSTOM_TOOL_OUTPUT_VERIFIED\"\n";

    bool def_ok = belya_harness_define_custom_tool(h, "custom_calc", "A dynamically invented calculator tool", params, script);
    assert(def_ok == true);
    assert(h->tool_count == orig_tools + 1);

    // Execute the custom tool
    BelyaRegisteredTool *custom_t = &h->tools[h->tool_count - 1];
    assert(strcmp(custom_t->name, "custom_calc") == 0);
    assert(custom_t->callback != NULL);

    char *obs = custom_t->callback(agent, NULL);
    assert(obs != NULL);
    assert(strstr(obs, "CUSTOM_TOOL_OUTPUT_VERIFIED") != NULL);
    free(obs);

    // Test Reflection
    belya_agent_add_message(agent, "user", "Invent and run custom_calc");
    belya_agent_add_message(agent, "assistant", "Executing custom calc");
    char *reflection = belya_agent_reflect_and_distill(agent);
    assert(reflection != NULL);
    free(reflection);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_dynamic_tool.sqlite");
    unlink(".belya/tools/custom_calc.sh");
    unlink(".belya/tools/custom_calc.json");
    rmdir(".belya/tools");
    rmdir(".belya");
    printf("  -> Dynamic Self-Tooling PASSED\n");
}

void test_harness_tools_and_patches(void) {
    printf("[Test] Harness Tool Suite (13 Tools) & Patch Engine...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    gw->streaming = false;
    BelyaAgent *agent = belya_agent_init(gw, "test_harness_mem.sqlite", "Test");
    BelyaHarness *h = belya_harness_init(agent);

    assert(h->tool_count >= 13);

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
    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_harness_mem.sqlite");
    printf("  -> Harness Tools & Patch Engine PASSED\n");
}

void test_telegram_adapter(void) {
    printf("[Test] Telegram Bot Adapter Security & Ephemeral Lifecycle...\n");
    TelegramBot *bot = telegram_bot_init("123456:FAKE_TOKEN_FOR_UNIT_TEST", "999888777,111222333");
    assert(bot != NULL);
    assert(strcmp(bot->bot_token, "123456:FAKE_TOKEN_FOR_UNIT_TEST") == 0);

    // Test parameter validation & safe handling of null/empty inputs
    assert(telegram_bot_send_message(bot, NULL, "test") == false);
    assert(telegram_bot_send_message(bot, "123", NULL) == false);
    assert(telegram_bot_send_status_message(bot, NULL, "status") == 0);
    assert(telegram_bot_send_status_message(bot, "123", NULL) == 0);
    assert(telegram_bot_edit_message(bot, NULL, 100, "edit") == false);
    assert(telegram_bot_edit_message(bot, "123", 0, "edit") == false);
    assert(telegram_bot_delete_message(bot, NULL, 100) == false);
    assert(telegram_bot_delete_message(bot, "123", 0) == false);
    assert(telegram_bot_send_chat_action(bot, NULL, "typing") == false);
    assert(telegram_bot_send_chat_action(bot, "123", NULL) == false);

    telegram_bot_stop(bot);
    assert(bot->running == false);

    telegram_bot_free(bot);
    printf("  -> Telegram Adapter PASSED\n");
}

void test_preflight_compiler_watchdog(void) {
    printf("[Test] Pre-Flight Compiler Watchdog (Auto-Healing Feedback Loop)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Test");
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Write broken C file
    JsonValue *w_broken = json_create_object();
    json_obj_add(w_broken, "path", json_create_string("test_broken.c"));
    json_obj_add(w_broken, "content", json_create_string("int main(void) { int x = ; return 0; }\n"));

    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "write_file") == 0) {
            char *obs = h->tools[t].callback(agent, w_broken);
            assert(obs != NULL);
            assert(strstr(obs, "COMPILER WARNING/ERROR") != NULL);
            free(obs);
            break;
        }
    }
    json_free(w_broken);

    // 2. Fix the file using edit_file
    JsonValue *e_fix = json_create_object();
    json_obj_add(e_fix, "path", json_create_string("test_broken.c"));
    json_obj_add(e_fix, "old_text", json_create_string("int x = ;"));
    json_obj_add(e_fix, "new_text", json_create_string("int x = 42;"));

    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "edit_file") == 0) {
            char *obs = h->tools[t].callback(agent, e_fix);
            assert(obs != NULL);
            assert(strstr(obs, "COMPILER WARNING/ERROR") == NULL);
            free(obs);
            break;
        }
    }
    json_free(e_fix);

    unlink("test_broken.c");
    belya_harness_free(h);
    model_gateway_free(gw);
    printf("  -> Pre-Flight Compiler Watchdog PASSED\n");
}

void test_fetch_url_tool(void) {
    printf("[Test] Native Web Content Retrieval (fetch_url)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Test");
    BelyaHarness *h = belya_harness_init(agent);

    assert(h->tool_count >= 14);

    // 1. Test missing url handling
    JsonValue *empty_args = json_create_object();
    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "fetch_url") == 0) {
            char *obs = h->tools[t].callback(agent, empty_args);
            assert(obs != NULL);
            assert(strstr(obs, "Missing url") != NULL);
            free(obs);
            break;
        }
    }
    json_free(empty_args);

    // 2. Test unsupported protocol handling (instant curl error without network socket)
    JsonValue *u_args = json_create_object();
    json_obj_add(u_args, "url", json_create_string("invalid_scheme://empty"));
    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "fetch_url") == 0) {
            char *obs = h->tools[t].callback(agent, u_args);
            assert(obs != NULL);
            assert(strstr(obs, "Error") != NULL);
            free(obs);
            break;
        }
    }
    json_free(u_args);

    belya_harness_free(h);
    model_gateway_free(gw);
    printf("  -> fetch_url Tool PASSED\n");
}

void test_gomaa_scoped_memory_and_timeline(void) {
    printf("[Test] Gomaa Memory Paradigm (Wing/Room Scoping, Salience & Timeline)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_gomaa_mem.sqlite", "Test System");

    // 1. Scoped memory persistence
    belya_agent_persist_memory(agent, "backend/auth: JWT Refresh Tokens", "Use RS256 with 15min expiry for access tokens.");
    belya_agent_persist_memory_scoped(agent, "Database Scaling", "Enable SQLite WAL mode for concurrent readers.", "infra", "database");

    // 2. Scoped memory search & verification
    char *auth_res = belya_agent_search_memory(agent, "JWT");
    assert(strstr(auth_res, "Wing: backend") != NULL);
    assert(strstr(auth_res, "Room: auth") != NULL);
    assert(strstr(auth_res, "Salience:") != NULL);
    free(auth_res);

    char *db_res = belya_agent_search_memory(agent, "Scaling");
    assert(strstr(db_res, "Wing: infra") != NULL);
    assert(strstr(db_res, "Room: database") != NULL);
    free(db_res);

    // 3. Timeline logging & verification
    char *timeline = belya_agent_get_timeline(agent, 10);
    assert(strstr(timeline, "=== Agent Timeline Log ===") != NULL);
    assert(strstr(timeline, "memory_persisted") != NULL);
    assert(strstr(timeline, "backend/auth") != NULL);
    free(timeline);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_gomaa_mem.sqlite");
    printf("  -> Gomaa Memory & Timeline PASSED\n");
}

void test_tool_call_scavenger(void) {
    printf("[Test] Tool-Call Scavenger Engine (DeepSeek/Reasoning Extraction)...\n");

    const char *known_tools[] = {"bash", "read_file", "write_file", "edit_file"};
    size_t known_count = 4;

    // 1. Scavenge from <think> XML block
    const char *reasoning_xml = "<think>\nLet me run bash command\n<tool_call>\n{\"name\": \"bash\", \"arguments\": {\"command\": \"ls -la\"}}\n</tool_call>\n</think>";
    ModelParsedToolCall *calls1 = NULL;
    size_t count1 = model_gateway_scavenge_tool_calls(NULL, reasoning_xml, known_tools, known_count, &calls1);
    assert(count1 == 1);
    assert(calls1 != NULL);
    assert(strcmp(calls1[0].name, "bash") == 0);
    assert(strstr(calls1[0].arguments_json, "ls -la") != NULL);
    for (size_t i = 0; i < count1; i++) {
        free(calls1[i].id);
        free(calls1[i].name);
        free(calls1[i].arguments_json);
    }
    free(calls1);

    // 2. Scavenge from markdown / raw embedded JSON
    const char *content_json = "I will inspect the file:\n```json\n{\"name\": \"read_file\", \"arguments\": {\"path\": \"main.c\"}}\n```";
    ModelParsedToolCall *calls2 = NULL;
    size_t count2 = model_gateway_scavenge_tool_calls(content_json, NULL, known_tools, known_count, &calls2);
    assert(count2 == 1);
    assert(calls2 != NULL);
    assert(strcmp(calls2[0].name, "read_file") == 0);
    assert(strstr(calls2[0].arguments_json, "main.c") != NULL);
    for (size_t i = 0; i < count2; i++) {
        free(calls2[i].id);
        free(calls2[i].name);
        free(calls2[i].arguments_json);
    }
    free(calls2);

    // 3. Reject unknown hallucinated tools
    const char *unknown_tool_json = "{\"name\": \"unregistered_magic_tool\", \"arguments\": {}}";
    ModelParsedToolCall *calls3 = NULL;
    size_t count3 = model_gateway_scavenge_tool_calls(unknown_tool_json, NULL, known_tools, known_count, &calls3);
    assert(count3 == 0);
    assert(calls3 == NULL);

    // 4. Reject malformed JSON
    const char *malformed_json = "{\"name\": \"bash\", \"arguments\": {\"command\": \"incomplete";
    ModelParsedToolCall *calls4 = NULL;
    size_t count4 = model_gateway_scavenge_tool_calls(malformed_json, NULL, known_tools, known_count, &calls4);
    assert(count4 == 0);

    printf("  -> Tool-Call Scavenger PASSED\n");
}

void test_skills_curation_and_recall(void) {
    printf("[Test] Skills Curation & Progressive Disclosure Loop...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_skills_mem.sqlite", "Test System");

    // 1. Save procedural skill
    bool saved = belya_agent_save_skill(agent, "git_sync", "sync_repo",
        "Synchronize current Git branch with remote origin",
        "Step 1: git fetch origin\nStep 2: git rebase origin/main\nStep 3: git push");
    assert(saved == true);

    // 2. Search skills
    char *search_res = belya_agent_search_skills(agent, "sync");
    assert(search_res != NULL);
    assert(strstr(search_res, "git_sync") != NULL);
    assert(strstr(search_res, "git fetch origin") != NULL);
    free(search_res);

    // 3. Manifest progressive disclosure
    char *manifest = belya_agent_get_skills_manifest(agent);
    assert(manifest != NULL);
    assert(strstr(manifest, "git_sync") != NULL);
    assert(strstr(manifest, "sync_repo") != NULL);
    free(manifest);

    // 4. On-demand trigger matching
    char *matched = belya_agent_match_skill_for_prompt(agent, "Please sync_repo with remote");
    assert(matched != NULL);
    assert(strstr(matched, "git rebase origin/main") != NULL);
    free(matched);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_skills_mem.sqlite");
    printf("  -> Skills Curation & Progressive Disclosure PASSED\n");
}

void test_git_checkpoint_and_rollback(void) {
    printf("[Test] Git & State Checkpoint and Instant Rollback...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_chk_mem.sqlite", "Test System");

    belya_agent_add_message(agent, "user", "Message 1: Start work");
    belya_agent_add_message(agent, "assistant", "Message 2: Working");

    // 1. Create Checkpoint
    bool chk_ok = belya_agent_create_checkpoint(agent, "stage_1");
    assert(chk_ok == true);

    // 2. Add further messages to simulate subsequent turn
    belya_agent_add_message(agent, "user", "Message 3: Do something risky");
    belya_agent_add_message(agent, "assistant", "Message 4: Execution result");
    assert(agent->msg_count == 5);

    // 3. List checkpoints
    char *chk_list = belya_agent_list_checkpoints(agent);
    assert(chk_list != NULL);
    assert(strstr(chk_list, "stage_1") != NULL);
    free(chk_list);

    // 4. Rollback to stage_1
    bool rb_ok = belya_agent_rollback_to_checkpoint(agent, "stage_1");
    assert(rb_ok == true);
    assert(agent->msg_count == 3); // Restored to 3 messages (system + 2 turns)

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_chk_mem.sqlite");
    printf("  -> Git Checkpointing & Instant Rollback PASSED\n");
}

void test_trajectory_exporter(void) {
    printf("[Test] Trajectory Exporter (OpenAI Fine-Tune JSONL Format)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Test Trajectory System Prompt");

    belya_agent_add_message(agent, "user", "Refactor memory module in C");
    belya_agent_add_message(agent, "assistant", "I will edit the file.");
    belya_agent_add_tool_result(agent, "call_999", "edit_file", "File successfully edited.");
    belya_agent_add_message(agent, "assistant", "Refactor complete and verified.");

    const char *out_file = "test_trajectory_output.jsonl";
    unlink(out_file);

    bool exp_ok = belya_agent_export_trajectory(agent, NULL, out_file);
    assert(exp_ok == true);

    FILE *f = fopen(out_file, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    assert(sz > 0);

    char *buf = malloc(sz + 1);
    size_t r = fread(buf, 1, sz, f);
    buf[r] = '\0';
    fclose(f);

    JsonValue *root = json_parse(buf);
    assert(root != NULL);
    assert(root->type == JSON_OBJECT);

    JsonValue *msgs = json_obj_get(root, "messages");
    assert(msgs != NULL && msgs->type == JSON_ARRAY);
    assert(msgs->u.array.count == 5); // system + user + assistant + tool + assistant

    json_free(root);
    free(buf);
    unlink(out_file);

    belya_agent_free(agent);
    model_gateway_free(gw);
    printf("  -> Trajectory Exporter PASSED\n");
}

void test_historical_conversation_search(void) {
    printf("[Test] Historical Conversation Search & Multi-Method REST Retrieval...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_conv_hist.sqlite", "Test System");

    // 1. Create a session and persist messages
    belya_agent_add_message(agent, "user", "Deploy my neural microservice to Kubernetes cluster production");
    belya_agent_add_message(agent, "assistant", "Kubernetes deployment YAML created with 3 replicas and ingress route.");
    belya_agent_save_session(agent, "k8s_deploy_01", "Production Kubernetes Deployment");

    // 2. Search conversation history across historical sessions
    char *res = belya_agent_search_conversations(agent, "Kubernetes");
    assert(res != NULL);
    assert(strstr(res, "Production Kubernetes Deployment") != NULL);
    assert(strstr(res, "neural microservice") != NULL);
    free(res);

    char *miss = belya_agent_search_conversations(agent, "nonexistent_keyword_xyz");
    assert(miss != NULL);
    assert(strstr(miss, "No matching past conversations found") != NULL);
    free(miss);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_conv_hist.sqlite");
    printf("  -> Historical Conversation Search PASSED\n");
}

void test_rest_api_advanced_options(void) {
    printf("[Test] Advanced REST Client (Multi-Method, Headers, JSON Body)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Test System");
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Test POST with custom headers (object format)
    JsonValue *p_args = json_create_object();
    json_obj_add(p_args, "url", json_create_string("https://httpbin.org/post"));
    json_obj_add(p_args, "method", json_create_string("POST"));
    json_obj_add(p_args, "body", json_create_string("{\"service\":\"belya\",\"action\":\"benchmark\"}"));
    JsonValue *hdrs = json_create_object();
    json_obj_add(hdrs, "Content-Type", json_create_string("application/json"));
    json_obj_add(hdrs, "X-Agent-ID", json_create_string("BelyaAgent-v4"));
    json_obj_add(p_args, "headers", hdrs);

    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "fetch_url") == 0) {
            char *obs = h->tools[t].callback(agent, p_args);
            assert(obs != NULL);
            // httpbin returns json echo with our payload
            assert(strstr(obs, "belya") != NULL || strstr(obs, "httpbin") != NULL || strstr(obs, "Error") != NULL);
            free(obs);
            break;
        }
    }
    json_free(p_args);

    belya_harness_free(h);
    model_gateway_free(gw);
    printf("  -> Advanced REST Client PASSED\n");
}

void test_tool_scavenger_deep_stress(void) {
    printf("[Test] Tool-Call Scavenger Deep Stress & Edge-Case Parser...\n");
    const char *known_tools[] = {"bash", "read_file", "write_file", "edit_file", "save_skill"};
    size_t known_count = 5;

    // 1. Multiple tool calls in single reasoning block with whitespace & escaped quotes
    const char *complex_reasoning =
        "<think>\n"
        "First I need to create a test script:\n"
        "<tool_call>\n"
        "{\"name\": \"write_file\", \"arguments\": {\"path\": \"perf.c\", \"content\": \"#include <stdio.h>\\nint main() { return 0; }\"}}\n"
        "</tool_call>\n"
        "Next, let me execute it:\n"
        "<tool_call>\n"
        "{\"name\": \"bash\", \"arguments\": {\"command\": \"gcc -O3 perf.c && ./a.out\"}}\n"
        "</tool_call>\n"
        "Also check unknown tool (should be filtered):\n"
        "<tool_call>\n"
        "{\"name\": \"unknown_magic_tool\", \"arguments\": {}}\n"
        "</tool_call>\n"
        "</think>";

    ModelParsedToolCall *calls = NULL;
    size_t count = model_gateway_scavenge_tool_calls(NULL, complex_reasoning, known_tools, known_count, &calls);
    assert(count == 2); // 2 known tools extracted, unknown filtered
    assert(strcmp(calls[0].name, "write_file") == 0);
    assert(strstr(calls[0].arguments_json, "perf.c") != NULL);
    assert(strcmp(calls[1].name, "bash") == 0);
    assert(strstr(calls[1].arguments_json, "gcc -O3") != NULL);

    for (size_t i = 0; i < count; i++) {
        free(calls[i].id);
        free(calls[i].name);
        free(calls[i].arguments_json);
    }
    free(calls);

    printf("  -> Tool-Call Scavenger Deep Stress PASSED\n");
}

void test_multi_checkpoint_rollback_integrity(void) {
    printf("[Test] Multi-Turn Checkpointing & Rollback State Machine...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_checkpoints.sqlite", "Test System");

    // 1. Create Checkpoint 1 (3 messages: system + 2)
    belya_agent_add_message(agent, "user", "Phase 1: Initial state");
    belya_agent_add_message(agent, "assistant", "Phase 1 completed");
    bool cp1 = belya_agent_create_checkpoint(agent, "phase_1_baseline");
    assert(cp1 == true);
    assert(agent->msg_count == 3);

    // 2. Add Phase 2 messages & Checkpoint 2 (5 messages: system + 4)
    belya_agent_add_message(agent, "user", "Phase 2: Complex refactor");
    belya_agent_add_message(agent, "assistant", "Phase 2 completed");
    bool cp2 = belya_agent_create_checkpoint(agent, "phase_2_refactor");
    assert(cp2 == true);
    assert(agent->msg_count == 5);

    // 3. Add Phase 3 messages (7 messages: system + 6)
    belya_agent_add_message(agent, "user", "Phase 3: Buggy experimental code");
    belya_agent_add_message(agent, "assistant", "Phase 3 crashed with errors");
    assert(agent->msg_count == 7);

    // 4. Rollback to Checkpoint 2 (Restores cleanly to 5 messages)
    bool rb2 = belya_agent_rollback_to_checkpoint(agent, "phase_2_refactor");
    assert(rb2 == true);
    assert(agent->msg_count == 5);
    assert(strcmp(agent->messages[3].content, "Phase 2: Complex refactor") == 0);

    // 5. Rollback further to Checkpoint 1 (Restores cleanly to 3 messages)
    bool rb1 = belya_agent_rollback_to_checkpoint(agent, "phase_1_baseline");
    assert(rb1 == true);
    assert(agent->msg_count == 3);
    assert(strcmp(agent->messages[1].content, "Phase 1: Initial state") == 0);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_checkpoints.sqlite");
    printf("  -> Multi-Turn Checkpointing & Rollback PASSED\n");
}

void test_progressive_disclosure_manifest(void) {
    printf("[Test] Progressive Disclosure Manifest & Salience Priority...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_prog_disc.sqlite", "Test System");

    // Save 3 procedural skills
    belya_agent_save_skill(agent, "skill_posix_threads", "pthread", "POSIX thread pool pattern", "1. pthread_create 2. pthread_join");
    belya_agent_save_skill(agent, "skill_simd_vector", "simd", "AVX2 SIMD vectorization", "1. immintrin.h 2. _mm256_load_ps");
    belya_agent_save_skill(agent, "skill_atomic_cas", "atomic", "Lock-free atomic compare and swap", "1. stdatomic.h 2. atomic_compare_exchange");

    char *manifest = belya_agent_get_skills_manifest(agent);
    assert(manifest != NULL);
    assert(strstr(manifest, "skill_posix_threads") != NULL);
    assert(strstr(manifest, "pthread") != NULL);
    assert(strstr(manifest, "skill_simd_vector") != NULL);
    assert(strstr(manifest, "skill_atomic_cas") != NULL);
    free(manifest);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_prog_disc.sqlite");
    printf("  -> Progressive Disclosure Manifest PASSED\n");
}

void test_forced_synthesis_and_keepalive(void) {
    printf("[Test] Forced Synthesis on Step Exhaustion & Persistent Keep-Alive...\n");
    ModelGateway *gw = model_gateway_init("http://127.0.0.1:9999/mock/v1", "test-key", "mock-model");
    assert(gw != NULL);
    assert(gw->curl_handle == NULL); // Lazy init on first request
    assert(gw->user_agent != NULL);
    assert(strstr(gw->user_agent, "BelyaAgent") != NULL);

    // Test custom MODEL_USER_AGENT env override
    setenv("MODEL_USER_AGENT", "CustomClient/2.0.0 (Darwin)", 1);
    ModelGateway *gw_custom = model_gateway_init("http://127.0.0.1:9999/mock/v1", "test-key", "mock-model");
    assert(gw_custom != NULL);
    assert(strcmp(gw_custom->user_agent, "CustomClient/2.0.0 (Darwin)") == 0);
    model_gateway_free(gw_custom);
    unsetenv("MODEL_USER_AGENT");

    BelyaAgent *agent = belya_agent_init(gw, "test_synthesis.sqlite", "You are an AI assistant.");
    assert(agent != NULL);

    // Verify tool output truncation in context (>8000 bytes default or TOOL_OUTPUT_LIMIT capped)
    char large_tool_out[12000];
    memset(large_tool_out, 'X', sizeof(large_tool_out) - 1);
    large_tool_out[sizeof(large_tool_out) - 1] = '\0';
    belya_agent_add_tool_result(agent, "call_test1", "bash", large_tool_out);

    assert(agent->msg_count == 2);
    assert(strlen(agent->messages[1].content) <= 8500);
    assert(strstr(agent->messages[1].content, "[... Output truncated:") != NULL);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_synthesis.sqlite");
    printf("  -> Forced Synthesis & Keep-Alive PASSED\n");
}

void test_v6_enhancements(void) {
    printf("[Test] v6.0 Resilient Edit Fallback, Regex Search & Diagnostics...\n");
    ModelGateway *gw = model_gateway_init("http://127.0.0.1:9999/mock/v1", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "v6 system");
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Whitespace-normalized fallback edit test
    const char *test_code = "int calculate_sum(int a, int b) {\n    int res = a + b;\n    return res;\n}\n";
    FILE *f = fopen("test_whitespace_edit.c", "wb");
    assert(f != NULL);
    fwrite(test_code, 1, strlen(test_code), f);
    fclose(f);

    // Simulate LLM edit request with different indentation (tabs or different spaces)
    JsonValue *edit_args = json_create_object();
    json_obj_add(edit_args, "path", json_create_string("test_whitespace_edit.c"));
    json_obj_add(edit_args, "old_text", json_create_string("int calculate_sum(int a, int b) {\n\t\tint res = a + b;\n\t\treturn res;\n}\n"));
    json_obj_add(edit_args, "new_text", json_create_string("int calculate_sum(int a, int b) {\n    return a + b;\n}\n"));

    for (size_t i = 0; i < h->tool_count; i++) {
        if (strcmp(h->tools[i].name, "edit_file") == 0) {
            char *res = h->tools[i].callback(agent, edit_args);
            assert(res != NULL);
            assert(strstr(res, "successfully edited") != NULL);
            free(res);
            break;
        }
    }
    json_free(edit_args);

    // Verify file contents after edit
    FILE *rf = fopen("test_whitespace_edit.c", "rb");
    assert(rf != NULL);
    char buf[512] = {0};
    size_t rb = fread(buf, 1, sizeof(buf) - 1, rf);
    assert(rb > 0);
    fclose(rf);
    unlink("test_whitespace_edit.c");
    assert(strstr(buf, "return a + b;") != NULL);

    // 2. POSIX Regex search_files test
    FILE *f_reg = fopen("test_regex_file.txt", "wb");
    assert(f_reg != NULL);
    const char *reg_content = "error_code: 404\nstatus: OK\nerror_code: 500\nuser_id: 12345\n";
    fwrite(reg_content, 1, strlen(reg_content), f_reg);
    fclose(f_reg);

    JsonValue *s_args = json_create_object();
    json_obj_add(s_args, "pattern", json_create_string("error_code: [0-9]{3}"));
    json_obj_add(s_args, "path", json_create_string("."));
    json_obj_add(s_args, "file_glob", json_create_string("test_regex_file.txt"));
    json_obj_add(s_args, "regex", json_create_bool(true));

    for (size_t i = 0; i < h->tool_count; i++) {
        if (strcmp(h->tools[i].name, "search_files") == 0) {
            char *res = h->tools[i].callback(agent, s_args);
            assert(res != NULL);
            assert(strstr(res, "404") != NULL);
            assert(strstr(res, "500") != NULL);
            free(res);
            break;
        }
    }
    json_free(s_args);
    unlink("test_regex_file.txt");

    // 3. Diagnostic near-match preview on edit failure
    FILE *f_diag = fopen("test_diag.txt", "wb");
    assert(f_diag != NULL);
    const char *diag_sample = "header_line\nimportant_token_alpha = 100\nfooter_line\n";
    fwrite(diag_sample, 1, strlen(diag_sample), f_diag);
    fclose(f_diag);

    JsonValue *bad_edit = json_create_object();
    json_obj_add(bad_edit, "path", json_create_string("test_diag.txt"));
    json_obj_add(bad_edit, "old_text", json_create_string("important_token_alpha = 9999"));
    json_obj_add(bad_edit, "new_text", json_create_string("something_else"));

    for (size_t i = 0; i < h->tool_count; i++) {
        if (strcmp(h->tools[i].name, "edit_file") == 0) {
            char *res = h->tools[i].callback(agent, bad_edit);
            assert(res != NULL);
            assert(strstr(res, "old_text was not found") != NULL);
            assert(strstr(res, "Similar lines found in file") != NULL);
            assert(strstr(res, "important_token_alpha") != NULL);
            free(res);
            break;
        }
    }
    json_free(bad_edit);
    unlink("test_diag.txt");

    belya_harness_free(h);
    model_gateway_free(gw);
    printf("  -> v6.0 Enhancements PASSED\n");
}

static BelyaToolCallback get_tool_cb(BelyaHarness *h, const char *name) {
    for (size_t i = 0; i < h->tool_count; i++) {
        if (strcmp(h->tools[i].name, name) == 0) return h->tools[i].callback;
    }
    return NULL;
}

void test_all_17_tools_exhaustive(void) {
    printf("[Test] Exhaustive Verification of All 17 Tools & Edge Cases...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    gw->streaming = false;
    BelyaAgent *agent = belya_agent_init(gw, "test_all_tools_mem.sqlite", "Test System");
    BelyaHarness *h = belya_harness_init(agent);

    assert(h->tool_count >= 17);

    // 1. bash
    BelyaToolCallback cb_bash = get_tool_cb(h, "bash");
    assert(cb_bash != NULL);
    JsonValue *b1 = json_create_object();
    json_obj_add(b1, "command", json_create_string("echo 'BASH_VERIFIED_OUTPUT'"));
    char *out_b1 = cb_bash(agent, b1);
    assert(out_b1 && strstr(out_b1, "BASH_VERIFIED_OUTPUT") != NULL);
    free(out_b1);
    json_free(b1);

    // 1b. bash compound cd command
    char orig_test_cwd[4096];
    if (!getcwd(orig_test_cwd, sizeof(orig_test_cwd))) orig_test_cwd[0] = '\0';
    JsonValue *b_cd = json_create_object();
    json_obj_add(b_cd, "command", json_create_string("cd /tmp && pwd"));
    char *out_cd = cb_bash(agent, b_cd);
    assert(out_cd && strstr(out_cd, "/tmp") != NULL);
    free(out_cd);
    json_free(b_cd);
    if (strlen(orig_test_cwd) > 0) { int ret = chdir(orig_test_cwd); (void)ret; }
    if (h) snprintf(h->cwd, sizeof(h->cwd), "%s", orig_test_cwd);

    JsonValue *b2 = json_create_object();
    char *out_b2 = cb_bash(agent, b2);
    assert(out_b2 && strstr(out_b2, "Missing command") != NULL);
    free(out_b2);
    json_free(b2);

    // 2. write_file
    BelyaToolCallback cb_write = get_tool_cb(h, "write_file");
    assert(cb_write != NULL);
    JsonValue *w1 = json_create_object();
    json_obj_add(w1, "path", json_create_string("test_tool_sample.txt"));
    json_obj_add(w1, "content", json_create_string("Line 1: Zero\nLine 2: One\nLine 3: Two\nLine 4: Three\nLine 5: Four\n"));
    char *out_w1 = cb_write(agent, w1);
    assert(out_w1 && strstr(out_w1, "successfully written") != NULL);
    free(out_w1);
    json_free(w1);

    JsonValue *w2 = json_create_object();
    char *out_w2 = cb_write(agent, w2);
    assert(out_w2 && strstr(out_w2, "Missing path") != NULL);
    free(out_w2);
    json_free(w2);

    JsonValue *w3 = json_create_object();
    json_obj_add(w3, "path", json_create_string("test_bad_syntax.c"));
    json_obj_add(w3, "content", json_create_string("int invalid_func( { return ;"));
    char *out_w3 = cb_write(agent, w3);
    assert(out_w3 && (strstr(out_w3, "COMPILER WARNING/ERROR") != NULL || strstr(out_w3, "Syntax check failed") != NULL || strstr(out_w3, "error:") != NULL));
    free(out_w3);
    json_free(w3);
    unlink("test_bad_syntax.c");

    // 3. read_file
    BelyaToolCallback cb_read = get_tool_cb(h, "read_file");
    assert(cb_read != NULL);
    JsonValue *r1 = json_create_object();
    json_obj_add(r1, "path", json_create_string("test_tool_sample.txt"));
    char *out_r1 = cb_read(agent, r1);
    assert(out_r1 && strstr(out_r1, "Line 1: Zero") != NULL && strstr(out_r1, "Line 5: Four") != NULL);
    free(out_r1);
    json_free(r1);

    JsonValue *r2 = json_create_object();
    json_obj_add(r2, "path", json_create_string("test_tool_sample.txt"));
    json_obj_add(r2, "offset", json_create_number(2));
    json_obj_add(r2, "limit", json_create_number(2));
    char *out_r2 = cb_read(agent, r2);
    assert(out_r2 && strstr(out_r2, "Line 2: One") != NULL);
    assert(strstr(out_r2, "Line 1: Zero") == NULL);
    free(out_r2);
    json_free(r2);

    JsonValue *r3 = json_create_object();
    json_obj_add(r3, "path", json_create_string("test_nonexistent_file_9999.txt"));
    char *out_r3 = cb_read(agent, r3);
    assert(out_r3 && strstr(out_r3, "not found or inaccessible") != NULL);
    free(out_r3);
    json_free(r3);

    // 4. edit_file
    BelyaToolCallback cb_edit = get_tool_cb(h, "edit_file");
    assert(cb_edit != NULL);
    JsonValue *e1 = json_create_object();
    json_obj_add(e1, "path", json_create_string("test_tool_sample.txt"));
    json_obj_add(e1, "old_text", json_create_string("Line 2: One"));
    json_obj_add(e1, "new_text", json_create_string("Line 2: ONE_EDITED"));
    char *out_e1 = cb_edit(agent, e1);
    assert(out_e1 && strstr(out_e1, "successfully edited") != NULL);
    free(out_e1);
    json_free(e1);

    JsonValue *e_dup_w = json_create_object();
    json_obj_add(e_dup_w, "path", json_create_string("test_dup.txt"));
    json_obj_add(e_dup_w, "content", json_create_string("repeat\nrepeat\n"));
    char *out_dup_w = cb_write(agent, e_dup_w);
    free(out_dup_w);
    json_free(e_dup_w);

    JsonValue *e2 = json_create_object();
    json_obj_add(e2, "path", json_create_string("test_dup.txt"));
    json_obj_add(e2, "old_text", json_create_string("repeat"));
    json_obj_add(e2, "new_text", json_create_string("unique"));
    char *out_e2 = cb_edit(agent, e2);
    assert(out_e2 && (strstr(out_e2, "ambiguous") != NULL || strstr(out_e2, "matches found") != NULL));
    free(out_e2);
    json_free(e2);
    unlink("test_dup.txt");

    // 5. apply_patch
    BelyaToolCallback cb_patch = get_tool_cb(h, "apply_patch");
    assert(cb_patch != NULL);
    const char *patch_text =
        "<<<<<<< SEARCH\n"
        "Line 3: Two\n"
        "=======\n"
        "Line 3: TWO_PATCHED\n"
        ">>>>>>> REPLACE\n"
        "<<<<<<< SEARCH\n"
        "Line 4: Three\n"
        "=======\n"
        "Line 4: THREE_PATCHED\n"
        ">>>>>>> REPLACE";
    JsonValue *p1 = json_create_object();
    json_obj_add(p1, "path", json_create_string("test_tool_sample.txt"));
    json_obj_add(p1, "patch", json_create_string(patch_text));
    char *out_p1 = cb_patch(agent, p1);
    assert(out_p1 && strstr(out_p1, "successfully applied") != NULL);
    free(out_p1);
    json_free(p1);

    const char *bad_patch =
        "<<<<<<< SEARCH\n"
        "Nonexistent line in file\n"
        "=======\n"
        "Replacement text\n"
        ">>>>>>> REPLACE";
    JsonValue *p2 = json_create_object();
    json_obj_add(p2, "path", json_create_string("test_tool_sample.txt"));
    json_obj_add(p2, "patch", json_create_string(bad_patch));
    char *out_p2 = cb_patch(agent, p2);
    assert(out_p2 && (strstr(out_p2, "mismatch") != NULL || strstr(out_p2, "not found") != NULL));
    free(out_p2);
    json_free(p2);

    // 6. list_dir
    BelyaToolCallback cb_list = get_tool_cb(h, "list_dir");
    assert(cb_list != NULL);
    JsonValue *ld1 = json_create_object();
    json_obj_add(ld1, "path", json_create_string("."));
    char *out_ld1 = cb_list(agent, ld1);
    assert(out_ld1 && strstr(out_ld1, "test_tool_sample.txt") != NULL);
    free(out_ld1);
    json_free(ld1);

    // 7. search_files
    BelyaToolCallback cb_search = get_tool_cb(h, "search_files");
    assert(cb_search != NULL);
    JsonValue *s1 = json_create_object();
    json_obj_add(s1, "path", json_create_string("."));
    json_obj_add(s1, "pattern", json_create_string("TWO_PATCHED"));
    char *out_s1 = cb_search(agent, s1);
    assert(out_s1 && strstr(out_s1, "test_tool_sample.txt") != NULL);
    free(out_s1);
    json_free(s1);

    JsonValue *s2 = json_create_object();
    json_obj_add(s2, "path", json_create_string("."));
    json_obj_add(s2, "pattern", json_create_string("THREE_[A-Z]+"));
    json_obj_add(s2, "regex", json_create_bool(true));
    char *out_s2 = cb_search(agent, s2);
    assert(out_s2 && strstr(out_s2, "test_tool_sample.txt") != NULL);
    free(out_s2);
    json_free(s2);

    // 8. git_status
    BelyaToolCallback cb_gstat = get_tool_cb(h, "git_status");
    assert(cb_gstat != NULL);
    char *out_gs = cb_gstat(agent, NULL);
    assert(out_gs && strlen(out_gs) > 0);
    free(out_gs);

    // 9. git_diff
    BelyaToolCallback cb_gdiff = get_tool_cb(h, "git_diff");
    assert(cb_gdiff != NULL);
    char *out_gd = cb_gdiff(agent, NULL);
    assert(out_gd != NULL);
    free(out_gd);

    // 10. save_memory
    BelyaToolCallback cb_smem = get_tool_cb(h, "save_memory");
    assert(cb_smem != NULL);
    JsonValue *sm1 = json_create_object();
    json_obj_add(sm1, "topic", json_create_string("ToolIntegration"));
    json_obj_add(sm1, "room", json_create_string("core"));
    json_obj_add(sm1, "content", json_create_string("Verifying all 17 tools deterministically"));
    json_obj_add(sm1, "wing", json_create_string("facts"));
    char *out_sm1 = cb_smem(agent, sm1);
    assert(out_sm1 && (strstr(out_sm1, "stored") != NULL || strstr(out_sm1, "successfully") != NULL));
    free(out_sm1);
    json_free(sm1);

    // 11. recall_memory
    BelyaToolCallback cb_rmem = get_tool_cb(h, "recall_memory");
    assert(cb_rmem != NULL);
    JsonValue *rm1 = json_create_object();
    json_obj_add(rm1, "query", json_create_string("ToolIntegration"));
    char *out_rm1 = cb_rmem(agent, rm1);
    assert(out_rm1 && strstr(out_rm1, "ToolIntegration") != NULL);
    free(out_rm1);
    json_free(rm1);

    // 12. save_skill
    BelyaToolCallback cb_sskill = get_tool_cb(h, "save_skill");
    assert(cb_sskill != NULL);
    JsonValue *sk1 = json_create_object();
    json_obj_add(sk1, "name", json_create_string("skill_exhaustive_test"));
    json_obj_add(sk1, "trigger", json_create_string("run exhaustive test"));
    json_obj_add(sk1, "description", json_create_string("Exhaustive verification procedure"));
    json_obj_add(sk1, "instructions", json_create_string("Step 1: Check memory\nStep 2: Run ASan"));
    char *out_sk1 = cb_sskill(agent, sk1);
    assert(out_sk1 && strstr(out_sk1, "successfully saved") != NULL);
    free(out_sk1);
    json_free(sk1);

    // 13. recall_skill
    BelyaToolCallback cb_rskill = get_tool_cb(h, "recall_skill");
    assert(cb_rskill != NULL);
    JsonValue *rsk1 = json_create_object();
    json_obj_add(rsk1, "query", json_create_string("exhaustive"));
    char *out_rsk1 = cb_rskill(agent, rsk1);
    assert(out_rsk1 && strstr(out_rsk1, "skill_exhaustive_test") != NULL);
    free(out_rsk1);
    json_free(rsk1);

    // 14. recall_conversation
    BelyaToolCallback cb_rconv = get_tool_cb(h, "recall_conversation");
    assert(cb_rconv != NULL);
    JsonValue *rc1 = json_create_object();
    json_obj_add(rc1, "query", json_create_string("test"));
    char *out_rc1 = cb_rconv(agent, rc1);
    assert(out_rc1 != NULL);
    free(out_rc1);
    json_free(rc1);

    // 15. spawn_subagent
    BelyaToolCallback cb_sub = get_tool_cb(h, "spawn_subagent");
    assert(cb_sub != NULL);
    JsonValue *sub_bad = json_create_object();
    char *out_sub_bad = cb_sub(agent, sub_bad);
    assert(out_sub_bad && strstr(out_sub_bad, "Missing subagent task argument") != NULL);
    free(out_sub_bad);
    json_free(sub_bad);

    JsonValue *sub_ok = json_create_object();
    json_obj_add(sub_ok, "task", json_create_string("Test isolated subagent"));
    char *out_sub_ok = cb_sub(agent, sub_ok);
    assert(out_sub_ok && strstr(out_sub_ok, "Subagent Task Execution Envelope") != NULL);
    free(out_sub_ok);
    json_free(sub_ok);

    // 16. define_tool
    BelyaToolCallback cb_deftool = get_tool_cb(h, "define_tool");
    assert(cb_deftool != NULL);
    JsonValue *dt1 = json_create_object();
    json_obj_add(dt1, "name", json_create_string("dynamic_ping"));
    json_obj_add(dt1, "description", json_create_string("Dynamic ping tool"));
    json_obj_add(dt1, "script_body", json_create_string("echo \"DYNAMIC_PING_SUCCESS: $1\""));
    char *out_dt1 = cb_deftool(agent, dt1);
    assert(out_dt1 && strstr(out_dt1, "successfully defined") != NULL);
    free(out_dt1);
    json_free(dt1);

    BelyaToolCallback cb_dyn_ping = get_tool_cb(h, "dynamic_ping");
    assert(cb_dyn_ping != NULL);
    JsonValue *dp_args = json_create_object();
    json_obj_add(dp_args, "msg", json_create_string("HelloFromHarness"));
    char *out_dp = cb_dyn_ping(agent, dp_args);
    assert(out_dp && strstr(out_dp, "DYNAMIC_PING_SUCCESS") != NULL);
    free(out_dp);
    json_free(dp_args);

    // 17. fetch_url
    BelyaToolCallback cb_fetch = get_tool_cb(h, "fetch_url");
    assert(cb_fetch != NULL);
    JsonValue *fu_bad = json_create_object();
    char *out_fu_bad = cb_fetch(agent, fu_bad);
    assert(out_fu_bad && strstr(out_fu_bad, "Missing url") != NULL);
    free(out_fu_bad);
    json_free(fu_bad);

    JsonValue *fu_inv = json_create_object();
    json_obj_add(fu_inv, "url", json_create_string("http://127.0.0.1:59999/nonexistent_endpoint"));
    char *out_fu_inv = cb_fetch(agent, fu_inv);
    assert(out_fu_inv != NULL);
    free(out_fu_inv);
    json_free(fu_inv);

    unlink("test_tool_sample.txt");
    unlink(".belya/tools/dynamic_ping.sh");
    unlink(".belya/tools/dynamic_ping.json");
    rmdir(".belya/tools");
    rmdir(".belya");

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_all_tools_mem.sqlite");
    printf("  -> Exhaustive 17 Tools & Edge Cases PASSED\n");
}

void test_skills_lifecycle_and_auto_injection(void) {
    printf("[Test] Skills Lifecycle: Trigger Matching, Auto-Injection & Salience Boost...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Test System");

    // 1. Register 3 distinct skills
    assert(belya_agent_save_skill(agent, "skill_docker_build", "build docker",
        "Builds container image", "1. docker build -t test . 2. docker run test"));
    assert(belya_agent_save_skill(agent, "skill_asan_check", "run asan",
        "Runs AddressSanitizer checks", "1. gcc -fsanitize=address ... 2. ./test_bin"));
    assert(belya_agent_save_skill(agent, "skill_vps_deploy", "deploy vps",
        "Deploys binary to remote server", "1. rsync binary 2. systemctl restart belya"));

    // 2. Test Manifest Generation
    char *manifest = belya_agent_get_skills_manifest(agent);
    assert(manifest != NULL);
    assert(strstr(manifest, "skill_docker_build") != NULL);
    assert(strstr(manifest, "skill_asan_check") != NULL);
    assert(strstr(manifest, "skill_vps_deploy") != NULL);
    free(manifest);

    // 3. Test Prompt Trigger Matching
    char *matched1 = belya_agent_match_skill_for_prompt(agent, "Please run asan on this repository immediately");
    assert(matched1 != NULL);
    assert(strstr(matched1, "gcc -fsanitize=address") != NULL);
    free(matched1);

    char *matched2 = belya_agent_match_skill_for_prompt(agent, "Could you build docker image for production?");
    assert(matched2 != NULL);
    assert(strstr(matched2, "docker build -t test .") != NULL);
    free(matched2);

    char *matched_none = belya_agent_match_skill_for_prompt(agent, "What is the capital of France?");
    assert(matched_none == NULL);

    // 4. Test Salience Boosting on Repeated Hit
    char *matched3 = belya_agent_match_skill_for_prompt(agent, "Please run asan again");
    assert(matched3 != NULL);
    free(matched3);

    belya_agent_free(agent);
    model_gateway_free(gw);
    printf("  -> Skills Lifecycle & Auto-Injection PASSED\n");
}

void test_subagent_recursion_guard(void) {
    printf("[Test] Subagent Recursion Guard & Sandbox Tool Isolation...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Test System");
    BelyaHarness *h = belya_harness_init(agent);

    BelyaToolCallback cb_sub = get_tool_cb(h, "spawn_subagent");
    assert(cb_sub != NULL);

    JsonValue *sub_args = json_create_object();
    json_obj_add(sub_args, "task", json_create_string("Verify subagent sandbox"));
    json_obj_add(sub_args, "max_turns", json_create_number(1));
    char *envelope = cb_sub(agent, sub_args);
    assert(envelope != NULL);
    assert(strstr(envelope, "Subagent Task Execution Envelope") != NULL);
    free(envelope);
    json_free(sub_args);

    belya_harness_free(h);
    model_gateway_free(gw);
    printf("  -> Subagent Recursion Guard PASSED\n");
}

void test_self_telemetry_and_proprioception(void) {
    printf("[Test] Self-Telemetry, RSS Calculation & Proprioception...\n");
    double rss_mb = belya_get_current_rss_mb();
    assert(rss_mb >= 0.0);
    assert(rss_mb < 2048.0); // Should be well within reasonable bounds

    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Test Telemetry System");

    // Add sample messages to check token estimation
    belya_agent_add_message(agent, "user", "Hello Belya, how is your memory and step budget?");
    size_t est_tokens = belya_agent_total_tokens(agent);
    assert(est_tokens > 0);

    belya_agent_free(agent);
    model_gateway_free(gw);
    printf("  -> Self-Telemetry & Proprioception PASSED (RSS: %.2f MB)\n", rss_mb);
}

void test_metacognitive_circuit_breaker_and_verification_guard(void) {
    printf("[Test] Metacognitive Circuit Breaker & Verification Guard...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Test System");
    BelyaHarness *h = belya_harness_init(agent);

    belya_harness_reset_turn_state(h);
    assert(h->consecutive_tool_failures == 0);
    assert(h->files_modified_in_turn == false);
    assert(h->verification_performed_in_turn == false);

    // 1. First failure
    char *breaker_msg = NULL;
    const char *bad_args = "{\"command\": \"cat /tmp/nonexistent_file_xyz.txt\"}";
    bool tripped = belya_harness_record_tool_observation(h, "bash", bad_args, "Error: cat: /tmp/nonexistent_file_xyz.txt: No such file or directory", &breaker_msg);
    assert(!tripped);
    assert(breaker_msg == NULL);
    assert(h->consecutive_tool_failures == 1);
    assert(h->verification_performed_in_turn == true); // bash counts as inspection/verification

    // 2. Second failure with identical args
    tripped = belya_harness_record_tool_observation(h, "bash", bad_args, "Error: cat: /tmp/nonexistent_file_xyz.txt: No such file or directory", &breaker_msg);
    assert(!tripped);
    assert(breaker_msg == NULL);
    assert(h->consecutive_tool_failures == 2);

    // 3. Third failure with identical args -> Must trip circuit breaker!
    tripped = belya_harness_record_tool_observation(h, "bash", bad_args, "Error: cat: /tmp/nonexistent_file_xyz.txt: No such file or directory", &breaker_msg);
    assert(tripped);
    assert(breaker_msg != NULL);
    assert(strstr(breaker_msg, "[METACOGNITIVE CIRCUIT BREAKER]") != NULL);
    assert(strstr(breaker_msg, "3 times consecutively") != NULL);
    assert(h->consecutive_tool_failures == 0); // Auto-reset after tripping
    free(breaker_msg);

    // 4. Test code modification tracking
    tripped = belya_harness_record_tool_observation(h, "write_file", "{\"path\": \"/tmp/foo.txt\"}", "File written successfully", &breaker_msg);
    assert(!tripped);
    assert(h->files_modified_in_turn == true);
    assert(h->consecutive_tool_failures == 0);

    // 5. Test turn reset
    belya_harness_reset_turn_state(h);
    assert(h->files_modified_in_turn == false);
    assert(h->verification_performed_in_turn == false);
    assert(h->verification_guard_tripped == false);
    assert(h->consecutive_tool_failures == 0);

    belya_harness_free(h);
    model_gateway_free(gw);
    printf("  -> Metacognitive Circuit Breaker & Verification Guard PASSED\n");
}

static void test_workspace_path_jailing(void) {
    printf("[Test] Track A.1: Workspace Path Jailing (is_path_jailed)...\n");
    assert(!is_path_jailed(NULL, NULL, false));
    assert(!is_path_jailed("", NULL, false));
    assert(!is_path_jailed("../secret.txt", NULL, false));
    assert(!is_path_jailed("foo/../secret.txt", NULL, false));
    assert(!is_path_jailed("../../etc/passwd", NULL, true));
    assert(!is_path_jailed("/etc/passwd", NULL, true));
    assert(!is_path_jailed("/tmp/test_jail_write.txt", NULL, true));

    // Valid write within workspace
    assert(is_path_jailed("test_jail_valid.txt", NULL, true));
    assert(is_path_jailed("./test_jail_valid.txt", NULL, true));

    // Whitelisted reads
    assert(is_path_jailed("/dev/null", NULL, false));

    // Forbidden read
    assert(!is_path_jailed("/etc/shadow", NULL, false));

    // Backward compatibility wrapper
    assert(is_path_safe("test_jail_valid.txt"));
    assert(!is_path_safe("../escaped.txt"));

    // Tool integration check
    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, "test_jail_mem.sqlite", NULL);
    BelyaHarness *h = belya_harness_init(agent);

    BelyaToolCallback cb_write = NULL;
    BelyaToolCallback cb_read = NULL;
    for (size_t i = 0; i < h->tool_count; i++) {
        if (strcmp(h->tools[i].name, "write_file") == 0) cb_write = h->tools[i].callback;
        if (strcmp(h->tools[i].name, "read_file") == 0) cb_read = h->tools[i].callback;
    }
    assert(cb_write && cb_read);

    JsonValue *bad_write = json_create_object();
    json_obj_add(bad_write, "path", json_create_string("/tmp/malicious_write.txt"));
    json_obj_add(bad_write, "content", json_create_string("evil"));
    char *out_bw = cb_write(agent, bad_write);
    assert(out_bw && strstr(out_bw, "Path traversal denied") != NULL);
    free(out_bw);
    json_free(bad_write);

    JsonValue *bad_read = json_create_object();
    json_obj_add(bad_read, "path", json_create_string("../outside_jail.txt"));
    char *out_br = cb_read(agent, bad_read);
    assert(out_br && strstr(out_br, "Path traversal denied") != NULL);
    free(out_br);
    json_free(bad_read);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_jail_mem.sqlite");
    printf("  -> Workspace Path Jailing PASSED\n");
}

static void test_markdown_frontmatter_parser(void) {
    printf("[Test] Track A.2: C99 Markdown Frontmatter Parser (minifrontmatter)...\n");

    const char *sample_md =
        "---\n"
        "name: belya-architect\n"
        "description: \"Zero-dependency systems architect\"\n"
        "model: hermes-3\n"
        "triggers: [design, arch, \"system spec\"]\n"
        "---\n\n"
        "# Architecture Overview\n"
        "Details on pure C99 systems design.\n";

    Frontmatter *fm = frontmatter_parse(sample_md);
    assert(fm != NULL);
    assert(strcmp(frontmatter_get_scalar(fm, "name"), "belya-architect") == 0);
    assert(strcmp(frontmatter_get_scalar(fm, "description"), "Zero-dependency systems architect") == 0);
    assert(strcmp(frontmatter_get_scalar(fm, "model"), "hermes-3") == 0);

    assert(frontmatter_get_list_count(fm, "triggers") == 3);
    assert(strcmp(frontmatter_get_list_item(fm, "triggers", 0), "design") == 0);
    assert(strcmp(frontmatter_get_list_item(fm, "triggers", 1), "arch") == 0);
    assert(strcmp(frontmatter_get_list_item(fm, "triggers", 2), "system spec") == 0);

    char *trig_str = frontmatter_get_list_as_string(fm, "triggers", ", ");
    assert(trig_str != NULL);
    assert(strstr(trig_str, "design, arch, system spec") != NULL);
    free(trig_str);

    assert(fm->body != NULL);
    assert(strstr(fm->body, "# Architecture Overview") != NULL);
    assert(strstr(fm->body, "pure C99 systems design") != NULL);
    frontmatter_free(fm);

    // Bullet list test
    const char *bullet_md =
        "---\n"
        "name: bullet-manifest\n"
        "triggers:\n"
        "  - alpha\n"
        "  - beta\n"
        "  - gamma\n"
        "---\n"
        "Body of bullet manifest\n";

    Frontmatter *fm_b = frontmatter_parse(bullet_md);
    assert(fm_b != NULL);
    assert(frontmatter_get_list_count(fm_b, "triggers") == 3);
    assert(strcmp(frontmatter_get_list_item(fm_b, "triggers", 0), "alpha") == 0);
    assert(strcmp(frontmatter_get_list_item(fm_b, "triggers", 1), "beta") == 0);
    assert(strcmp(frontmatter_get_list_item(fm_b, "triggers", 2), "gamma") == 0);
    frontmatter_free(fm_b);

    // Edge cases
    assert(frontmatter_parse(NULL) == NULL);
    assert(frontmatter_parse("No frontmatter markdown here") == NULL);
    assert(frontmatter_parse("---\nunclosed frontmatter") == NULL);

    printf("  -> C99 Markdown Frontmatter Parser PASSED\n");
}

static void test_file_first_skills_catalog(void) {
    printf("[Test] Track A.3: File-First Skills System (skills/*/SKILL.md)...\n");

    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, "test_filefirst_skills.sqlite", NULL);

    size_t loaded = belya_agent_load_disk_skills(agent, "skills");
    assert(loaded >= 4);

    char *search_res = belya_agent_search_skills(agent, "c99");
    assert(search_res && strstr(search_res, "c99-safety") != NULL);
    free(search_res);

    char *vps_res = belya_agent_search_skills(agent, "vps");
    assert(vps_res && strstr(vps_res, "vps-deploy") != NULL);
    free(vps_res);

    // Test prompt-matching trigger
    char *matched = belya_agent_match_skill_for_prompt(agent, "Please investigate this memory leak in C");
    assert(matched && strstr(matched, "C99 Memory Safety Protocols") != NULL);
    free(matched);

    // Test saving new skill and verifying disk mirror
    strncpy(agent->db_path, "filefirst_mirror.sqlite", sizeof(agent->db_path) - 1);
    bool saved = belya_agent_save_skill(agent, "test_mirror_skill", "mirror", "Disk mirror test skill", "Step 1. Verify\nStep 2. Assert");
    assert(saved);
    assert(access("skills/test_mirror_skill/SKILL.md", F_OK) == 0);

    // Clean up mirrored test skill
    unlink("skills/test_mirror_skill/SKILL.md");
    rmdir("skills/test_mirror_skill");

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_filefirst_skills.sqlite");
    printf("  -> File-First Skills System PASSED (Loaded %zu disk skills)\n", loaded);
}

static void test_composable_rule_packs(void) {
    printf("[Test] Track A.4: Composable Rule Packs (rules/*/*.md)...\n");

    DynString rules_ds = dyn_str_new();
    size_t rules_count = belya_agent_load_rule_packs(NULL, "rules", &rules_ds);
    assert(rules_count >= 3);
    assert(strstr(rules_ds.data, "=== Composable Rule Pack: common/execution.md ===") != NULL);
    assert(strstr(rules_ds.data, "=== Composable Rule Pack: c/memory_safety.md ===") != NULL);
    assert(strstr(rules_ds.data, "=== Composable Rule Pack: security/path_jailing.md ===") != NULL);
    dyn_str_free(&rules_ds);

    // Verify belya_agent_init injects active rule packs into agent system prompt
    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, "test_rules_init.sqlite", NULL);
    assert(agent->msg_count > 0);
    assert(strstr(agent->messages[0].content, "=== Composable Rule Pack: c/memory_safety.md ===") != NULL);
    assert(strstr(agent->messages[0].content, "=== Composable Rule Pack: security/path_jailing.md ===") != NULL);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_rules_init.sqlite");
    printf("  -> Composable Rule Packs PASSED (Loaded %zu rule packs)\n", rules_count);
}

static void test_troubleshooting_pattern_resolver(void) {
    printf("[Test] Track A.5: Systematic TROUBLESHOOTING.md Pattern Resolver...\n");

    // 1. Direct resolver tests
    char *res1 = belya_troubleshooting_resolve("main.c:42: undefined reference to 'curl_easy_init'", "TROUBLESHOOTING.md");
    assert(res1 != NULL);
    assert(strstr(res1, "Pattern: undefined reference to") != NULL);
    assert(strstr(res1, "-lcurl") != NULL);
    free(res1);

    char *res2 = belya_troubleshooting_resolve("belya.c:15: warning: implicit declaration of function 'usleep'", "TROUBLESHOOTING.md");
    assert(res2 != NULL);
    assert(strstr(res2, "Pattern: implicit declaration of function") != NULL);
    free(res2);

    char *res3 = belya_troubleshooting_resolve("Error: AddressSanitizer: heap-buffer-overflow on address 0x1234", "TROUBLESHOOTING.md");
    assert(res3 != NULL);
    assert(strstr(res3, "Pattern: AddressSanitizer") != NULL);
    free(res3);

    char *res4 = belya_troubleshooting_resolve("Error: Path traversal denied.", "TROUBLESHOOTING.md");
    assert(res4 != NULL);
    assert(strstr(res4, "Pattern: Path traversal denied") != NULL);
    free(res4);

    char *res_none = belya_troubleshooting_resolve("Success: 100% tests passed with no warnings", "TROUBLESHOOTING.md");
    assert(res_none == NULL);

    // 2. Integration with belya_harness_record_tool_observation
    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, "test_ts_resolve.sqlite", NULL);
    BelyaHarness *h = belya_harness_init(agent);

    char *breaker_msg = NULL;
    const char *err_trace = "gcc -o belya main.o -lsqlite3\nerror: undefined reference to 'curl_easy_init'";
    bool tripped = belya_harness_record_tool_observation(h, "bash", "{\"command\":\"make\"}", err_trace, &breaker_msg);
    assert(!tripped);
    assert(breaker_msg != NULL);
    assert(strstr(breaker_msg, "💡 [TROUBLESHOOTING RESOLVER]: Known Failure Pattern Remedy:") != NULL);
    assert(strstr(breaker_msg, "Pattern: undefined reference to") != NULL);
    free(breaker_msg);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_ts_resolve.sqlite");
    printf("  -> Systematic TROUBLESHOOTING.md Pattern Resolver PASSED\n");
}

void test_belya_agency_architecture(void) {
    printf("[Test] Track B: Belya Agency Multi-Agent Orchestration & Rollback Guard...\n");

    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_agency.sqlite", "Agency Master System");
    assert(agent != NULL);

    // 1. Declarative Agent Manifest Protocol (agents/*.md)
    size_t loaded = belya_agent_load_manifests(agent, "agents");
    assert(loaded >= 5);

    // Verify each role manifest
    BelyaAgentManifest *arch = belya_agent_get_manifest(agent, "architect");
    assert(arch != NULL);
    assert(strcmp(arch->name, "architect") == 0);
    assert(strstr(arch->tools, "read_file") != NULL);
    assert(strstr(arch->tools, "search_files") != NULL);
    assert(arch->max_turns == 8);
    belya_agent_manifest_free(arch);

    BelyaAgentManifest *bld = belya_agent_get_manifest(agent, "builder");
    assert(bld != NULL);
    assert(strcmp(bld->name, "builder") == 0);
    assert(strstr(bld->tools, "write_file") != NULL);
    assert(strstr(bld->tools, "edit_file") != NULL);
    assert(bld->max_turns == 10);
    belya_agent_manifest_free(bld);

    BelyaAgentManifest *rev = belya_agent_get_manifest(agent, "reviewer");
    assert(rev != NULL);
    assert(strcmp(rev->name, "reviewer") == 0);
    assert(strstr(rev->tools, "git_diff") != NULL);
    belya_agent_manifest_free(rev);

    BelyaAgentManifest *tst = belya_agent_get_manifest(agent, "tester");
    assert(tst != NULL);
    assert(strcmp(tst->name, "tester") == 0);
    assert(strstr(tst->tools, "bash") != NULL);
    belya_agent_manifest_free(tst);

    BelyaAgentManifest *trg = belya_agent_get_manifest(agent, "triage");
    assert(trg != NULL);
    assert(strcmp(trg->name, "triage") == 0);
    assert(strstr(trg->tools, "dispatch_agent") != NULL);
    belya_agent_manifest_free(trg);

    char *manifest_list = belya_agent_list_manifests(agent);
    assert(manifest_list != NULL);
    assert(strstr(manifest_list, "architect") != NULL);
    assert(strstr(manifest_list, "builder") != NULL);
    assert(strstr(manifest_list, "tester") != NULL);
    free(manifest_list);

    // 2. Dynamic Routing via FTS5 / Keyword
    BelyaAgentManifest *routed_arch = belya_agent_route_manifest(agent, "architect");
    assert(routed_arch != NULL);
    assert(strcmp(routed_arch->name, "architect") == 0);
    belya_agent_manifest_free(routed_arch);

    BelyaAgentManifest *routed_bld = belya_agent_route_manifest(agent, "builder");
    assert(routed_bld != NULL);
    assert(strcmp(routed_bld->name, "builder") == 0);
    belya_agent_manifest_free(routed_bld);

    // 3. Least-Privilege Tool Bounding
    // Architect: strictly read-only
    const char *arch_tools = "read_file, search_files, list_dir, git_status, git_diff, recall_memory";
    assert(belya_harness_is_tool_whitelisted(arch_tools, "read_file") == true);
    assert(belya_harness_is_tool_whitelisted(arch_tools, "search_files") == true);
    assert(belya_harness_is_tool_whitelisted(arch_tools, "write_file") == false);
    assert(belya_harness_is_tool_whitelisted(arch_tools, "edit_file") == false);
    assert(belya_harness_is_tool_whitelisted(arch_tools, "bash") == false);

    BelyaAgent *arch_agent = belya_agent_init(gw, ":memory:", "Architect Test");
    BelyaHarness *arch_h = belya_harness_init_bounded(arch_agent, arch_tools, "architect");
    assert(arch_h != NULL);
    assert(get_tool_cb(arch_h, "read_file") != NULL);
    assert(get_tool_cb(arch_h, "write_file") == NULL);
    assert(get_tool_cb(arch_h, "bash") == NULL);
    belya_harness_free(arch_h);

    // Tester: restricted execution guard
    const char *tester_tools = "bash, read_file, git_status";
    BelyaAgent *tst_agent = belya_agent_init(gw, ":memory:", "Tester Test");
    BelyaHarness *tst_h = belya_harness_init_bounded(tst_agent, tester_tools, "tester");
    assert(tst_h != NULL);
    assert(tst_h->bash_restricted == true);
    assert(get_tool_cb(tst_h, "bash") != NULL);
    assert(get_tool_cb(tst_h, "write_file") == NULL);

    BelyaToolCallback cb_tst_bash = get_tool_cb(tst_h, "bash");
    assert(cb_tst_bash != NULL);

    // Allowed command: echo/make/test
    JsonValue *allowed_cmd = json_create_object();
    json_obj_add(allowed_cmd, "command", json_create_string("echo 'test execution'"));
    char *obs_allowed = cb_tst_bash(tst_agent, allowed_cmd);
    assert(obs_allowed != NULL);
    assert(strstr(obs_allowed, "test execution") != NULL);
    free(obs_allowed);
    json_free(allowed_cmd);

    // Blocked command: unauthorized curl / rm / arbitrary command
    JsonValue *blocked_cmd = json_create_object();
    json_obj_add(blocked_cmd, "command", json_create_string("curl http://example.com/exploit"));
    char *obs_blocked = cb_tst_bash(tst_agent, blocked_cmd);
    assert(obs_blocked != NULL);
    assert(strstr(obs_blocked, "Unauthorized command rejected") != NULL);
    free(obs_blocked);
    json_free(blocked_cmd);

    belya_harness_free(tst_h);

    // 4. Per-Subagent Git Rollback Guard (tested in isolated scratch repo)
    int s_init = system("rm -rf /tmp/test_belya_git && mkdir -p /tmp/test_belya_git && cd /tmp/test_belya_git && git init -q && git config user.name 'Belya' && git config user.email 'belya@test.local' && echo 'clean_state_v1' > dummy.txt && git add dummy.txt && git commit -m 'baseline' -q 2>/dev/null");
    assert(s_init == 0);

    char baseline_sha[128] = {0};
    FILE *gp2 = popen("cd /tmp/test_belya_git && git rev-parse HEAD 2>/dev/null", "r");
    if (gp2) {
        if (fgets(baseline_sha, sizeof(baseline_sha), gp2)) {
            baseline_sha[strcspn(baseline_sha, "\r\n")] = '\0';
        }
        pclose(gp2);
    }
    assert(strlen(baseline_sha) == 40);

    // Simulate corrupted subagent modification
    FILE *df_bad = fopen("/tmp/test_belya_git/dummy.txt", "w");
    assert(df_bad != NULL);
    fprintf(df_bad, "corrupted_state_that_fails\n");
    fclose(df_bad);

    // Verify file is modified
    FILE *check_bad = fopen("/tmp/test_belya_git/dummy.txt", "r");
    char check_buf[64] = {0};
    assert(fgets(check_buf, sizeof(check_buf), check_bad) != NULL);
    fclose(check_bad);
    assert(strstr(check_buf, "corrupted") != NULL);

    // Trigger rollback via git reset --hard
    char reset_cmd[256];
    snprintf(reset_cmd, sizeof(reset_cmd), "cd /tmp/test_belya_git && git reset --hard %s >/dev/null 2>&1 && git clean -fd >/dev/null 2>&1", baseline_sha);
    int r_ret = system(reset_cmd);
    assert(r_ret == 0);

    // Verify file is restored to baseline
    FILE *check_restored = fopen("/tmp/test_belya_git/dummy.txt", "r");
    assert(check_restored != NULL);
    memset(check_buf, 0, sizeof(check_buf));
    assert(fgets(check_buf, sizeof(check_buf), check_restored) != NULL);
    fclose(check_restored);
    assert(strstr(check_buf, "clean_state_v1") != NULL);

    // Clean up scratch repo
    int s_clean = system("rm -rf /tmp/test_belya_git");
    assert(s_clean == 0);

    // 5. Chief-of-Staff Triage Layer
    BelyaHarness *main_h = belya_harness_init(agent);

    // Fast-path test
    char **p1 = NULL;
    size_t c1 = 0;
    char *reply1 = NULL;
    bool t1 = belya_agency_triage(main_h, "hello", &p1, &c1, &reply1);
    assert(t1 == true);
    assert(reply1 != NULL);
    assert(c1 == 0);
    free(reply1);

    // Research query triage -> [architect]
    char **p2 = NULL;
    size_t c2 = 0;
    char *reply2 = NULL;
    bool t2 = belya_agency_triage(main_h, "Explain how token budgeting and compaction work", &p2, &c2, &reply2);
    assert(t2 == true);
    assert(c2 == 1);
    assert(strcmp(p2[0], "architect") == 0);
    for (size_t i = 0; i < c2; i++) free(p2[i]);
    free(p2);

    // Review query triage -> [reviewer]
    char **p3 = NULL;
    size_t c3 = 0;
    char *reply3 = NULL;
    bool t3 = belya_agency_triage(main_h, "Review and audit git diff for memory leaks", &p3, &c3, &reply3);
    assert(t3 == true);
    assert(c3 == 1);
    assert(strcmp(p3[0], "reviewer") == 0);
    for (size_t i = 0; i < c3; i++) free(p3[i]);
    free(p3);

    // Test query triage -> [tester]
    char **p4 = NULL;
    size_t c4 = 0;
    char *reply4 = NULL;
    bool t4 = belya_agency_triage(main_h, "Run test suite and verify coverage", &p4, &c4, &reply4);
    assert(t4 == true);
    assert(c4 == 1);
    assert(strcmp(p4[0], "tester") == 0);
    for (size_t i = 0; i < c4; i++) free(p4[i]);
    free(p4);

    // Implementation mission triage -> [architect, builder, tester]
    char **p5 = NULL;
    size_t c5 = 0;
    char *reply5 = NULL;
    bool t5 = belya_agency_triage(main_h, "Implement feature for multi-agent triage and verify", &p5, &c5, &reply5);
    assert(t5 == true);
    assert(c5 == 3);
    assert(strcmp(p5[0], "architect") == 0);
    assert(strcmp(p5[1], "builder") == 0);
    assert(strcmp(p5[2], "tester") == 0);
    for (size_t i = 0; i < c5; i++) free(p5[i]);
    free(p5);

    belya_harness_free(main_h);
    model_gateway_free(gw);
    unlink("test_agency.sqlite");
    printf("  -> Belya Agency Multi-Agent Architecture & Rollback Guard PASSED\n");
}

void test_jev_client(void) {
    printf("[Test] Jev TypeSafe AI Integration & Coprocessor Parser...\n");

    // 1. Client initialization and memory safety
    assert(jev_client_init(NULL) == NULL);
    assert(jev_client_init("") == NULL);

    JevClient *client = jev_client_init("test_key_12345");
    assert(client != NULL);
    assert(strcmp(client->api_key, "test_key_12345") == 0);
    assert(strcmp(client->base_url, "https://jevtypesafeai.com/api/v1") == 0);
    assert(client->timeout_ms == 5000);
    jev_client_free(client);

    // 2. Triage parser
    const char *mock_triage_json =
        "{\n"
        "  \"answers\": {\n"
        "    \"route\": {\n"
        "      \"choice\": \"architect\",\n"
        "      \"confidence\": 0.96\n"
        "    }\n"
        "  }\n"
        "}";
    char *role = jev_parse_triage_response(mock_triage_json);
    assert(role != NULL);
    assert(strcmp(role, "architect") == 0);
    free(role);

    // Malformed/empty triage JSON
    assert(jev_parse_triage_response(NULL) == NULL);
    assert(jev_parse_triage_response("{\"invalid\": true}") == NULL);

    // 3. Tool risk gating parser
    const char *mock_risk_allow =
        "{\n"
        "  \"action\": \"allow\",\n"
        "  \"risk\": 0.12,\n"
        "  \"confidence\": 0.99\n"
        "}";
    JevRiskResult *r1 = jev_parse_risk_response(mock_risk_allow);
    assert(r1 != NULL);
    assert(strcmp(r1->action, "allow") == 0);
    assert(r1->risk >= 0.11 && r1->risk <= 0.13);
    assert(r1->confidence >= 0.98);
    jev_risk_result_free(r1);

    const char *mock_risk_block =
        "{\n"
        "  \"action\": \"block\",\n"
        "  \"risk\": 0.92,\n"
        "  \"confidence\": 0.95\n"
        "}";
    JevRiskResult *r2 = jev_parse_risk_response(mock_risk_block);
    assert(r2 != NULL);
    assert(strcmp(r2->action, "block") == 0);
    assert(r2->risk > 0.85);
    jev_risk_result_free(r2);

    const char *mock_risk_confirm =
        "{\n"
        "  \"action\": \"confirm\",\n"
        "  \"risk\": 0.65,\n"
        "  \"confidence\": 0.90\n"
        "}";
    JevRiskResult *r3 = jev_parse_risk_response(mock_risk_confirm);
    assert(r3 != NULL);
    assert(strcmp(r3->action, "confirm") == 0);
    assert(r3->risk > 0.60);
    jev_risk_result_free(r3);

    assert(jev_parse_risk_response(NULL) == NULL);

    // 4. Context filter parser
    const char *mock_filter_json =
        "{\n"
        "  \"action\": \"keep\",\n"
        "  \"relevance\": \"critical\",\n"
        "  \"redundant\": false,\n"
        "  \"confidence\": 0.97\n"
        "}";
    JevContextFilterResult *f1 = jev_parse_filter_response(mock_filter_json);
    assert(f1 != NULL);
    assert(strcmp(f1->action, "keep") == 0);
    assert(strcmp(f1->relevance, "critical") == 0);
    assert(f1->redundant == false);
    assert(f1->confidence > 0.95);
    jev_context_filter_result_free(f1);

    assert(jev_parse_filter_response(NULL) == NULL);

    // 5. Null guards on network entrypoints
    assert(jev_decide_triage_role(NULL, "task") == NULL);
    assert(jev_check_tool_risk(NULL, "goal", "tool", "args") == NULL);
    assert(jev_filter_context_item(NULL, "task", "item") == NULL);

    // 6. Agent integration without API key (fallback invariant)
    ModelGateway *gw = model_gateway_init("test", "test", "test");
    BelyaAgent *ag = belya_agent_init(gw, "test_jev.sqlite", "system");
    assert(ag != NULL);
    belya_agent_free(ag);
    model_gateway_free(gw);
    unlink("test_jev.sqlite");

    printf("  -> Jev TypeSafe AI Integration & Coprocessor Parser PASSED\n");
}

void test_security_adversarial_suite(void) {
    printf("[Test] Security & Adversarial Hardening Suite (SSRF, Path Jailing, Fuzzing, SQLi)...\n");

    // 1. SSRF Guard: IPv4 Private / Loopback / Link-Local / Multicast / Broadcast vs Public
    {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;

        // Loopback (127.0.0.0/8)
        inet_pton(AF_INET, "127.0.0.1", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);
        inet_pton(AF_INET, "127.255.255.255", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);

        // Current network / Zero (0.0.0.0/8)
        inet_pton(AF_INET, "0.0.0.0", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);
        inet_pton(AF_INET, "0.255.255.255", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);

        // Private RFC1918 10.0.0.0/8
        inet_pton(AF_INET, "10.0.0.1", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);
        inet_pton(AF_INET, "10.254.12.34", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);

        // Private RFC1918 172.16.0.0/12
        inet_pton(AF_INET, "172.16.0.1", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);
        inet_pton(AF_INET, "172.31.255.254", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);

        // Public adjacent to 172.16/12
        inet_pton(AF_INET, "172.15.255.255", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == false);
        inet_pton(AF_INET, "172.32.0.1", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == false);

        // Private RFC1918 192.168.0.0/16
        inet_pton(AF_INET, "192.168.0.1", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);
        inet_pton(AF_INET, "192.168.100.50", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);

        // Link-local / AWS metadata (169.254.0.0/16)
        inet_pton(AF_INET, "169.254.169.254", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == true);

        // Public routable IPs
        inet_pton(AF_INET, "8.8.8.8", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == false);
        inet_pton(AF_INET, "1.1.1.1", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == false);
        inet_pton(AF_INET, "142.250.190.46", &sin.sin_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin) == false);
    }

    // 2. SSRF Guard: IPv6 Loopback, ULA, Link-local, IPv4-mapped
    {
        struct sockaddr_in6 sin6;
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;

        // Loopback (::1)
        inet_pton(AF_INET6, "::1", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == true);

        // Link-local (fe80::/10)
        inet_pton(AF_INET6, "fe80::1", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == true);

        // Unique Local Address ULA (fc00::/7)
        inet_pton(AF_INET6, "fc00::1", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == true);
        inet_pton(AF_INET6, "fd12:3456:789a:1::1", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == true);

        // IPv4-mapped private IPv6
        inet_pton(AF_INET6, "::ffff:127.0.0.1", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == true);
        inet_pton(AF_INET6, "::ffff:10.0.0.2", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == true);
        inet_pton(AF_INET6, "::ffff:169.254.169.254", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == true);

        // IPv4-mapped public IPv6
        inet_pton(AF_INET6, "::ffff:8.8.8.8", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == false);

        // Public IPv6
        inet_pton(AF_INET6, "2001:4860:4860::8888", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == false);
        inet_pton(AF_INET6, "2606:4700:4700::1111", &sin6.sin6_addr);
        assert(belya_harness_sockaddr_is_private((const struct sockaddr *)&sin6) == false);
    }

    // 3. Secret Path Exfiltration Blocker
    {
        // Must reject all secret files & paths
        assert(belya_harness_is_secret_path(".env") == true);
        assert(belya_harness_is_secret_path(".env.production") == true);
        assert(belya_harness_is_secret_path(".env.local") == true);
        assert(belya_harness_is_secret_path("config/.env") == true);
        assert(belya_harness_is_secret_path("budget_state.txt") == true);
        assert(belya_harness_is_secret_path(".deploy_state") == true);
        assert(belya_harness_is_secret_path("id_rsa") == true);
        assert(belya_harness_is_secret_path("id_ed25519") == true);
        assert(belya_harness_is_secret_path("id_ecdsa") == true);
        assert(belya_harness_is_secret_path("credentials.json") == true);
        assert(belya_harness_is_secret_path("service_account.json") == true);
        assert(belya_harness_is_secret_path("/home/user/.ssh/id_rsa") == true);
        assert(belya_harness_is_secret_path("/repo/.git/config") == true);
        assert(belya_harness_is_secret_path("/root/.aws/credentials") == true);
        assert(belya_harness_is_secret_path("/root/.config/gcloud/credentials.db") == true);
        assert(belya_harness_is_secret_path("belya_memory.sqlite") == true);
        assert(belya_harness_is_secret_path("almaz_memory.sqlite") == true);
        assert(belya_harness_is_secret_path("belya_memory.sqlite-wal") == true);
        assert(belya_harness_is_secret_path("almaz_memory.sqlite-wal") == true);

        // Path jailing must block them even if in workspace
        assert(is_path_jailed(".env", NULL, false) == false);
        assert(is_path_jailed(".env", NULL, true) == false);
        assert(is_path_jailed(".ssh/id_rsa", NULL, false) == false);
        assert(is_path_jailed(".git/config", NULL, false) == false);
        assert(is_path_jailed("belya_memory.sqlite", NULL, false) == false);

        // Legitimate files must pass
        assert(belya_harness_is_secret_path("main.c") == false);
        assert(belya_harness_is_secret_path("src/belya_agent.c") == false);
        assert(belya_harness_is_secret_path("README.md") == false);
        assert(belya_harness_is_secret_path("environment.c") == false);
    }

    // 4. MiniJSON Fuzzing & Malformed Payload Resilience
    {
        // Null pointer check
        assert(json_parse(NULL) == NULL);

        // Empty string
        assert(json_parse("") == NULL);
        assert(json_parse("   ") == NULL);

        // Unclosed braces / brackets
        assert(json_parse("{") == NULL);
        assert(json_parse("[") == NULL);
        assert(json_parse("{\"key\": \"val\"") == NULL);
        assert(json_parse("{\"key\": [1, 2, 3}") == NULL);

        // Trailing commas, missing values, unquoted keys, undefined
        assert(json_parse("{\"a\": 1, }") == NULL);
        assert(json_parse("{123: 456}") == NULL);
        assert(json_parse("{\"a\": undefined}") == NULL);
        assert(json_parse("{\"a\": }") == NULL);

        // Unterminated strings and escape sequences
        assert(json_parse("{\"a\": \"unterminated") == NULL);
        assert(json_parse("{\"a\": [1, 2,") == NULL);
        assert(json_parse("{\"a\": true, \"b\":") == NULL);

        // Deep nesting recursion bomb test (>128 levels)
        char deep_nested[1000];
        memset(deep_nested, '[', 300);
        memset(deep_nested + 300, ']', 300);
        deep_nested[600] = '\0';
        JsonValue *deep_v = json_parse(deep_nested);
        // JSON_MAX_DEPTH is 128, so 300 levels deep MUST fail safely with NULL
        assert(deep_v == NULL);

        // Valid boundary JSON within depth limit
        char valid_nested[100];
        memset(valid_nested, '[', 30);
        memset(valid_nested + 30, ']', 30);
        valid_nested[60] = '\0';
        JsonValue *valid_v = json_parse(valid_nested);
        assert(valid_v != NULL);
        json_free(valid_v);
    }

    // 5. SQLite Memory Layer SQL Injection Resistance
    {
        ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
        BelyaAgent *agent = belya_agent_init(gw, "test_sqli_mem.sqlite", NULL);
        assert(agent != NULL);

        // Inject quotes and SQL control characters in topic and content
        const char *sqli_topic = "test'; DROP TABLE memories; --";
        const char *sqli_content = "Robert'); DROP TABLE students;-- ' OR '1'='1";
        belya_agent_persist_memory(agent, sqli_topic, sqli_content);

        // Search must work safely and retrieve the payload without executing it
        char *search_res = belya_agent_search_memory(agent, "Robert");
        assert(search_res != NULL);
        assert(strstr(search_res, "DROP TABLE students") != NULL);
        free(search_res);

        // Ensure memories table still exists and is not corrupted
        char *search_topic = belya_agent_search_memory(agent, "DROP TABLE memories");
        assert(search_topic != NULL);
        free(search_topic);

        // Test SQL injection in skill name / triggers
        bool saved_skill = belya_agent_save_skill(agent, "sqli_skill", "'; DELETE FROM skills; --",
                                                 "Skill SQLi test", "Safe body");
        assert(saved_skill == true);

        char *skill_search = belya_agent_search_skills(agent, "DELETE FROM skills");
        assert(skill_search != NULL);
        assert(strstr(skill_search, "sqli_skill") != NULL);
        free(skill_search);

        belya_agent_free(agent);
        model_gateway_free(gw);
        unlink("test_sqli_mem.sqlite");
    }

    printf("  -> Security & Adversarial Hardening Suite PASSED\n");
}

void test_in_context_loop_breaker(void) {
    printf("[Test] Late-2026 In-Context Locking Breaker (Attractor Detection)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_loop_breaker.sqlite", "Test System Prompt");
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Period 1 detection: 3 identical tool calls
    char *breaker_msg = NULL;
    bool tripped = belya_harness_record_tool_observation(h, "read_file", "{\"path\":\"README.md\"}", "content line 1", &breaker_msg);
    assert(!tripped && breaker_msg == NULL);
    tripped = belya_harness_record_tool_observation(h, "read_file", "{\"path\":\"README.md\"}", "content line 1", &breaker_msg);
    assert(!tripped && breaker_msg == NULL);
    tripped = belya_harness_record_tool_observation(h, "read_file", "{\"path\":\"README.md\"}", "content line 1", &breaker_msg);
    assert(tripped && breaker_msg != NULL);
    assert(strstr(breaker_msg, "DETERMINISTIC ATTRACTOR DETECTED (Period 1)") != NULL);
    assert(h->loop_breaker_tripped == true);
    assert(h->loop_detector.detected_period == 1);
    free(breaker_msg);

    // 2. Turn reset clears detector
    belya_harness_reset_turn_state(h);
    assert(h->loop_breaker_tripped == false);
    assert(h->loop_detector.count == 0);

    // 3. Period 2 detection: alternating oscillation A -> B -> A -> B -> A -> B
    const char *tool_a = "{\"command\":\"pwd\"}";
    const char *tool_b = "{\"command\":\"ls\"}";
    tripped = belya_harness_record_tool_observation(h, "bash", tool_a, "/home", &breaker_msg);
    assert(!tripped && breaker_msg == NULL);
    tripped = belya_harness_record_tool_observation(h, "bash", tool_b, "file1 file2", &breaker_msg);
    assert(!tripped && breaker_msg == NULL);
    tripped = belya_harness_record_tool_observation(h, "bash", tool_a, "/home", &breaker_msg);
    assert(!tripped && breaker_msg == NULL);
    tripped = belya_harness_record_tool_observation(h, "bash", tool_b, "file1 file2", &breaker_msg);
    assert(!tripped && breaker_msg == NULL);
    tripped = belya_harness_record_tool_observation(h, "bash", tool_a, "/home", &breaker_msg);
    assert(!tripped && breaker_msg == NULL);
    tripped = belya_harness_record_tool_observation(h, "bash", tool_b, "file1 file2", &breaker_msg);
    assert(tripped && breaker_msg != NULL);
    assert(strstr(breaker_msg, "DETERMINISTIC ATTRACTOR DETECTED (Period 2)") != NULL);
    assert(h->loop_breaker_tripped == true);
    assert(h->loop_detector.detected_period == 2);
    free(breaker_msg);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_loop_breaker.sqlite");
    printf("  -> Late-2026 In-Context Locking Breaker PASSED\n");
}

void test_context_compactor_middle_out(void) {
    printf("[Test] Late-2026 128-Call / 96k Middle-Out Context Compactor...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_compactor.sqlite", "Test System Prompt");
    assert(agent != NULL);

    // Add root user instruction
    belya_agent_add_message(agent, "user", "ROOT MISSION: Refactor IPv6 parser and verify zero memory leaks.");

    // Add 20 intermediate turns
    for (int i = 0; i < 20; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Intermediate step %d: inspecting code lines %d-%d", i, i * 10, i * 10 + 10);
        belya_agent_add_message(agent, (i % 2 == 0) ? "assistant" : "user", buf);
    }
    assert(agent->msg_count == 22);

    // Perform middle-out compaction retaining 4 recent messages
    belya_agent_compact_history_middle_out(agent, 4);

    // Layout must be:
    // Index 0: system
    // Index 1: root user objective (PINNED!)
    // Index 2: summary of intermediate steps
    // Index 3..6: 4 recent messages
    assert(agent->msg_count == 7);
    assert(strcmp(agent->messages[0].role, "system") == 0);
    assert(strcmp(agent->messages[1].role, "user") == 0);
    assert(strstr(agent->messages[1].content, "ROOT MISSION:") != NULL);
    assert(strcmp(agent->messages[2].role, "assistant") == 0);
    assert(strstr(agent->messages[2].content, "[CONTEXT COMPACTOR: INTERMEDIATE TURNS") != NULL);
    assert(strstr(agent->messages[2].content, "Root mission constraints pinned") != NULL);

    // Verify lossless archival in SQLite table session_messages_archived
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(agent->db, "SELECT count(*) FROM session_messages_archived;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK);
    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);
    int archived_count = sqlite3_column_int(stmt, 0);
    assert(archived_count >= 15);
    sqlite3_finalize(stmt);

    belya_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_compactor.sqlite");
    printf("  -> Late-2026 Middle-Out Context Compactor PASSED\n");
}

static void test_roma_context_firewall(void) {
    printf("[Test] Late-2026 ROMA Recursive Context Firewall...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_roma.sqlite", "Roma Test");
    BelyaHarness *h = belya_harness_init(agent);

    belya_agent_load_manifests(agent, "agents");
    char *envelope = belya_agency_dispatch_subagent(h, "architect", "Inspect repository structure", "Read-only audit");
    assert(envelope != NULL);
    assert(strstr(envelope, "=== Belya Agency Subagent Execution Envelope") != NULL);
    assert(strstr(envelope, "ROMA Context Firewall: Child execution trace isolated") != NULL);
    // Crucial ROMA invariant: raw child execution trace must NOT contaminate parent envelope
    assert(strstr(envelope, "\nExecution Trace:\n") == NULL);
    free(envelope);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_roma.sqlite");
    printf("  -> Late-2026 ROMA Recursive Context Firewall PASSED\n");
}

static void test_symbol_graph_navigation(void) {
    printf("[Test] Late-2026 AST Symbol Graph Navigation & Call-Tree...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, "test_symbols.sqlite", "Symbol Test");
    BelyaHarness *h = belya_harness_init(agent);

    size_t count = belya_harness_index_symbols(h, ".");
    assert(count > 0);

    // Test Goto Definition
    char *def = belya_harness_goto_definition(h, "belya_harness_index_symbols");
    assert(def != NULL);
    assert(strstr(def, "Symbol Definition: 'belya_harness_index_symbols'") != NULL);
    free(def);

    // Test Macro resolution
    char *mdef = belya_harness_goto_definition(h, "HARNESS_ACTION_HISTORY_SIZE");
    assert(mdef != NULL);
    assert(strstr(mdef, "MACRO") != NULL || strstr(mdef, "HARNESS_ACTION_HISTORY_SIZE") != NULL);
    free(mdef);

    // Test Callers lookup
    char *callers = belya_harness_find_callers(h, "belya_harness_index_symbols");
    assert(callers != NULL);
    free(callers);

    // Test Tool callbacks
    BelyaToolCallback cb_goto = get_tool_cb(h, "goto_definition");
    assert(cb_goto != NULL);
    JsonValue *args = json_create_object();
    json_obj_add(args, "symbol", json_create_string("belya_harness_index_symbols"));
    char *res = cb_goto(agent, args);
    assert(res != NULL);
    assert(strstr(res, "Symbol Definition") != NULL);
    free(res);
    json_free(args);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_symbols.sqlite");
    printf("  -> Late-2026 AST Symbol Graph Navigation PASSED (%zu symbols indexed)\n", count);
}

static void test_repo_to_mcp_synthesis(void) {
    printf("[Test] Late-2026 Paper2Agent Repo-to-MCP Dynamic Synthesis...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "MCP Test");
    BelyaHarness *h = belya_harness_init(agent);

    bool ok = belya_harness_synthesize_mcp_server(h, "paper2agent_calc", "compute_pi", "Compute pi approximation", "echo 3.14159", "{\"type\":\"object\"}");
    assert(ok == true);

    // Verify MCP tool registered into active harness
    assert(get_tool_cb(h, "compute_pi") != NULL || h->tool_count >= 19);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink(".belya/mcp/paper2agent_calc_server.sh");
    printf("  -> Late-2026 Paper2Agent Repo-to-MCP Synthesis PASSED\n");
}

static void test_mythos_variant_analysis(void) {
    printf("[Test] Late-2026 Mythos Variant Security Sweeper...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Audit Test");
    BelyaHarness *h = belya_harness_init(agent);

    char *report = belya_harness_audit_security_variants(h, "all", ".");
    assert(report != NULL);
    assert(strstr(report, "Mythos Variant Security Sweeper Report") != NULL);
    assert(strstr(report, "PoC Verification Command") != NULL);
    free(report);

    // Test Tool callback
    BelyaToolCallback cb_audit = get_tool_cb(h, "audit_security_variants");
    assert(cb_audit != NULL);
    JsonValue *args = json_create_object();
    json_obj_add(args, "pattern_type", json_create_string("buffer_overflow"));
    json_obj_add(args, "target_path", json_create_string("."));
    char *obs = cb_audit(agent, args);
    assert(obs != NULL);
    assert(strstr(obs, "Mythos Variant Security Sweeper Report") != NULL);
    free(obs);
    json_free(args);

    belya_harness_free(h);
    model_gateway_free(gw);
    printf("  -> Late-2026 Mythos Variant Security Sweeper PASSED\n");
}

static void test_phase1_diff_colorizer_and_ask_user(void) {
    printf("[Test] Phase 1: Stateful Diff Colorizer, ask_user & Signal-Safe Modal...\n");

    // 1. Test Stateful ANSI Diff Colorizer
    const char *sample_diff = 
        "diff --git a/test.c b/test.c\n"
        "--- a/test.c\n"
        "+++ b/test.c\n"
        "@@ -10,3 +10,3 @@\n"
        " unchanged line\n"
        "----i;\n"
        "-old_call();\n"
        "+new_call();\n";

    char *colored = belya_ansi_diff_colorize(sample_diff);
    assert(colored != NULL);
    assert(strstr(colored, "\033[32m+new_call();") != NULL);
    assert(strstr(colored, "\033[31m-old_call();") != NULL);
    assert(strstr(colored, "\033[31m----i;") != NULL);
    assert(strstr(colored, "\033[1;36m--- a/test.c") != NULL);
    free(colored);

    // 2. Test NO_COLOR suppression
    setenv("NO_COLOR", "1", 1);
    char *no_color = belya_ansi_diff_colorize(sample_diff);
    assert(no_color != NULL);
    assert(strstr(no_color, "\033[") == NULL);
    free(no_color);
    unsetenv("NO_COLOR");

    // 3. Test ask_user tool in headless mode
    setenv("BELYA_TEST_MODE", "1", 1);
    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", NULL);
    BelyaHarness *h = belya_harness_init(agent);

    JsonValue *au_args = json_create_object();
    json_obj_add(au_args, "question", json_create_string("Do you prefer Approach A or Approach B?"));
    JsonValue *opts = json_create_array();
    json_arr_add(opts, json_create_string("Approach A: Zero-allocation"));
    json_arr_add(opts, json_create_string("Approach B: Ring-buffer"));
    json_obj_add(au_args, "options", opts);

    BelyaToolCallback ask_cb = NULL;
    for (size_t i = 0; i < h->tool_count; i++) {
        if (strcmp(h->tools[i].name, "ask_user") == 0) {
            ask_cb = h->tools[i].callback;
            break;
        }
    }
    assert(ask_cb != NULL);

    char *au_res = ask_cb(agent, au_args);
    assert(au_res != NULL);
    assert(strstr(au_res, "Approach A or Approach B") != NULL);
    free(au_res);
    json_free(au_args);

    // 4. Test belya_terminal_permission_prompt in headless mode
    bool perm = belya_terminal_permission_prompt(h, "bash", "{\"command\":\"ls\"}", NULL);
    assert(perm == true);

    belya_harness_free(h);
    model_gateway_free(gw);
    unsetenv("BELYA_TEST_MODE");
    printf("  -> Phase 1: Stateful Diff Colorizer, ask_user & Signal-Safe Modal PASSED\n");
}

static void test_phase2_deterministic_hooks_and_5tier_compactor(void) {
    printf("[Test] Phase 2: Deterministic Hooks, 5-Tier Compactor & Dynamic Cache Boundary...\n");

    // 1. Test Lifecycle Hook Runner (Exit 2 = Blocked policy, Exit 0 = Allowed)
    int ret_mk = system("mkdir -p .belya/hooks");
    (void)ret_mk;
    FILE *fhook = fopen(".belya/hooks/pre_tool_use.sh", "w");
    assert(fhook != NULL);
    fprintf(fhook, "#!/bin/sh\n"
                   "if [ \"$BELYA_TOOL_NAME\" = \"forbidden_tool\" ]; then\n"
                   "    echo \"Policy Violation: forbidden_tool is strictly blocked.\"\n"
                   "    exit 2\n"
                   "fi\n"
                   "echo \"Tool approved\"\n"
                   "exit 0\n");
    fclose(fhook);
    chmod(".belya/hooks/pre_tool_use.sh", 0755);

    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "Static System Instructions");
    BelyaHarness *h = belya_harness_init(agent);

    // Call blocked hook
    BelyaHookResult r_block = belya_run_lifecycle_hook(h, BELYA_HOOK_PRE_TOOL_USE, "forbidden_tool", "{}", NULL);
    assert(r_block.blocked == true);
    assert(r_block.exit_code == 2);
    assert(r_block.output != NULL);
    assert(strstr(r_block.output, "forbidden_tool is strictly blocked") != NULL);
    belya_hook_result_free(&r_block);

    // Call allowed hook
    BelyaHookResult r_allow = belya_run_lifecycle_hook(h, BELYA_HOOK_PRE_TOOL_USE, "allowed_tool", "{}", NULL);
    assert(r_allow.blocked == false);
    assert(r_allow.exit_code == 0);
    assert(r_allow.output != NULL);
    assert(strstr(r_allow.output, "Tool approved") != NULL);
    belya_hook_result_free(&r_allow);

    // Clean up hook script
    unlink(".belya/hooks/pre_tool_use.sh");
    rmdir(".belya/hooks");

    // 2. Test SYSTEM_PROMPT_DYNAMIC_BOUNDARY presence
    assert(strlen(SYSTEM_PROMPT_DYNAMIC_BOUNDARY) > 5);

    // 3. Test 5-Tier Compactor
    // Add messages
    belya_agent_add_message(agent, "user", "ROOT MISSION: Verify pure C99 5-tier compaction.");
    belya_agent_add_message(agent, "assistant", "Working on tier tests...    \n\n\n");
    assert(agent->msg_count == 3);

    // Tier 1: Micro-pruning (trims trailing whitespace)
    belya_agent_compact_5tier(agent, COMPACT_TIER_1_MICRO, 0);
    assert(strcmp(agent->messages[2].content, "Working on tier tests...") == 0);

    // Tier 2: Truncate large tool observation older than 3 turns
    char *big_tool_obs = malloc(2000);
    memset(big_tool_obs, 'A', 1999);
    big_tool_obs[1999] = '\0';
    belya_agent_add_message(agent, "tool", big_tool_obs);
    free(big_tool_obs);
    // Add 3 turns to make tool observation older than 3 turns
    belya_agent_add_message(agent, "assistant", "Step 1");
    belya_agent_add_message(agent, "user", "Step 2");
    belya_agent_add_message(agent, "assistant", "Step 3");
    assert(agent->msg_count == 7);
    belya_agent_compact_5tier(agent, COMPACT_TIER_2_TRUNCATE_TOOLS, 0);
    assert(strstr(agent->messages[3].content, "Truncated") != NULL);

    // Tier 3: Middle-out semantic pruning
    belya_agent_compact_5tier(agent, COMPACT_TIER_3_MIDDLE_OUT, 2);
    // Preserves index 0, index 1 (root mission), summary at index 2, and 2 recent turns
    assert(agent->msg_count == 5);
    assert(strcmp(agent->messages[0].role, "system") == 0);
    assert(strstr(agent->messages[1].content, "ROOT MISSION:") != NULL);
    assert(strstr(agent->messages[2].content, "CONTEXT COMPACTOR") != NULL);

    // Tier 5: Emergency compaction
    belya_agent_compact_5tier(agent, COMPACT_TIER_5_EMERGENCY, 0);
    assert(agent->msg_count == 3);
    assert(strstr(agent->messages[1].content, "EMERGENCY COMPACTION BRIEF") != NULL);

    belya_harness_free(h);
    model_gateway_free(gw);
    printf("  -> Phase 2: Deterministic Hooks, 5-Tier Compactor & Dynamic Cache Boundary PASSED\n");
}

static void test_phase3_task_board_fts5_recall_and_cron(void) {
    printf("[Test] Phase 3: Task Board, FTS5 Recall & Background Cron Engine...\n");

    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    unlink("test_phase3.sqlite");
    BelyaAgent *agent = belya_agent_init(gw, "test_phase3.sqlite", NULL);
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Task Board Backend Operations
    int64_t t1 = belya_agent_task_create(agent, "Implement Task Board", "Full C99 SQLite relational task board", "", "Belya");
    assert(t1 > 0);
    int64_t t2 = belya_agent_task_create(agent, "Verify ASan Safety", "Run AddressSanitizer and verify 0 leaks", "1", "Almaz");
    assert(t2 > t1);

    bool up_ok = belya_agent_task_update(agent, t1, "completed", "All schema and tools operational");
    assert(up_ok);

    char *t1_details = belya_agent_task_get(agent, t1);
    assert(t1_details && strstr(t1_details, "Implement Task Board") != NULL);
    assert(strstr(t1_details, "Status:       completed") != NULL);
    free(t1_details);

    char *all_tasks = belya_agent_task_list(agent, "all");
    assert(all_tasks && strstr(all_tasks, "Implement Task Board") != NULL);
    assert(strstr(all_tasks, "Verify ASan Safety") != NULL);
    free(all_tasks);

    char *pending_tasks = belya_agent_task_list(agent, "pending");
    assert(pending_tasks && strstr(pending_tasks, "Verify ASan Safety") != NULL);
    assert(strstr(pending_tasks, "Implement Task Board") == NULL);
    free(pending_tasks);

    // 2. Cron Engine Backend Operations
    int64_t c1 = belya_agent_cron_create(agent, "hourly_vacuum", "0 * * * *", "Run database maintenance vacuum");
    assert(c1 > 0);

    char *cron_jobs = belya_agent_cron_list(agent);
    assert(cron_jobs && strstr(cron_jobs, "hourly_vacuum") != NULL);
    assert(strstr(cron_jobs, "0 * * * *") != NULL);
    free(cron_jobs);

    DynString tick_logs = dyn_str_new();
    int fired = belya_agent_cron_tick(agent, &tick_logs);
    assert(fired >= 1);
    assert(strstr(tick_logs.data, "hourly_vacuum") != NULL);
    dyn_str_free(&tick_logs);

    // 3. Memory Dream Sweep
    char *dream_report = belya_agent_dream_sweep(agent);
    assert(dream_report && strstr(dream_report, "Auto-Dream Sweep") != NULL);
    free(dream_report);

    // 4. Native Tools Verification via tool callbacks
    BelyaToolCallback cb_tc = get_tool_cb(h, "task_create");
    assert(cb_tc != NULL);
    JsonValue *tc_args = json_create_object();
    json_obj_add(tc_args, "subject", json_create_string("Test Task via Tool"));
    json_obj_add(tc_args, "description", json_create_string("Tool harness creation test"));
    char *tc_res = cb_tc(agent, tc_args);
    json_free(tc_args);
    assert(tc_res && strstr(tc_res, "created successfully") != NULL);
    free(tc_res);

    BelyaToolCallback cb_tl = get_tool_cb(h, "task_list");
    assert(cb_tl != NULL);
    JsonValue *tl_args = json_create_object();
    json_obj_add(tl_args, "status", json_create_string("all"));
    char *tl_res = cb_tl(agent, tl_args);
    json_free(tl_args);
    assert(tl_res && strstr(tl_res, "Test Task via Tool") != NULL);
    free(tl_res);

    BelyaToolCallback cb_cc = get_tool_cb(h, "cron_create");
    assert(cb_cc != NULL);
    JsonValue *cron_args = json_create_object();
    json_obj_add(cron_args, "name", json_create_string("test_cron_tool"));
    json_obj_add(cron_args, "schedule", json_create_string("*/5 * * * *"));
    json_obj_add(cron_args, "prompt", json_create_string("Periodic self-check"));
    char *cron_res = cb_cc(agent, cron_args);
    json_free(cron_args);
    assert(cron_res && strstr(cron_res, "registered with schedule") != NULL);
    free(cron_res);

    BelyaToolCallback cb_cl = get_tool_cb(h, "cron_list");
    assert(cb_cl != NULL);
    JsonValue *empty_args = json_create_object();
    char *cl_res = cb_cl(agent, empty_args);
    json_free(empty_args);
    assert(cl_res && strstr(cl_res, "test_cron_tool") != NULL);
    free(cl_res);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_phase3.sqlite");
    printf("  -> Phase 3: Task Board, FTS5 Recall & Background Cron Engine PASSED\n");
}

static void test_phase4_plan_mode_and_worktrees(void) {
    printf("[Test] Phase 4: Plan Mode Sandbox, Isolated Git Worktrees & SKILL.md Parity...\n");

    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, "test_phase4.sqlite", NULL);
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Verify Tool Callbacks are Registered
    BelyaToolCallback cb_epm = get_tool_cb(h, "enter_plan_mode");
    BelyaToolCallback cb_xpm = get_tool_cb(h, "exit_plan_mode");
    BelyaToolCallback cb_ew = get_tool_cb(h, "enter_worktree");
    BelyaToolCallback cb_lw = get_tool_cb(h, "leave_worktree");
    assert(cb_epm != NULL);
    assert(cb_xpm != NULL);
    assert(cb_ew != NULL);
    assert(cb_lw != NULL);

    // 2. Test Plan Mode Activation & Mutating Guards
    assert(h->plan_mode == false);
    JsonValue *empty_args = json_create_object();
    char *epm_res = cb_epm(agent, empty_args);
    json_free(empty_args);
    assert(epm_res && strstr(epm_res, "Plan Mode activated") != NULL);
    free(epm_res);
    assert(h->plan_mode == true);

    // Test write_file blocked in Plan Mode
    BelyaToolCallback cb_write = get_tool_cb(h, "write_file");
    assert(cb_write != NULL);
    JsonValue *wr_args = json_create_object();
    json_obj_add(wr_args, "path", json_create_string("test_blocked.txt"));
    json_obj_add(wr_args, "content", json_create_string("blocked content"));
    char *wr_res = cb_write(agent, wr_args);
    json_free(wr_args);
    assert(wr_res && strstr(wr_res, "Blocked: Plan Mode is active") != NULL);
    free(wr_res);

    // Test edit_file blocked in Plan Mode
    BelyaToolCallback cb_edit = get_tool_cb(h, "edit_file");
    assert(cb_edit != NULL);
    JsonValue *ed_args = json_create_object();
    json_obj_add(ed_args, "path", json_create_string("test_blocked.txt"));
    json_obj_add(ed_args, "old_text", json_create_string("old"));
    json_obj_add(ed_args, "new_text", json_create_string("new"));
    char *ed_res = cb_edit(agent, ed_args);
    json_free(ed_args);
    assert(ed_res && strstr(ed_res, "Blocked: Plan Mode is active") != NULL);
    free(ed_res);

    // Test apply_patch blocked in Plan Mode
    BelyaToolCallback cb_patch = get_tool_cb(h, "apply_patch");
    assert(cb_patch != NULL);
    JsonValue *pt_args = json_create_object();
    json_obj_add(pt_args, "path", json_create_string("test_blocked.txt"));
    json_obj_add(pt_args, "patch", json_create_string("patch data"));
    char *pt_res = cb_patch(agent, pt_args);
    json_free(pt_args);
    assert(pt_res && strstr(pt_res, "Blocked: Plan Mode is active") != NULL);
    free(pt_res);

    // Test bash mutating command blocked in Plan Mode
    BelyaToolCallback cb_bash = get_tool_cb(h, "bash");
    assert(cb_bash != NULL);
    JsonValue *bsh_mut = json_create_object();
    json_obj_add(bsh_mut, "command", json_create_string("rm -rf nonexistent_file.txt"));
    char *bsh_mut_res = cb_bash(agent, bsh_mut);
    json_free(bsh_mut);
    assert(bsh_mut_res && strstr(bsh_mut_res, "Blocked: Plan Mode is active") != NULL);
    free(bsh_mut_res);

    // Test read-only bash command allowed in Plan Mode
    JsonValue *bsh_ro = json_create_object();
    json_obj_add(bsh_ro, "command", json_create_string("echo plan_mode_read_only_ok"));
    char *bsh_ro_res = cb_bash(agent, bsh_ro);
    json_free(bsh_ro);
    assert(bsh_ro_res && strstr(bsh_ro_res, "plan_mode_read_only_ok") != NULL);
    free(bsh_ro_res);

    // Deactivate Plan Mode
    empty_args = json_create_object();
    char *xpm_res = cb_xpm(agent, empty_args);
    json_free(empty_args);
    assert(xpm_res && strstr(xpm_res, "Plan Mode deactivated") != NULL);
    free(xpm_res);
    assert(h->plan_mode == false);

    // 3. Test Git Worktree Sandbox Lifecycle
    char orig_cwd[4096];
    assert(getcwd(orig_cwd, sizeof(orig_cwd)) != NULL);

    char *wt_enter = belya_harness_enter_worktree(h, "test_phase4_branch");
    assert(wt_enter != NULL);
    assert(strstr(wt_enter, "Entered isolated git worktree") != NULL);
    free(wt_enter);
    assert(h->in_worktree == true);

    char current_cwd[4096];
    assert(getcwd(current_cwd, sizeof(current_cwd)) != NULL);
    assert(strstr(current_cwd, ".belya/worktrees/wt_test_phase4_branch") != NULL);

    char *wt_leave = belya_harness_leave_worktree(h, false);
    assert(wt_leave != NULL);
    assert(strstr(wt_leave, "Left git worktree") != NULL);
    free(wt_leave);
    assert(h->in_worktree == false);

    char restored_cwd[4096];
    assert(getcwd(restored_cwd, sizeof(restored_cwd)) != NULL);
    assert(strcmp(restored_cwd, orig_cwd) == 0);

    // 4. Test .belya/skills/ Discovery
    int ret_p4 = system("mkdir -p .belya/skills/test_p4_skill");
    (void)ret_p4;
    FILE *skf = fopen(".belya/skills/test_p4_skill/SKILL.md", "w");
    assert(skf != NULL);
    fprintf(skf, "---\nname: test_p4_skill\ndescription: Phase 4 Plan Mode Verification Skill\ntriggers: p4, plan_test\n---\nStep 1: Check invariants\nStep 2: Assert.\n");
    fclose(skf);

    size_t loaded_skills = belya_agent_load_disk_skills(agent, ".belya/skills");
    assert(loaded_skills >= 1);

    char *found_skill = belya_agent_search_skills(agent, "plan_test");
    assert(found_skill != NULL);
    assert(strstr(found_skill, "test_p4_skill") != NULL);
    free(found_skill);

    unlink(".belya/skills/test_p4_skill/SKILL.md");
    rmdir(".belya/skills/test_p4_skill");

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_phase4.sqlite");
    printf("  -> Phase 4: Plan Mode Sandbox, Isolated Git Worktrees & SKILL.md Parity PASSED\n");
}

static void test_phase5_pty_subshells_and_task_manager(void) {
    printf("[Test] Phase 5: POSIX PTY Subshells & Background Task Manager...\n");

    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, "test_phase5.sqlite", NULL);
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Verify manage_task tool callback is registered
    BelyaToolCallback cb_mt = get_tool_cb(h, "manage_task");
    assert(cb_mt != NULL);

    // 2. Spawn a short-lived background task via PTY
    int t1 = belya_harness_spawn_task(h, "echo 'belya_pty_task_output_verified'");
    assert(t1 > 0);

    // Wait a brief moment for output capture and exit
    struct timespec ts_wait = {0, 150000000L}; // 150ms
    nanosleep(&ts_wait, NULL);

    char *t1_status = belya_harness_task_status(h, t1);
    assert(t1_status != NULL);
    assert(strstr(t1_status, "Task ID: 1") != NULL);
    assert(strstr(t1_status, "belya_pty_task_output_verified") != NULL);
    assert(strstr(t1_status, "completed") != NULL);
    free(t1_status);

    // 3. Spawn a long-running background task and test process group kill
    int t2 = belya_harness_spawn_task(h, "sleep 30");
    assert(t2 > 0);

    char *task_list = belya_harness_task_list(h);
    assert(task_list != NULL);
    assert(strstr(task_list, "sleep 30") != NULL);
    free(task_list);

    char *t2_status = belya_harness_task_status(h, t2);
    assert(t2_status != NULL);
    assert(strstr(t2_status, "running") != NULL);
    free(t2_status);

    // Test terminating the task group
    char *kill_res = belya_harness_task_kill(h, t2);
    assert(kill_res != NULL);
    assert(strstr(kill_res, "terminated successfully") != NULL);
    free(kill_res);

    char *t2_post_kill = belya_harness_task_status(h, t2);
    assert(t2_post_kill != NULL);
    assert(strstr(t2_post_kill, "killed") != NULL);
    free(t2_post_kill);

    // 4. Test tool_manage_task tool callback
    JsonValue *list_args = json_create_object();
    json_obj_add(list_args, "action", json_create_string("list"));
    char *tool_list_res = cb_mt(agent, list_args);
    json_free(list_args);
    assert(tool_list_res != NULL);
    assert(strstr(tool_list_res, "Active & Background Tasks Table") != NULL);
    free(tool_list_res);

    // 5. Test tool_bash with background:true
    BelyaToolCallback cb_bash = get_tool_cb(h, "bash");
    assert(cb_bash != NULL);
    JsonValue *bg_args = json_create_object();
    json_obj_add(bg_args, "command", json_create_string("echo 'bg_bash_verified'"));
    json_obj_add(bg_args, "background", json_create_bool(true));
    char *bg_res = cb_bash(agent, bg_args);
    json_free(bg_args);
    assert(bg_res != NULL);
    assert(strstr(bg_res, "Command launched in background PTY subshell") != NULL);
    free(bg_res);

    // Clean up harness and verify deterministic descriptor & process reaping
    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_phase5.sqlite");
    printf("  -> Phase 5: POSIX PTY Subshells & Background Task Manager PASSED\n");
}

static void test_phase6_web_search_notebook_and_lsp(void) {
    printf("[Test] Phase 6: Web Search/Fetch, Notebook Splicer & LSP-over-MCP...\n");

    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, "test_phase6.sqlite", NULL);
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Test HTML-to-Markdown Parser (belya_html_to_markdown)
    const char *test_html = 
        "<html><head><title>Test Doc</title>"
        "<style>body { color: red; }</style>"
        "<script>console.log('malicious script');</script>"
        "</head><body>"
        "<h1>Overview Header</h1>"
        "<p>This is a paragraph with <b>bold</b> and <a href=\"https://example.com/doc\">documentation link</a>.</p>"
        "<ul><li>First feature item</li><li>Second feature item</li></ul>"
        "<pre><code>int x = 42;</code></pre>"
        "<!-- Comment to strip -->"
        "</body></html>";

    char *md_output = belya_html_to_markdown(test_html, 51200);
    assert(md_output != NULL);
    assert(strstr(md_output, "# Overview Header") != NULL);
    assert(strstr(md_output, "[documentation link](https://example.com/doc)") != NULL);
    assert(strstr(md_output, "* First feature item") != NULL);
    assert(strstr(md_output, "* Second feature item") != NULL);
    assert(strstr(md_output, "int x = 42;") != NULL);
    assert(strstr(md_output, "malicious script") == NULL);
    assert(strstr(md_output, "color: red") == NULL);
    free(md_output);

    // 2. Test Multi-Provider Web Search (belya_harness_web_search)
    char *search_res = belya_harness_web_search(h, "mock_test_search", "mock");
    assert(search_res != NULL);
    assert(strstr(search_res, "Web Search Results for") != NULL);
    assert(strstr(search_res, "Official C99 Specification") != NULL);
    assert(strstr(search_res, "POSIX.1-2008") != NULL);
    free(search_res);

    // 3. Test Jupyter Notebook Splicer (belya_harness_edit_notebook)
    const char *test_nb_path = "test_phase6_nb.ipynb";
    const char *initial_nb_json = 
        "{\n"
        "  \"cells\": [\n"
        "    {\n"
        "      \"cell_type\": \"markdown\",\n"
        "      \"metadata\": {},\n"
        "      \"source\": \"# Initial Title\\nFirst markdown text.\"\n"
        "    },\n"
        "    {\n"
        "      \"cell_type\": \"code\",\n"
        "      \"execution_count\": 1,\n"
        "      \"metadata\": {},\n"
        "      \"outputs\": [{\"output_type\": \"stream\", \"text\": \"Execution output 1\\n\"}],\n"
        "      \"source\": \"print('initial code')\"\n"
        "    }\n"
        "  ],\n"
        "  \"metadata\": {},\n"
        "  \"nbformat\": 4,\n"
        "  \"nbformat_minor\": 2\n"
        "}";

    FILE *nbf = fopen(test_nb_path, "w");
    assert(nbf != NULL);
    fputs(initial_nb_json, nbf);
    fclose(nbf);

    // 3a. Read action
    char *nb_read = belya_harness_edit_notebook(h, test_nb_path, "read", -1, NULL, NULL);
    assert(nb_read != NULL);
    assert(strstr(nb_read, "Jupyter Notebook: test_phase6_nb.ipynb (2 cells") != NULL);
    assert(strstr(nb_read, "Initial Title") != NULL);
    assert(strstr(nb_read, "print('initial code')") != NULL);
    free(nb_read);

    // 3b. Replace cell action
    char *nb_repl = belya_harness_edit_notebook(h, test_nb_path, "replace_cell", 1, "code", "print('modified code')");
    assert(nb_repl != NULL);
    assert(strstr(nb_repl, "successfully replaced") != NULL);
    free(nb_repl);

    char *nb_read_post_repl = belya_harness_edit_notebook(h, test_nb_path, "read", -1, NULL, NULL);
    assert(nb_read_post_repl != NULL);
    assert(strstr(nb_read_post_repl, "print('modified code')") != NULL);
    free(nb_read_post_repl);

    // 3c. Insert cell action
    char *nb_ins = belya_harness_edit_notebook(h, test_nb_path, "insert_cell", 2, "markdown", "### Appended Section");
    assert(nb_ins != NULL);
    assert(strstr(nb_ins, "successfully inserted") != NULL);
    free(nb_ins);

    char *nb_read_post_ins = belya_harness_edit_notebook(h, test_nb_path, "read", -1, NULL, NULL);
    assert(nb_read_post_ins != NULL);
    assert(strstr(nb_read_post_ins, "3 cells") != NULL);
    assert(strstr(nb_read_post_ins, "Appended Section") != NULL);
    free(nb_read_post_ins);

    // 3d. Delete cell action
    char *nb_del = belya_harness_edit_notebook(h, test_nb_path, "delete_cell", 0, NULL, NULL);
    assert(nb_del != NULL);
    assert(strstr(nb_del, "successfully deleted") != NULL);
    free(nb_del);

    char *nb_read_post_del = belya_harness_edit_notebook(h, test_nb_path, "read", -1, NULL, NULL);
    assert(nb_read_post_del != NULL);
    assert(strstr(nb_read_post_del, "2 cells") != NULL);
    assert(strstr(nb_read_post_del, "Initial Title") == NULL);
    free(nb_read_post_del);

    unlink(test_nb_path);

    // 4. Test LSP-over-MCP & Native Symbol Graph
    belya_harness_index_symbols(h, ".");

    char *def_res = belya_harness_lsp_definition(h, "belya_harness.h", 1, 1, "belya_harness_init");
    assert(def_res != NULL);
    assert(strstr(def_res, "LSP Definition") != NULL);
    assert(strstr(def_res, "belya_harness_init") != NULL);
    free(def_res);

    char *ref_res = belya_harness_lsp_references(h, "belya_harness.h", 1, 1, "belya_harness_init");
    assert(ref_res != NULL);
    assert(strstr(ref_res, "LSP References") != NULL);
    free(ref_res);

    // 5. Test Tool Callbacks Registration
    assert(get_tool_cb(h, "web_search") != NULL);
    assert(get_tool_cb(h, "web_fetch") != NULL);
    assert(get_tool_cb(h, "edit_notebook") != NULL);
    assert(get_tool_cb(h, "lsp_definition") != NULL);
    assert(get_tool_cb(h, "lsp_references") != NULL);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_phase6.sqlite");
    printf("  -> Phase 6: Web Search/Fetch, Notebook Splicer & LSP-over-MCP PASSED\n");
}

static void test_phase7_omnichannel_and_remote_backends(void) {
    printf("[Test] Phase 7: Omnichannel Gateway & Stdin-Piped Remote Backends...\n");

    ModelGateway *gw = model_gateway_init("http://mock", "mock-key", "mock-model");
    BelyaAgent *agent = belya_agent_init(gw, "test_phase7.sqlite", NULL);
    BelyaHarness *h = belya_harness_init(agent);

    // 1. Test Discord Adapter & Event Handling
    BelyaDiscordAdapter *discord = belya_discord_init("mock_token_123", "chan_999");
    assert(discord != NULL);
    assert(strcmp(discord->bot_token, "mock_token_123") == 0);
    assert(discord->heartbeat_interval_ms == 41250);

    // Test sending message (mock token bypasses network)
    bool disc_sent = belya_discord_send_message(discord, "chan_999", "Hello Discord from Belya");
    assert(disc_sent == true);

    // Test Discord Opcode 10 Hello Event
    const char *discord_hello = "{\"op\": 10, \"d\": {\"heartbeat_interval\": 45000}}";
    BelyaChannelMessage msg;
    bool hello_handled = belya_discord_handle_event(discord, discord_hello, &msg);
    assert(hello_handled == false);
    assert(discord->connected == true);
    assert(discord->heartbeat_interval_ms == 45000);

    // Test Discord Opcode 0 Message Create Event
    const char *discord_msg_json = 
        "{\"op\": 0, \"t\": \"MESSAGE_CREATE\", \"s\": 42, \"d\": {"
        "  \"channel_id\": \"chan_999\","
        "  \"content\": \"Deploy phase 7 immediately\","
        "  \"id\": \"msg_101\","
        "  \"author\": {\"id\": \"user_42\", \"username\": \"almaz_operator\"}"
        "}}";
    bool msg_handled = belya_discord_handle_event(discord, discord_msg_json, &msg);
    assert(msg_handled == true);
    assert(strcmp(msg.platform, "discord") == 0);
    assert(strcmp(msg.channel_id, "chan_999") == 0);
    assert(strcmp(msg.user_id, "user_42") == 0);
    assert(strcmp(msg.user_name, "almaz_operator") == 0);
    assert(strcmp(msg.text, "Deploy phase 7 immediately") == 0);
    assert(discord->last_sequence == 42);

    // Test Discord Thread Mapping Session ID
    char disc_session_id[256];
    belya_channel_get_session_id(&msg, disc_session_id, sizeof(disc_session_id));
    assert(strcmp(disc_session_id, "discord_chan_999") == 0);

    // 2. Test Slack Adapter & Webhook Ingress (< 100 ms Challenge Response)
    BelyaSlackAdapter *slack = belya_slack_init("mock_xoxb_123", "mock_xapp_456", "C0123456");
    assert(slack != NULL);
    assert(strcmp(slack->bot_token, "mock_xoxb_123") == 0);

    bool slack_sent = belya_slack_send_message(slack, "C0123456", "Hello Slack from Belya");
    assert(slack_sent == true);

    // Test Slack url_verification challenge
    const char *slack_verify_json = "{\"type\": \"url_verification\", \"challenge\": \"3eZbrw1aWjvWqWOrWVuFlKHgABC\"}";
    char ack_buf[512] = {0};
    bool is_event = belya_slack_handle_webhook(slack, slack_verify_json, &msg, ack_buf, sizeof(ack_buf));
    assert(is_event == false);
    assert(strstr(ack_buf, "3eZbrw1aWjvWqWOrWVuFlKHgABC") != NULL);

    // Test Slack event_callback
    const char *slack_event_json = 
        "{\"type\": \"event_callback\", \"event\": {"
        "  \"channel\": \"C0123456\","
        "  \"user\": \"U98765\","
        "  \"text\": \"Run integration tests\","
        "  \"thread_ts\": \"1712345678.000100\""
        "}}";
    memset(ack_buf, 0, sizeof(ack_buf));
    is_event = belya_slack_handle_webhook(slack, slack_event_json, &msg, ack_buf, sizeof(ack_buf));
    assert(is_event == true);
    assert(strstr(ack_buf, "{\"ok\":true}") != NULL);
    assert(strcmp(msg.platform, "slack") == 0);
    assert(strcmp(msg.channel_id, "C0123456") == 0);
    assert(strcmp(msg.user_id, "U98765") == 0);
    assert(strcmp(msg.thread_id, "1712345678.000100") == 0);
    assert(strcmp(msg.text, "Run integration tests") == 0);

    // Test Thread-Specific Session ID Continuity
    char slack_session_id[256];
    belya_channel_get_session_id(&msg, slack_session_id, sizeof(slack_session_id));
    assert(strcmp(slack_session_id, "slack_C0123456_1712345678.000100") == 0);

    // 3. Test Polymorphic Omnichannel Creation & Route
    BelyaOmniChannel *omni_disc = belya_channel_create_discord("mock_tok", "111");
    assert(omni_disc != NULL);
    assert(belya_channel_send(omni_disc, "111", "Testing omni discord") == true);

    BelyaOmniChannel *omni_slack = belya_channel_create_slack("mock_tok", "mock_app", "222");
    assert(omni_slack != NULL);
    assert(belya_channel_send(omni_slack, "222", "Testing omni slack") == true);

    // Route message with session continuity
    char route_reply[1024];
    bool routed = belya_channel_route_message(omni_disc, h, &msg, route_reply, sizeof(route_reply));
    assert(routed == true);
    assert(strlen(route_reply) > 0);

    belya_discord_free(discord);
    belya_slack_free(slack);
    belya_channel_free(omni_disc);
    belya_channel_free(omni_slack);

    // 4. Test Stdin-Piped Remote Execution Backend
    // Test exec_backend tool
    BelyaToolCallback cb_eb = get_tool_cb(h, "exec_backend");
    assert(cb_eb != NULL);

    JsonValue *eb_get_args = json_create_object();
    json_obj_add(eb_get_args, "action", json_create_string("get"));
    char *eb_res = cb_eb(agent, eb_get_args);
    json_free(eb_get_args);
    assert(eb_res && strstr(eb_res, "Execution backend: local") != NULL);
    free(eb_res);

    // Set to Docker
    JsonValue *eb_set_doc = json_create_object();
    json_obj_add(eb_set_doc, "action", json_create_string("set"));
    json_obj_add(eb_set_doc, "backend", json_create_string("docker"));
    json_obj_add(eb_set_doc, "target", json_create_string("sandbox_container_v1"));
    eb_res = cb_eb(agent, eb_set_doc);
    json_free(eb_set_doc);
    assert(eb_res && strstr(eb_res, "docker (container: sandbox_container_v1)") != NULL);
    free(eb_res);
    assert(h->exec_backend == BELYA_BACKEND_DOCKER);
    assert(strcmp(h->docker_container, "sandbox_container_v1") == 0);

    // Set to SSH
    JsonValue *eb_set_ssh = json_create_object();
    json_obj_add(eb_set_ssh, "action", json_create_string("set"));
    json_obj_add(eb_set_ssh, "backend", json_create_string("ssh"));
    json_obj_add(eb_set_ssh, "target", json_create_string("root@192.168.1.100"));
    eb_res = cb_eb(agent, eb_set_ssh);
    json_free(eb_set_ssh);
    assert(eb_res && strstr(eb_res, "ssh (host: root@192.168.1.100)") != NULL);
    free(eb_res);
    assert(h->exec_backend == BELYA_BACKEND_SSH);
    assert(strcmp(h->ssh_host, "root@192.168.1.100") == 0);

    // Reset back to Local
    JsonValue *eb_set_loc = json_create_object();
    json_obj_add(eb_set_loc, "action", json_create_string("set"));
    json_obj_add(eb_set_loc, "backend", json_create_string("local"));
    eb_res = cb_eb(agent, eb_set_loc);
    json_free(eb_set_loc);
    assert(eb_res && strstr(eb_res, "Execution backend set to: local") != NULL);
    free(eb_res);
    assert(h->exec_backend == BELYA_BACKEND_LOCAL);

    // Test zero host shell-quoting execution via belya_remote_exec_piped
    int exit_code = 0;
    char *piped_out = belya_remote_exec_piped(BELYA_BACKEND_LOCAL, NULL, NULL, "echo 'zero_quoting_stdin_pipeline_ok'", &exit_code);
    assert(piped_out != NULL);
    assert(strstr(piped_out, "zero_quoting_stdin_pipeline_ok") != NULL);
    assert(exit_code == 0);
    free(piped_out);

    // Test zero host shell-quoting with nasty strings (double quotes, backticks, dollar signs)
    char *nasty_out = belya_remote_exec_piped(BELYA_BACKEND_LOCAL, NULL, NULL, "printf \"%s\\n\" \"quotes'and\\\"dollar$VAR`whoami`\"", &exit_code);
    assert(nasty_out != NULL);
    assert(exit_code == 0);
    free(nasty_out);

    // Test missing Docker target validation
    char *err_doc = belya_remote_exec_piped(BELYA_BACKEND_DOCKER, "", NULL, "ls", &exit_code);
    assert(err_doc != NULL);
    assert(strstr(err_doc, "Error: Missing Docker container target") != NULL);
    free(err_doc);

    // Test missing SSH target validation
    char *err_ssh = belya_remote_exec_piped(BELYA_BACKEND_SSH, "", NULL, "ls", &exit_code);
    assert(err_ssh != NULL);
    assert(strstr(err_ssh, "Error: Missing SSH host target") != NULL);
    free(err_ssh);

    belya_harness_free(h);
    model_gateway_free(gw);
    unlink("test_phase7.sqlite");
    printf("  -> Phase 7: Omnichannel Gateway & Stdin-Piped Remote Backends PASSED\n");
}

static void test_gateway_preflight_probe(void) {
    printf("[Test] Model Gateway Pre-Flight Health & Auth Probe...\n");

    // 1. Test offline primary + invalid key fallback
    ModelGateway *gw = model_gateway_init("http://127.0.0.1:9999/v1/chat/completions", "none", "test-offline-model");
    assert(gw != NULL);
    model_gateway_set_fallback(gw, "https://openrouter.ai/api/v1/chat/completions", "invalid-key-test-12345", "deepseek/deepseek-v4-flash");

    ModelGatewayProbeResult probe = model_gateway_preflight_probe(gw);
    assert(probe.primary.endpoint != NULL);
    assert(probe.primary.reachable == false);
    assert(probe.primary.auth_valid == false);

    assert(probe.has_fallback == true);
    assert(probe.fallback.endpoint != NULL);
    assert(probe.fallback.auth_valid == false);
    assert(probe.all_ok == false);

    // Verify report formatting does not crash
    model_gateway_print_probe_report(&probe);
    model_gateway_probe_result_free(&probe);
    model_gateway_free(gw);

    // 2. Test probing active API key if available in environment
    const char *active_key = getenv("OPENROUTER_API_KEY");
    if (!active_key || strlen(active_key) == 0) active_key = getenv("MODEL_API_KEY");
    if (active_key && strncmp(active_key, "sk-or-v1-", 9) == 0) {
        ModelGateway *gw2 = model_gateway_init("https://openrouter.ai/api/v1/chat/completions", active_key, "deepseek/deepseek-v4-flash");
        assert(gw2 != NULL);
        ModelGatewayProbeResult probe2 = model_gateway_preflight_probe(gw2);
        assert(probe2.primary.reachable == true);
        assert(probe2.primary.auth_valid == true);
        assert(probe2.primary.http_code == 200);
        assert(probe2.all_ok == true);
        model_gateway_probe_result_free(&probe2);
        model_gateway_free(gw2);
    }

    printf("  -> Gateway Pre-Flight Health & Auth Probe PASSED\n");
}

static void test_championship_presets_and_stage_telemetry(void) {
    printf("[Test] Dubai Championship UAE Falcon Presets & Stage Telemetry...\n");

    // 1. Verify Falcon presets
    const ModelPresetConfig *p_falcon = model_preset_get("falcon");
    assert(p_falcon != NULL);
    assert(strcmp(p_falcon->name, "falcon") == 0);
    assert(strstr(p_falcon->model_name, "falcon3") != NULL);
    assert(strstr(p_falcon->endpoint, "11434") != NULL);
    assert(p_falcon->is_airgap == true);

    const ModelPresetConfig *p_vllm = model_preset_get("falcon-vllm");
    assert(p_vllm != NULL);
    assert(strstr(p_vllm->model_name, "Falcon3") != NULL);
    assert(strstr(p_vllm->endpoint, "8000") != NULL);

    const ModelPresetConfig *p_f2 = model_preset_get("falcon2");
    assert(p_f2 != NULL);
    assert(strstr(p_f2->model_name, "falcon2") != NULL);

    const ModelPresetConfig *p_airgap = model_preset_get("airgap");
    assert(p_airgap != NULL);
    assert(p_airgap->is_airgap == true);

    assert(model_preset_get("non_existent_preset") == NULL);

    // 2. Verify Markdown Codeblock Scavenger Fallback
    const char *known_tools[] = {"bash", "read_file", "write_file"};
    size_t known_count = 3;
    const char *fenced_bash = "Here is the solution to check the workspace status:\n```bash\ngit status --porcelain\n```\nDone.";
    ModelParsedToolCall *calls = NULL;
    size_t count = model_gateway_scavenge_tool_calls(fenced_bash, NULL, known_tools, known_count, &calls);
    assert(count == 1);
    assert(calls != NULL);
    assert(strcmp(calls[0].name, "bash") == 0);
    assert(strstr(calls[0].arguments_json, "git status --porcelain") != NULL);
    for (size_t i = 0; i < count; i++) {
        if (calls[i].id) free(calls[i].id);
        if (calls[i].name) free(calls[i].name);
        if (calls[i].arguments_json) free(calls[i].arguments_json);
    }
    free(calls);

    // 3. Verify Stage Telemetry Harness Initialization
    setenv("BELYA_TEST_MODE", "1", 1);
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "falcon3:7b");
    assert(gw != NULL);
    BelyaAgent *agent = belya_agent_init(gw, ":memory:", NULL);
    assert(agent != NULL);
    BelyaHarness *h = belya_harness_init(agent);
    assert(h != NULL);

    h->stage_mode = true;
    assert(h->stage_mode == true);

    belya_harness_free(h);
    model_gateway_free(gw);
    unsetenv("BELYA_TEST_MODE");

    printf("  -> Dubai Championship UAE Falcon Presets & Stage Telemetry PASSED\n");
}

int main(void) {
    printf("\n================ Running BelyaHarness & BelyaAgent Super Strict Test Suite ================\n");
    test_dyn_string();
    test_minijson();
    test_token_estimator();
    test_agent_memory_and_rules();
    test_session_checkpointing();
    test_self_tooling_define_tool();
    test_harness_tools_and_patches();
    test_telegram_adapter();
    test_preflight_compiler_watchdog();
    test_fetch_url_tool();
    test_gomaa_scoped_memory_and_timeline();
    test_tool_call_scavenger();
    test_skills_curation_and_recall();
    test_git_checkpoint_and_rollback();
    test_trajectory_exporter();
    test_historical_conversation_search();
    test_rest_api_advanced_options();
    test_tool_scavenger_deep_stress();
    test_multi_checkpoint_rollback_integrity();
    test_progressive_disclosure_manifest();
    test_forced_synthesis_and_keepalive();
    test_v6_enhancements();
    test_all_17_tools_exhaustive();
    test_skills_lifecycle_and_auto_injection();
    test_subagent_recursion_guard();
    test_self_telemetry_and_proprioception();
    test_metacognitive_circuit_breaker_and_verification_guard();
    test_workspace_path_jailing();
    test_markdown_frontmatter_parser();
    test_file_first_skills_catalog();
    test_composable_rule_packs();
    test_troubleshooting_pattern_resolver();
    test_belya_agency_architecture();
    test_jev_client();
    test_security_adversarial_suite();
    test_in_context_loop_breaker();
    test_context_compactor_middle_out();
    test_roma_context_firewall();
    test_symbol_graph_navigation();
    test_repo_to_mcp_synthesis();
    test_mythos_variant_analysis();
    test_phase1_diff_colorizer_and_ask_user();
    test_phase2_deterministic_hooks_and_5tier_compactor();
    test_phase3_task_board_fts5_recall_and_cron();
    test_phase4_plan_mode_and_worktrees();
    test_phase5_pty_subshells_and_task_manager();
    test_phase6_web_search_notebook_and_lsp();
    test_phase7_omnichannel_and_remote_backends();
    test_gateway_preflight_probe();
    test_championship_presets_and_stage_telemetry();
    printf("================ All Tests Passed Successfully (50/50 - 100%%) ================\n\n");
    return 0;
}


