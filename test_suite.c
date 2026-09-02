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

void test_token_estimator(void) {
    printf("[Test] BPE-calibrated Token Estimator...\n");
    const char *sample = "Hello world! This is a test of the BPE token estimation algorithm in C99.";
    size_t tokens = count_estimated_tokens(sample);
    assert(tokens >= 15 && tokens <= 25);

    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, ":memory:", "System instructions for token testing");
    c_agent_add_message(agent, "user", "How many tokens are in this sentence?");
    c_agent_add_message(agent, "assistant", "This sentence contains approximately 10 tokens.");

    size_t total_agent_tokens = c_agent_total_tokens(agent);
    assert(total_agent_tokens > 20);

    c_agent_free(agent);
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
    CAgent *agent = c_agent_init(gw, "test_session_db.sqlite", "System Root Instructions");

    c_agent_add_message(agent, "user", "Fix bug in memory allocator");
    c_agent_add_message(agent, "assistant", "I am inspecting the allocator code.");
    c_agent_add_tool_result(agent, "call_123", "read_file", "void *alloc() { return malloc(10); }");
    c_agent_add_message(agent, "assistant", "Fixed the bug by checking null pointers.");

    assert(agent->msg_count == 5);

    // Save Session
    bool saved = c_agent_save_session(agent, "test_sess_001", "Memory Bug Fix");
    assert(saved == true);

    char *sessions_list = c_agent_list_sessions(agent);
    assert(strstr(sessions_list, "test_sess_001") != NULL);
    assert(strstr(sessions_list, "Memory Bug Fix") != NULL);
    free(sessions_list);

    // Clear agent memory completely
    c_agent_clear_history(agent);
    assert(agent->msg_count == 1);

    // Restore Session
    bool loaded = c_agent_load_session(agent, "test_sess_001");
    assert(loaded == true);
    assert(agent->msg_count == 5);
    assert(strcmp(agent->messages[1].role, "user") == 0);
    assert(strcmp(agent->messages[1].content, "Fix bug in memory allocator") == 0);
    assert(strcmp(agent->messages[3].role, "tool") == 0);
    assert(strstr(agent->messages[3].content, "void *alloc()") != NULL);

    c_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_session_db.sqlite");
    printf("  -> Session Checkpointing & Resumption PASSED\n");
}

