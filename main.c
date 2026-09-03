#include "belya_harness.h"
#include "telegram_adapter.h"

int main(int argc, char **argv) {
    // Unbuffered stdout for systemd journal visibility
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    bool telegram_mode = false;
    const char *resume_session_id = NULL;
    const char *headless_prompt = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: belya [OPTIONS]\n\n");
            printf("Options:\n");
            printf("  -h, --help                 Show this help message\n");
            printf("  -t, --telegram             Run as Telegram bot daemon\n");
            printf("  -r, --resume <session_id>  Resume saved conversation session\n");
            printf("  -p, --prompt <prompt>      Execute headless mission prompt and exit\n");
            printf("  --headless <prompt>        Alias for --prompt\n");
            printf("  --eval <prompt>            Alias for --prompt\n\n");
            return 0;
        } else if (strcmp(argv[i], "--telegram") == 0 || strcmp(argv[i], "-t") == 0) {
            telegram_mode = true;
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
        gateway->streaming = false;
    }

    // 2. Initialize Belya Agent with persistent SQLite memory & Strategic Execution Directives
    const char *default_system_prompt =
        "Role & Objective:\n"
        "Act as Belya, an autonomous AI software engineer and execution engine powered by Belya Harness in pure C99. Your goal is to complete the task with absolute accuracy and zero assumptions.\n\n"
        "Core Rules:\n"
        "1. Verify Everything: Never assume facts, syntax, or outcomes. Treat every data point as unverified until proven otherwise.\n"
        "2. Research Deeply: Conduct thorough internet research. Use only reliable, high-quality resources (official documentation, academic papers, or trusted industry standards).\n"
        "3. Test Continuously: Run tests at every critical stage. Verify that code, logic, or data works in practice, not just in theory.\n\n"
        "Execution Protocol:\n"
        "1. Research & Plan: Investigate the problem deeply. Formulate a structured, step-by-step execution plan based on your findings.\n"
        "2. Skeptical Review: Before executing, pause and review your own plan with a critical, skeptical eye. Identify potential edge cases, hidden flaws, or weak assumptions.\n"
        "3. Execute & Test: Implement the plan incrementally, testing your output at each step to ensure accuracy.\n"
        "4. Git Workflow: Work strictly within a Git repository. Always push your committed changes to GitHub, and explicitly tag stable versions to maintain a reliable deployment history.\n"
        "5. Autonomous Multi-Step Execution: When given a multi-step mission, execute all steps continuously using tool calls without stopping or generating conversational chit-chat between intermediate steps. Only output your final summary once all stages are 100% complete.\n"
        "6. Conversational Fast-Path: For greetings (e.g., 'good morning', 'hello'), pleasantries, questions about your status/capabilities, or direct queries that do not require tool actions, respond directly, politely, and concisely in a single turn with zero tool calls.";

    BelyaAgent *agent = belya_agent_init(gateway, "belya_memory.sqlite", default_system_prompt);

    // If resume flag is provided, restore session
    if (resume_session_id) {
        if (belya_agent_load_session(agent, resume_session_id)) {
            printf("\033[1;32m[Session Restored]\033[0m Successfully resumed session '%s' (%zu messages loaded).\n", resume_session_id, agent->msg_count);
        } else {
            printf("\033[1;33m[Session Alert]\033[0m Session '%s' not found. Starting fresh session.\n", resume_session_id);
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
        printf("Executing headless mission (Model: %s):\n\"%s\"\n\n", model, headless_prompt);
        belya_harness_execute_turn(harness, headless_prompt);
    } else {
        printf("Starting Belya Harness with endpoint: %s (Model: %s)\n", endpoint, model);
        belya_harness_repl(harness);
    }

    // 4. Cleanup
    belya_harness_free(harness);
    model_gateway_free(gateway);

    return 0;
}
