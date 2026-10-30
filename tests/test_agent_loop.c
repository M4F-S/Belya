/* tests/test_agent_loop.c — first tests that drive the real agent loop.
 *
 * Everything else in test_suite.c calls tool callbacks directly; these call
 * belya_agent_step() against a scripted HTTP model, so the request builder,
 * the SSE/JSON decode path, tool-call parsing and message bookkeeping are all
 * exercised end to end.
 *
 * Run via `make test-loop` (starts tests/mock_model_server.py).
 */
#include "belya_harness.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  [ok] %s\n", msg);                                        \
        } else {                                                               \
            printf("  [FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);          \
            g_fail++;                                                          \
        }                                                                      \
    } while (0)

static const char *mock_url(void) {
    const char *url = getenv("MODEL_MOCK_URL");
    return (url && *url) ? url : "http://127.0.0.1:18081/v1/chat/completions";
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    ModelGateway *gw = model_gateway_init(mock_url(), "test-key", "mock-model");
    if (!gw) {
        fprintf(stderr, "gateway init failed\n");
        return 2;
    }
    gw->streaming = false;   /* non-streaming responses from the mock */
    gw->max_retries = 2;     /* 429 path must retry */
    gw->timeout_sec = 10;

    BelyaAgent *agent = belya_agent_init(gw, ":memory:", "You are a test agent.");
    if (!agent) {
        fprintf(stderr, "agent init failed\n");
        return 2;
    }
    BelyaHarness *h = belya_harness_init(agent);
    CHECK(h != NULL, "harness initialises and registers tools");
    CHECK(h && h->tool_count >= 30, "tool registry populated");

    belya_agent_add_message(agent, "user", "say hello");

    /* 1. plain text turn ------------------------------------------------ */
    size_t before = agent->msg_count;
    ModelGatewayResponse r1 = belya_agent_step(agent);
    CHECK(!r1.has_tool_call, "text reply parsed as text (no tool call)");
    CHECK(r1.content && strstr(r1.content, "hello from mock") != NULL,
          "assistant content decoded from JSON");
    CHECK(agent->msg_count == before + 1, "assistant message appended to history");
    model_gateway_response_free(&r1);

    /* 2. tool-call turn -------------------------------------------------- */
    belya_agent_add_message(agent, "user", "run a command");
    ModelGatewayResponse r2 = belya_agent_step(agent);
    CHECK(r2.has_tool_call, "tool_call detected");
    CHECK(r2.tool_call_count == 1, "exactly one tool call parsed");
    CHECK(r2.has_tool_call && r2.tool_calls[0].name &&
              strcmp(r2.tool_calls[0].name, "bash") == 0,
          "tool name parsed");
    CHECK(r2.has_tool_call && r2.tool_calls[0].arguments_json &&
              strstr(r2.tool_calls[0].arguments_json, "echo hi") != NULL,
          "tool arguments parsed");
    model_gateway_response_free(&r2);

    /* 3. 429 then success ------------------------------------------------- */
    belya_agent_add_message(agent, "user", "retry please");
    ModelGatewayResponse r3 = belya_agent_step(agent);
    CHECK(r3.content && strstr(r3.content, "after retry") != NULL,
          "429 is retried and the retry response is returned");
    model_gateway_response_free(&r3);

    belya_harness_free(h);
    model_gateway_free(gw);

    printf("\nagent loop tests: %s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
