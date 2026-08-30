#include "c_harness.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *endpoint = getenv("MODEL_ENDPOINT");
    const char *api_key = getenv("MODEL_API_KEY");
    const char *model = getenv("MODEL_NAME");
    const char *timeout_env = getenv("MODEL_TIMEOUT");
    const char *retries_env = getenv("MODEL_MAX_RETRIES");

    if (!endpoint) endpoint = "http://localhost:11434/v1/chat/completions";
    if (!api_key)  api_key = "none";
    if (!model)    model = "hermes-3";

    printf("Starting C Harness with endpoint: %s (Model: %s)\n", endpoint, model);

    // 1. Initialize Model Gateway
    ModelGateway *gateway = model_gateway_init(endpoint, api_key, model);
    if (timeout_env && atoi(timeout_env) > 0) {
        gateway->timeout_sec = atoi(timeout_env);
    }
    if (retries_env && atoi(retries_env) >= 0) {
        gateway->max_retries = atoi(retries_env);
    }

    // 2. Initialize C Agent with persistent SQLite memory
    CAgent *agent = c_agent_init(
        gateway,
        "c_agent_memory.sqlite",
        "You are an expert autonomous software engineer operating inside C Harness. "
        "Always inspect files and execute commands before coming to conclusions."
    );

    // 3. Initialize C Harness & register execution engine
    CHarness *harness = c_harness_init(agent);

    // 4. Run interactive REPL loop
    c_harness_repl(harness);

    // 5. Cleanup
    c_harness_free(harness);
    model_gateway_free(gateway);

    return 0;
}