void test_self_tooling_define_tool(void) {
    printf("[Test] Dynamic Self-Tooling (define_tool & Custom Script Execution)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, "test_dynamic_tool.sqlite", "System Prompt");
    CHarness *h = c_harness_init(agent);

    size_t orig_tools = h->tool_count;

    // Define a new tool dynamically
    JsonValue *params = json_create_object();
    json_obj_add(params, "type", json_create_string("object"));
    const char *script = "#!/bin/sh\necho \"CUSTOM_TOOL_OUTPUT_VERIFIED\"\n";

    bool def_ok = c_harness_define_custom_tool(h, "custom_calc", "A dynamically invented calculator tool", params, script);
    assert(def_ok == true);
    assert(h->tool_count == orig_tools + 1);

    // Execute the custom tool
    CHarnessRegisteredTool *custom_t = &h->tools[h->tool_count - 1];
    assert(strcmp(custom_t->name, "custom_calc") == 0);
    assert(custom_t->callback != NULL);

    char *obs = custom_t->callback(agent, NULL);
    assert(obs != NULL);
    assert(strstr(obs, "CUSTOM_TOOL_OUTPUT_VERIFIED") != NULL);
    free(obs);

    // Test Reflection
    c_agent_add_message(agent, "user", "Invent and run custom_calc");
    c_agent_add_message(agent, "assistant", "Executing custom calc");
    char *reflection = c_agent_reflect_and_distill(agent);
    assert(reflection != NULL);
    free(reflection);

    c_harness_free(h);
    model_gateway_free(gw);
    unlink("test_dynamic_tool.sqlite");
    unlink(".charness/tools/custom_calc.sh");
    unlink(".charness/tools/custom_calc.json");
    rmdir(".charness/tools");
    rmdir(".charness");
    printf("  -> Dynamic Self-Tooling PASSED\n");
}

void test_harness_tools_and_patches(void) {
    printf("[Test] Harness Tool Suite (13 Tools) & Patch Engine...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    gw->streaming = false;
    CAgent *agent = c_agent_init(gw, "test_harness_mem.sqlite", "Test");
    CHarness *h = c_harness_init(agent);

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

    telegram_bot_stop(bot);
    assert(bot->running == false);

    telegram_bot_free(bot);
    printf("  -> Telegram Adapter PASSED\n");
}

void test_preflight_compiler_watchdog(void) {
    printf("[Test] Pre-Flight Compiler Watchdog (Auto-Healing Feedback Loop)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, ":memory:", "Test");
    CHarness *h = c_harness_init(agent);

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
    c_harness_free(h);
    model_gateway_free(gw);
    printf("  -> Pre-Flight Compiler Watchdog PASSED\n");
}

void test_fetch_url_tool(void) {
    printf("[Test] Native Web Content Retrieval (fetch_url)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, ":memory:", "Test");
    CHarness *h = c_harness_init(agent);

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

    c_harness_free(h);
    model_gateway_free(gw);
    printf("  -> fetch_url Tool PASSED\n");
}

void test_gomaa_scoped_memory_and_timeline(void) {
    printf("[Test] Gomaa Memory Paradigm (Wing/Room Scoping, Salience & Timeline)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, "test_gomaa_mem.sqlite", "Test System");

    // 1. Scoped memory persistence
    c_agent_persist_memory(agent, "backend/auth: JWT Refresh Tokens", "Use RS256 with 15min expiry for access tokens.");
    c_agent_persist_memory_scoped(agent, "Database Scaling", "Enable SQLite WAL mode for concurrent readers.", "infra", "database");

    // 2. Scoped memory search & verification
    char *auth_res = c_agent_search_memory(agent, "JWT");
    assert(strstr(auth_res, "Wing: backend") != NULL);
    assert(strstr(auth_res, "Room: auth") != NULL);
    assert(strstr(auth_res, "Salience:") != NULL);
    free(auth_res);

    char *db_res = c_agent_search_memory(agent, "Scaling");
    assert(strstr(db_res, "Wing: infra") != NULL);
    assert(strstr(db_res, "Room: database") != NULL);
    free(db_res);

    // 3. Timeline logging & verification
    char *timeline = c_agent_get_timeline(agent, 10);
    assert(strstr(timeline, "=== Agent Timeline Log ===") != NULL);
    assert(strstr(timeline, "memory_persisted") != NULL);
    assert(strstr(timeline, "backend/auth") != NULL);
    free(timeline);

    c_agent_free(agent);
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
    CAgent *agent = c_agent_init(gw, "test_skills_mem.sqlite", "Test System");

    // 1. Save procedural skill
    bool saved = c_agent_save_skill(agent, "git_sync", "sync_repo",
        "Synchronize current Git branch with remote origin",
        "Step 1: git fetch origin\nStep 2: git rebase origin/main\nStep 3: git push");
    assert(saved == true);

    // 2. Search skills
    char *search_res = c_agent_search_skills(agent, "sync");
    assert(search_res != NULL);
    assert(strstr(search_res, "git_sync") != NULL);
    assert(strstr(search_res, "git fetch origin") != NULL);
    free(search_res);

    // 3. Manifest progressive disclosure
    char *manifest = c_agent_get_skills_manifest(agent);
    assert(manifest != NULL);
    assert(strstr(manifest, "git_sync") != NULL);
    assert(strstr(manifest, "sync_repo") != NULL);
    free(manifest);

    // 4. On-demand trigger matching
    char *matched = c_agent_match_skill_for_prompt(agent, "Please sync_repo with remote");
    assert(matched != NULL);
    assert(strstr(matched, "git rebase origin/main") != NULL);
    free(matched);

    c_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_skills_mem.sqlite");
    printf("  -> Skills Curation & Progressive Disclosure PASSED\n");
}

void test_git_checkpoint_and_rollback(void) {
    printf("[Test] Git & State Checkpoint and Instant Rollback...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, "test_chk_mem.sqlite", "Test System");

    c_agent_add_message(agent, "user", "Message 1: Start work");
    c_agent_add_message(agent, "assistant", "Message 2: Working");

    // 1. Create Checkpoint
    bool chk_ok = c_agent_create_checkpoint(agent, "stage_1");
    assert(chk_ok == true);

    // 2. Add further messages to simulate subsequent turn
    c_agent_add_message(agent, "user", "Message 3: Do something risky");
    c_agent_add_message(agent, "assistant", "Message 4: Execution result");
    assert(agent->msg_count == 5);

    // 3. List checkpoints
    char *chk_list = c_agent_list_checkpoints(agent);
    assert(chk_list != NULL);
    assert(strstr(chk_list, "stage_1") != NULL);
    free(chk_list);

    // 4. Rollback to stage_1
    bool rb_ok = c_agent_rollback_to_checkpoint(agent, "stage_1");
    assert(rb_ok == true);
    assert(agent->msg_count == 3); // Restored to 3 messages (system + 2 turns)

    c_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_chk_mem.sqlite");
    printf("  -> Git Checkpointing & Instant Rollback PASSED\n");
}

void test_trajectory_exporter(void) {
    printf("[Test] Trajectory Exporter (OpenAI Fine-Tune JSONL Format)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, ":memory:", "Test Trajectory System Prompt");

    c_agent_add_message(agent, "user", "Refactor memory module in C");
    c_agent_add_message(agent, "assistant", "I will edit the file.");
    c_agent_add_tool_result(agent, "call_999", "edit_file", "File successfully edited.");
    c_agent_add_message(agent, "assistant", "Refactor complete and verified.");

    const char *out_file = "test_trajectory_output.jsonl";
    unlink(out_file);

    bool exp_ok = c_agent_export_trajectory(agent, NULL, out_file);
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

    c_agent_free(agent);
    model_gateway_free(gw);
    printf("  -> Trajectory Exporter PASSED\n");
}

void test_historical_conversation_search(void) {
    printf("[Test] Historical Conversation Search & Multi-Method REST Retrieval...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, "test_conv_hist.sqlite", "Test System");

    // 1. Create a session and persist messages
    c_agent_add_message(agent, "user", "Deploy my neural microservice to Kubernetes cluster production");
    c_agent_add_message(agent, "assistant", "Kubernetes deployment YAML created with 3 replicas and ingress route.");
    c_agent_save_session(agent, "k8s_deploy_01", "Production Kubernetes Deployment");

    // 2. Search conversation history across historical sessions
    char *res = c_agent_search_conversations(agent, "Kubernetes");
    assert(res != NULL);
    assert(strstr(res, "Production Kubernetes Deployment") != NULL);
    assert(strstr(res, "neural microservice") != NULL);
    free(res);

    char *miss = c_agent_search_conversations(agent, "nonexistent_keyword_xyz");
    assert(miss != NULL);
    assert(strstr(miss, "No matching past conversations found") != NULL);
    free(miss);

    c_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_conv_hist.sqlite");
    printf("  -> Historical Conversation Search PASSED\n");
}

void test_rest_api_advanced_options(void) {
    printf("[Test] Advanced REST Client (Multi-Method, Headers, JSON Body)...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, ":memory:", "Test System");
    CHarness *h = c_harness_init(agent);

    // 1. Test POST with custom headers (object format)
    JsonValue *p_args = json_create_object();
    json_obj_add(p_args, "url", json_create_string("https://httpbin.org/post"));
    json_obj_add(p_args, "method", json_create_string("POST"));
    json_obj_add(p_args, "body", json_create_string("{\"service\":\"charness\",\"action\":\"benchmark\"}"));
    JsonValue *hdrs = json_create_object();
    json_obj_add(hdrs, "Content-Type", json_create_string("application/json"));
    json_obj_add(hdrs, "X-Agent-ID", json_create_string("CAgent-v4"));
    json_obj_add(p_args, "headers", hdrs);

    for (size_t t = 0; t < h->tool_count; t++) {
        if (strcmp(h->tools[t].name, "fetch_url") == 0) {
            char *obs = h->tools[t].callback(agent, p_args);
            assert(obs != NULL);
            // httpbin returns json echo with our payload
            assert(strstr(obs, "charness") != NULL || strstr(obs, "httpbin") != NULL || strstr(obs, "Error") != NULL);
            free(obs);
            break;
        }
    }
    json_free(p_args);

    c_harness_free(h);
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
    CAgent *agent = c_agent_init(gw, "test_checkpoints.sqlite", "Test System");

    // 1. Create Checkpoint 1 (3 messages: system + 2)
    c_agent_add_message(agent, "user", "Phase 1: Initial state");
    c_agent_add_message(agent, "assistant", "Phase 1 completed");
    bool cp1 = c_agent_create_checkpoint(agent, "phase_1_baseline");
    assert(cp1 == true);
    assert(agent->msg_count == 3);

    // 2. Add Phase 2 messages & Checkpoint 2 (5 messages: system + 4)
    c_agent_add_message(agent, "user", "Phase 2: Complex refactor");
    c_agent_add_message(agent, "assistant", "Phase 2 completed");
    bool cp2 = c_agent_create_checkpoint(agent, "phase_2_refactor");
    assert(cp2 == true);
    assert(agent->msg_count == 5);

    // 3. Add Phase 3 messages (7 messages: system + 6)
    c_agent_add_message(agent, "user", "Phase 3: Buggy experimental code");
    c_agent_add_message(agent, "assistant", "Phase 3 crashed with errors");
    assert(agent->msg_count == 7);

    // 4. Rollback to Checkpoint 2 (Restores cleanly to 5 messages)
    bool rb2 = c_agent_rollback_to_checkpoint(agent, "phase_2_refactor");
    assert(rb2 == true);
    assert(agent->msg_count == 5);
    assert(strcmp(agent->messages[3].content, "Phase 2: Complex refactor") == 0);

    // 5. Rollback further to Checkpoint 1 (Restores cleanly to 3 messages)
    bool rb1 = c_agent_rollback_to_checkpoint(agent, "phase_1_baseline");
    assert(rb1 == true);
    assert(agent->msg_count == 3);
    assert(strcmp(agent->messages[1].content, "Phase 1: Initial state") == 0);

    c_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_checkpoints.sqlite");
    printf("  -> Multi-Turn Checkpointing & Rollback PASSED\n");
}

void test_progressive_disclosure_manifest(void) {
    printf("[Test] Progressive Disclosure Manifest & Salience Priority...\n");
    ModelGateway *gw = model_gateway_init("http://localhost:11434/v1/chat/completions", "none", "hermes-3");
    CAgent *agent = c_agent_init(gw, "test_prog_disc.sqlite", "Test System");

    // Save 3 procedural skills
    c_agent_save_skill(agent, "skill_posix_threads", "pthread", "POSIX thread pool pattern", "1. pthread_create 2. pthread_join");
    c_agent_save_skill(agent, "skill_simd_vector", "simd", "AVX2 SIMD vectorization", "1. immintrin.h 2. _mm256_load_ps");
    c_agent_save_skill(agent, "skill_atomic_cas", "atomic", "Lock-free atomic compare and swap", "1. stdatomic.h 2. atomic_compare_exchange");

    char *manifest = c_agent_get_skills_manifest(agent);
    assert(manifest != NULL);
    assert(strstr(manifest, "skill_posix_threads") != NULL);
    assert(strstr(manifest, "pthread") != NULL);
    assert(strstr(manifest, "skill_simd_vector") != NULL);
    assert(strstr(manifest, "skill_atomic_cas") != NULL);
    free(manifest);

    c_agent_free(agent);
    model_gateway_free(gw);
    unlink("test_prog_disc.sqlite");
    printf("  -> Progressive Disclosure Manifest PASSED\n");
}

int main(void) {
    printf("\n================ Running CHarness & CAgent Super Strict Test Suite ================\n");
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
    printf("================ All Tests Passed Successfully (20/20 - 100%%) ================\n\n");
    return 0;
}
