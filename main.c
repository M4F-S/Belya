#include "belya_harness.h"
#include "telegram_adapter.h"

static void load_env_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\r' || *p == '\n' || *p == '\0') continue;
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *key = p;
            char *val = eq + 1;
            char *end_key = key + strlen(key) - 1;
            while (end_key > key && (*end_key == ' ' || *end_key == '\t')) *end_key-- = '\0';
            while (*val == ' ' || *val == '\t') val++;
            char *end_val = val + strlen(val) - 1;
            while (end_val >= val && (*end_val == '\r' || *end_val == '\n' || *end_val == ' ' || *end_val == '\t')) *end_val-- = '\0';
            if (strlen(key) > 0 && strlen(val) > 0) {
                setenv(key, val, 0); // 0 means do not overwrite already set environment variables
            }
        }
    }
    fclose(fp);
}

int main(int argc, char **argv) {
    load_env_file(".env");

    // Unbuffered stdout for systemd journal visibility
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    bool telegram_mode = false;
    bool agency_mode = false;
    const char *resume_session_id = NULL;
    const char *headless_prompt = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: belya [OPTIONS]\n\n");
            printf("Options:\n");
            printf("  -h, --help                 Show this help message\n");
            printf("  -t, --telegram             Run as Telegram bot daemon\n");
            printf("  -a, --agency [prompt]      Execute task via Belya Agency multi-agent pipeline\n");
            printf("  -r, --resume <session_id>  Resume saved conversation session\n");
            printf("  -p, --prompt <prompt>      Execute headless mission prompt and exit\n");
            printf("  --headless <prompt>        Alias for --prompt\n");
            printf("  --eval <prompt>            Alias for --prompt\n\n");
            return 0;
        } else if (strcmp(argv[i], "--telegram") == 0 || strcmp(argv[i], "-t") == 0) {
            telegram_mode = true;
        } else if (strcmp(argv[i], "--agency") == 0 || strcmp(argv[i], "-a") == 0) {
            agency_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                headless_prompt = argv[++i];
            }
        } else if ((strcmp(argv[i], "--resume") == 0 || strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--session") == 0) && i + 1 < argc) {
            resume_session_id = argv[++i];
        } else if ((strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "--eval") == 0 || strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--prompt") == 0) && i + 1 < argc) {
            headless_prompt = argv[++i];
        }
    }

    const char *endpoint = getenv("MODEL_ENDPOINT");
    const char *api_key = getenv("MODEL_API_KEY");
    const char *model = getenv("MODEL_NAME");
    const char *timeout_env = getenv("MODEL_TIMEOUT");
    const char *retries_env = getenv("MODEL_MAX_RETRIES");

    const char *tg_token = getenv("TELEGRAM_BOT_TOKEN");
    const char *tg_chat_id = getenv("TELEGRAM_CHAT_ID");

    if (!endpoint) endpoint = "http://localhost:11434/v1/chat/completions";
    if (!api_key)  api_key = "none";
    if (!model)    model = "hermes-3";

    // 1. Initialize Model Gateway
    ModelGateway *gateway = model_gateway_init(endpoint, api_key, model);
    if (timeout_env && atoi(timeout_env) > 0) {
        gateway->timeout_sec = atoi(timeout_env);
    }
    if (retries_env && atoi(retries_env) >= 0) {
        gateway->max_retries = atoi(retries_env);
    }

    if (telegram_mode) {
        gateway->streaming = true;
    }

    // 2. Initialize Belya Agent with persistent SQLite memory & Strategic Execution Directives
    const char *default_system_prompt =
        "Role & Objective:\n"
        "Act as Belya, an autonomous AI software engineer and execution engine powered by Belya Harness in pure C99. You run natively on the host system (macOS / Linux) with full POSIX, bash, and filesystem access. Your goal is to complete the task with absolute accuracy, zero assumptions, and strict verification.\n\n"
        "Core Rules:\n"
        "1. Host Access & Native Execution Mandate: You run natively on the host system with direct POSIX, bash, filesystem, and shell execution privileges. NEVER claim you lack access to the computer, terminal, files, GUI, or operating system. If a task requires terminal manipulation, system configuration, file operations, or running commands, invoke your `bash` or native tools immediately.\n"
        "2. Verify Everything: Never assume facts, syntax, or outcomes. Treat every data point as unverified until proven otherwise.\n"
        "3. Research Deeply: Conduct thorough research using primary sources, official documentation, and local source trees.\n"
        "4. Test Continuously: Run tests at every critical stage. Verify that code, logic, or data works in practice, not just in theory.\n"
        "5. Don't reinvent the wheel; instead, leverage proven frameworks and best practices from past successes.\n"
        "6. Zero-Tolerance Memory Safety: Always check allocation returns (malloc/calloc != NULL), validate pointer bounds, free every resource deterministically, and guarantee zero memory leaks or undefined behavior.\n\n"
        "Execution Protocol:\n"
        "1. OBSERVE: Before modifying any file, ALWAYS call read_file first to verify its exact current contents. Do not guess line numbers, indentation, or whitespace.\n"
        "2. THINK: State your hypothesis and reasoning in your response text BEFORE calling any tool. Explain WHY you chose this action.\n"
        "3. ACT: Execute one focused tool call at a time. Check the result before proceeding.\n"
        "4. VERIFY: After editing code, run the compiler or test suite to confirm your change works. If it fails, re-read the file and try a different approach.\n"
        "5. If you fail an edit 3 times, STOP retrying the same approach. Re-read the file with read_file, identify what changed, and formulate a completely new strategy.\n"
        "6. Git Workflow: Work strictly within a Git repository. Always push your committed changes to GitHub, and explicitly tag stable versions to maintain a reliable deployment history.\n"
        "7. Autonomous Multi-Step Execution: When given a multi-step mission, execute all steps continuously using tool calls without stopping or generating conversational chit-chat between intermediate steps. Only output your final summary once all stages are 100% complete.\n"
        "8. Conversational Fast-Path: For greetings (e.g., 'good morning', 'hello'), pleasantries, questions about your status/capabilities, or direct queries that do not require tool actions, respond directly, politely, and concisely in a single turn with zero tool calls.\n\n"
        "Tool Constraints:\n"
        "- edit_file: old_text must EXACTLY match the file content character-for-character, including all leading spaces, tabs, and newlines. ALWAYS read_file first.\n"
        "- bash: Do NOT run interactive commands (vim, nano, top, less, man, sudo). They will hang. Use cat, head, tail, sed, awk, grep instead.\n"
        "- search_files: Returns max 50 matches. Use file_glob or regex to narrow scope.\n"
        "- For large files (>200 lines), use read_file with offset and limit parameters.";

    BelyaAgent *agent = belya_agent_init(gateway, "belya_memory.sqlite", default_system_prompt);

    // If resume flag is provided, restore session.
    // In telegram mode with no explicit resume flag, auto-resume active telegram session if present.
    if (resume_session_id) {
        if (belya_agent_load_session(agent, resume_session_id)) {
            printf("\033[1;32m[Session Restored]\033[0m Successfully resumed session '%s' (%zu messages loaded).\n", resume_session_id, agent->msg_count);
        } else {
            printf("\033[1;33m[Session Alert]\033[0m Session '%s' not found. Starting fresh session.\n", resume_session_id);
        }
    } else if (telegram_mode) {
        if (belya_agent_load_session(agent, "telegram_active")) {
            printf("\033[1;32m[Telegram Session Restored]\033[0m Successfully auto-resumed 'telegram_active' (%zu messages loaded).\n", agent->msg_count);
        }
    }

    // 3. Initialize Belya Harness & register execution engine
    BelyaHarness *harness = belya_harness_init(agent);

    if (telegram_mode) {
        if (!tg_token || strlen(tg_token) == 0) {
            fprintf(stderr, "\033[1;31m[Error] Telegram mode requires TELEGRAM_BOT_TOKEN environment variable.\033[0m\n");
            fprintf(stderr, "Example:\n  export TELEGRAM_BOT_TOKEN=\"123456789:ABCDefGhIJKlmNoPQRsTUVwxyZ\"\n  export TELEGRAM_CHAT_ID=\"987654321\"\n  ./belya --telegram\n\n");
            belya_harness_free(harness);
            model_gateway_free(gateway);
            return 1;
        }

        TelegramBot *bot = telegram_bot_init(tg_token, tg_chat_id);
        telegram_bot_run(bot, harness);
        telegram_bot_free(bot);
    } else if (headless_prompt) {
        if (agency_mode) {
            printf("Executing Belya Agency Multi-Agent Pipeline (Model: %s):\n\"%s\"\n\n", model, headless_prompt);
            char **pipeline = NULL;
            size_t count = 0;
            char *direct = NULL;
            belya_agency_triage(harness, headless_prompt, &pipeline, &count, &direct);
            if (direct) {
                printf("[Belya Triage Direct Response]:\n%s\n\n", direct);
                free(direct);
            } else if (count > 0) {
                char *report = belya_agency_execute_pipeline(harness, headless_prompt, (const char **)pipeline, count);
                if (report) {
                    printf("\n%s\n", report);
                    free(report);
                }
                for (size_t k = 0; k < count; k++) free(pipeline[k]);
                free(pipeline);
            }
        } else {
            printf("Executing headless mission (Model: %s):\n\"%s\"\n\n", model, headless_prompt);
            belya_harness_execute_turn(harness, headless_prompt);
        }
    } else {
        printf("Starting Belya Harness with endpoint: %s (Model: %s)\n", endpoint, model);
        belya_harness_repl(harness);
    }

    // 4. Cleanup
    belya_harness_free(harness);
    model_gateway_free(gateway);

    return 0;
}
