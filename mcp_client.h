#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include "common.h"
#include "minijson.h"
#include "c_agent.h"
#include <sys/types.h>

typedef struct MCPClient {
    char *command;
    int stdin_fd;
    int stdout_fd;
    pid_t pid;
    int request_id;
    bool connected;
} MCPClient;

MCPClient *mcp_client_start(const char *command_line);
JsonValue *mcp_client_request(MCPClient *client, const char *method, JsonValue *params);
JsonValue *mcp_client_list_tools(MCPClient *client);
char *mcp_client_call_tool(MCPClient *client, const char *tool_name, const JsonValue *arguments);
void mcp_client_close(MCPClient *client);

#endif
