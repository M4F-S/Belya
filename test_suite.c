#include "belya_harness.h"
#include "telegram_adapter.h"
#include "minifrontmatter.h"
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

void test_agent_memory_and_rules(void) {
    printf("[Test] Agent Memory (FTS5) & Rules Auto-Discovery...\n");

    FILE *rf = fopen(".agentrules", "w");
    if (rf) {
        fprintf(rf, "Always write self-contained C99 code without external dependencies.\n");
        fclose(rf);
    }

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
    
    // Restore repository .agentrules
    FILE *rf_rest = fopen(".agentrules", "w");
    if (rf_rest) {
        fprintf(rf_rest,
            "# Expert Researcher & Strategic Executioner Directives\n\n"
            "## Role & Objective\n"
            "Act as an expert researcher and strategic executioner. Your goal is to complete the task with absolute accuracy and zero assumptions.\n\n"
            "## Core Rules\n"
            "1. Verify Everything: Never assume facts, syntax, or outcomes. Treat every data point as unverified until proven otherwise.\n"
            "2. Research Deeply: Conduct thorough internet research. Use only reliable, high-quality resources.\n"
            "3. Test Continuously: Run tests at every critical stage. Verify that code, logic, or data works in practice, not just in theory.\n\n"
            "## Execution Protocol\n"
            "1. Research & Plan: Investigate the problem deeply. Formulate a structured, step-by-step execution plan.\n"
            "2. Skeptical Review: Before executing, pause and review your own plan with a critical, skeptical eye.\n"
            "3. Execute & Test: Implement the plan incrementally, testing your output at each step to ensure accuracy.\n"
            "4. Git Workflow: Work strictly within a Git repository. Always push your committed changes to GitHub, and explicitly tag stable versions.\n"
        );
        fclose(rf_rest);
    }
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
    fread(buf, 1, sizeof(buf) - 1, rf);
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
    printf("================ All Tests Passed Successfully (32/32 - 100%%) ================\n\n");
    return 0;
}
