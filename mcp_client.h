#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include "common.h"
#include "minijson.h"
#include "belya_agent.h"
#include <sys/types.h>

typedef enum {
    MCP_TRANSPORT_STDIO = 0,
    MCP_TRANSPORT_HTTP  = 1
} MCPTransport;

typedef struct MCPClient {
    MCPTransport transport;

    /* stdio transport */
    char *command;
    int stdin_fd;
    int stdout_fd;
    pid_t pid;

    /* shared */
    int request_id;
    bool connected;

    /* http transport */
    char *url;
    char *bearer_token;
    long timeout_ms;
    char *session_id;          /* pre-2026 servers only */
    char *negotiated_version;  /* set after the first request */
    int negotiated;
    int legacy;                /* 1 when the server needs initialize + session */
} MCPClient;

MCPClient *mcp_client_start(const char *command_line);
MCPClient *mcp_client_start_http(const char *url);

JsonValue *mcp_client_request(MCPClient *client, const char *method, JsonValue *params);
JsonValue *mcp_client_list_tools(MCPClient *client);
char *mcp_client_call_tool(MCPClient *client, const char *tool_name, const JsonValue *arguments);
void mcp_client_close(MCPClient *client);

#endif
