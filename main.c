#include "c_harness.h"
#include "telegram_adapter.h"

int main(int argc, char **argv) {
    bool telegram_mode = false;
    const char *resume_session_id = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--telegram") == 0 || strcmp(argv[i], "-t") == 0) {
            telegram_mode = true;
        } else if ((strcmp(argv[i], "--resume") == 0 || strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--session") == 0) && i + 1 < argc) {
            resume_session_id = argv[++i];
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

    // 2. Initialize C Agent with persistent SQLite memory
    CAgent *agent = c_agent_init(
        gateway,
        "c_agent_memory.sqlite",
        "You are an expert autonomous software engineer operating inside CHarness on a VPS. "
        "Always inspect files and verify system status before coming to conclusions."
    );

    // If resume flag is provided, restore session
    if (resume_session_id) {
        if (c_agent_load_session(agent, resume_session_id)) {
            printf("\033[1;32m[Session Restored]\033[0m Successfully resumed session '%s' (%zu messages loaded).\n", resume_session_id, agent->msg_count);
        } else {
            printf("\033[1;33m[Session Alert]\033[0m Session '%s' not found. Starting fresh session.\n", resume_session_id);
        }
    }

    // 3. Initialize CHarness & register execution engine
    CHarness *harness = c_harness_init(agent);

    if (telegram_mode) {
        if (!tg_token || strlen(tg_token) == 0) {
            fprintf(stderr, "\033[1;31m[Error] Telegram mode requires TELEGRAM_BOT_TOKEN environment variable.\033[0m\n");
            fprintf(stderr, "Example:\n  export TELEGRAM_BOT_TOKEN=\"123456789:ABCDefGhIJKlmNoPQRsTUVwxyZ\"\n  export TELEGRAM_CHAT_ID=\"987654321\"\n  ./c_agent_system --telegram\n\n");
            c_harness_free(harness);
            model_gateway_free(gateway);
            return 1;
        }

        TelegramBot *bot = telegram_bot_init(tg_token, tg_chat_id);
        telegram_bot_run(bot, harness);
        telegram_bot_free(bot);
    } else {
        printf("Starting CHarness with endpoint: %s (Model: %s)\n", endpoint, model);
        c_harness_repl(harness);
    }

    // 4. Cleanup
    c_harness_free(harness);
    model_gateway_free(gateway);

    return 0;
}
