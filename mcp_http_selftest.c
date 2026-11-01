/* Offline unit tests for the stateless MCP 2026-07-28 HTTP client.
 * With --url <endpoint>, also runs a live list+call against a mock server.
 */
#include "common.h"
#include "mcp_client.h"
#include "mcp_http.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_body_shape(void) {
    JsonValue *params = json_create_object();
    json_obj_add(params, "name", json_create_string("echo"));
    JsonValue *args = json_create_object();
    json_obj_add(args, "text", json_create_string("hello"));
    json_obj_add(params, "arguments", args);

    char *body = mcp_http_build_body("tools/call", params, 7);
    assert(body != NULL);
    assert(strstr(body, "\"jsonrpc\":\"2.0\"") != NULL);
    assert(strstr(body, "\"method\":\"tools/call\"") != NULL);
    assert(strstr(body, "\"name\":\"echo\"") != NULL);
    assert(strstr(body, "\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\"") != NULL);
    assert(strstr(body, "\"io.modelcontextprotocol/clientCapabilities\"") != NULL);

    /* _meta must live inside params, never at the JSON-RPC root. */
    const char *params_pos = strstr(body, "\"params\"");
    const char *meta_pos = strstr(body, "_meta");
    assert(params_pos != NULL && meta_pos != NULL && meta_pos > params_pos);

    JsonValue *parsed = json_parse(body);
    assert(parsed != NULL);
    assert(json_obj_get(parsed, "_meta") == NULL);
    JsonValue *p = json_obj_get(parsed, "params");
    assert(p != NULL && json_obj_get(p, "_meta") != NULL);
    json_free(parsed);
    free(body);
    json_free(params);
    printf("  [PASS] body shape: _meta inside params, protocol version, capabilities\n");
}

static void test_plain_json(void) {
    const char *body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"ok\"}]}}";
    JsonValue *v = mcp_http_parse_body(body);
    assert(v != NULL);
    JsonValue *r = json_obj_get(v, "result");
    assert(r != NULL);
    json_free(v);
    printf("  [PASS] plain JSON response parsing\n");
}

static void test_sse(void) {
    const char *body =
        "event: message\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"tools\":[{\"name\":\"echo\"}]}}\n"
        "\n";
    JsonValue *v = mcp_http_parse_body(body);
    assert(v != NULL);
    JsonValue *r = json_obj_get(v, "result");
    assert(r != NULL);
    JsonValue *tools = json_obj_get(r, "tools");
    assert(tools && tools->type == JSON_ARRAY && tools->u.array.count == 1);
    json_free(v);
    printf("  [PASS] text/event-stream frame parsing\n");
}

static void test_status_mapping(void) {
    JsonValue *e401 = mcp_http_error_for_status(401, "no token");
    JsonValue *e403 = mcp_http_error_for_status(403, "denied");
    assert(e401 && e403);
    const char *m1 = json_obj_get_str(json_obj_get(e401, "error"), "message");
    const char *m2 = json_obj_get_str(json_obj_get(e403, "error"), "message");
    assert(m1 && strstr(m1, "Unauthorized") != NULL);
    assert(m2 && strstr(m2, "Forbidden") != NULL);
    json_free(e401); json_free(e403);
    printf("  [PASS] HTTP 401/403 mapped to distinct errors\n");
}

static int live(const char *url) {
    MCPClient *c = mcp_client_start_http(url);
    if (!c) { printf("  [FAIL] could not create HTTP client\n"); return 1; }

    /* Surface transport/HTTP errors verbatim before checking for tools. */
    JsonValue *raw = mcp_client_request(c, "tools/list", NULL);
    if (!raw) {
        printf("  [FAIL] tools/list returned no response\n");
        mcp_client_close(c);
        return 1;
    }
    JsonValue *err = json_obj_get(raw, "error");
    if (err) {
        const char *m = json_obj_get_str(err, "message");
        printf("  [FAIL] server error: %s\n", m ? m : "(no message)");
        json_free(raw);
        mcp_client_close(c);
        return 1;
    }
    json_free(raw);

    JsonValue *tools = mcp_client_list_tools(c);
    if (!tools || tools->type != JSON_ARRAY || tools->u.array.count == 0) {
        printf("  [FAIL] tools/list returned no tools\n");
        mcp_client_close(c);
        return 1;
    }
    printf("  [PASS] live tools/list: %zu tool(s)\n", tools->u.array.count);
    json_free(tools);

    JsonValue *args = json_create_object();
    json_obj_add(args, "text", json_create_string("hello from belya"));
    char *resp = mcp_client_call_tool(c, "echo", args);
    json_free(args);
    if (!resp || !strstr(resp, "hello from belya")) {
        printf("  [FAIL] live tools/call: %s\n", resp ? resp : "(null)");
        free(resp);
        mcp_client_close(c);
        return 1;
    }
    printf("  [PASS] live tools/call: %s\n", resp);
    free(resp);
    mcp_client_close(c);
    return 0;
}

int main(int argc, char **argv) {
    printf("MCP HTTP client selftest (stateless %s)\n", MCP_HTTP_PROTOCOL_VERSION);
    test_body_shape();
    test_plain_json();
    test_sse();
    test_status_mapping();

    if (argc >= 3 && strcmp(argv[1], "--url") == 0) {
        if (live(argv[2]) != 0) return 1;
    }
    printf("ALL MCP HTTP SELFTESTS PASSED\n");
    return 0;
}
