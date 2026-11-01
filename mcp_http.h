#ifndef MCP_HTTP_H
#define MCP_HTTP_H

#include "common.h"
#include "minijson.h"
#include "mcp_client.h"

#define MCP_HTTP_PROTOCOL_VERSION "2026-07-28"

/* Builds the JSON-RPC body with _meta inside params (stateless 2026-07-28). */
char *mcp_http_build_body(const char *method, JsonValue *params, int id);

/* Parses a JSON or text/event-stream response body into a JsonValue. */
JsonValue *mcp_http_parse_body(const char *body);

/* Builds a JSON-RPC error object for an HTTP status. */
JsonValue *mcp_http_error_for_status(long status, const char *body);

/* Performs one stateless POST request. Returns parsed JSON or an error object. */
JsonValue *mcp_http_request(MCPClient *client, const char *method, JsonValue *params);

#endif
