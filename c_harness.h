#ifndef C_HARNESS_H
#define C_HARNESS_H

#include "c_agent.h"
#include "linenoise.h"
#include "mcp_client.h"

typedef enum {
    PERM_ALLOW,
    PERM_ASK_USER,
    PERM_DENY
} SecurityLevel;

typedef char *(*ToolCallback)(CAgent *agent, const JsonValue *args);

typedef struct CHarness CHarness;

typedef bool (*PermissionPromptFn)(CHarness *h, const char *tool_name, const char *args_json, void *userdata);

typedef struct {
    char *name;
    SecurityLevel security;
    ToolCallback callback;
    MCPClient *mcp_client;
    char *custom_script_path;   // Non-null if tool is backed by an on-disk script
} CHarnessRegisteredTool;

struct CHarness {
    CAgent *agent;
    CHarnessRegisteredTool tools[64];
    size_t tool_count;
    char cwd[4096];
    MCPClient *mcp_servers[8];
    size_t mcp_server_count;
    PermissionPromptFn permission_prompt_fn;
    void *permission_userdata;
};

CHarness *c_harness_init(CAgent *agent);
void c_harness_register_tool(CHarness *h, const char *name, const char *desc, JsonValue *params, SecurityLevel sec, ToolCallback fn);
bool c_harness_define_custom_tool(CHarness *h, const char *name, const char *desc, JsonValue *params, const char *script_body);
void c_harness_load_custom_tools(CHarness *h);
bool c_harness_connect_mcp(CHarness *h, const char *server_cmd);
void c_harness_repl(CHarness *h);
void c_harness_free(CHarness *h);

#endif
