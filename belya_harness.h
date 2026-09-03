#ifndef BELYA_HARNESS_H
#define BELYA_HARNESS_H

#include "belya_agent.h"
#include "linenoise.h"
#include "mcp_client.h"

typedef enum {
    PERM_ALLOW,
    PERM_ASK_USER,
    PERM_DENY
} SecurityLevel;

typedef char *(*BelyaToolCallback)(BelyaAgent *agent, const JsonValue *args);

typedef struct BelyaHarness BelyaHarness;

typedef bool (*BelyaPermissionPromptFn)(BelyaHarness *h, const char *tool_name, const char *args_json, void *userdata);

typedef struct {
    char *name;
    SecurityLevel security;
    BelyaToolCallback callback;
    MCPClient *mcp_client;
    char *custom_script_path;   // Non-null if tool is backed by an on-disk script
} BelyaRegisteredTool;

struct BelyaHarness {
    BelyaAgent *agent;
    BelyaRegisteredTool tools[64];
    size_t tool_count;
    char cwd[4096];
    MCPClient *mcp_servers[8];
    size_t mcp_server_count;
    BelyaPermissionPromptFn permission_prompt_fn;
    void *permission_userdata;
};

BelyaHarness *belya_harness_init(BelyaAgent *agent);
void belya_harness_register_tool(BelyaHarness *h, const char *name, const char *desc, JsonValue *params, SecurityLevel sec, BelyaToolCallback fn);
bool belya_harness_define_custom_tool(BelyaHarness *h, const char *name, const char *desc, JsonValue *params, const char *script_body);
void belya_harness_load_custom_tools(BelyaHarness *h);
bool belya_harness_connect_mcp(BelyaHarness *h, const char *server_cmd);
void belya_harness_repl(BelyaHarness *h);
void belya_harness_execute_turn(BelyaHarness *h, const char *prompt);
void belya_harness_free(BelyaHarness *h);

extern const char *g_active_custom_script_path;

#endif
