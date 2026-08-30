#ifndef C_AGENT_H
#define C_AGENT_H

#include "model_adapter.h"
#include <sqlite3.h>

typedef struct {
    char *role;
    char *content;
    char *tool_call_id;
    ModelParsedToolCall *tool_calls;
    size_t tool_call_count;
} AgentMessage;

typedef struct {
    char *name;
    char *description;
    JsonValue *parameters_schema;
} AgentToolSchema;

typedef struct CAgent {
    ModelGateway *gateway;
    sqlite3 *db;
    AgentMessage *messages;
    size_t msg_count;
    size_t msg_cap;
    AgentToolSchema *schemas;
    size_t schema_count;
    size_t max_context_messages;
    bool has_fts5;
} CAgent;

CAgent *c_agent_init(ModelGateway *gw, const char *db_path, const char *system_instructions);
void c_agent_register_schema(CAgent *agent, const char *name, const char *desc, JsonValue *params);
void c_agent_add_message(CAgent *agent, const char *role, const char *content);
void c_agent_add_tool_result(CAgent *agent, const char *tool_call_id, const char *name, const char *result);
void c_agent_persist_memory(CAgent *agent, const char *topic, const char *content);
char *c_agent_search_memory(CAgent *agent, const char *query);
void c_agent_compact_history(CAgent *agent, size_t keep_recent);
void c_agent_clear_history(CAgent *agent);

// Token Budgeting & Estimation
size_t c_agent_total_tokens(const CAgent *agent);

// Session Management
bool c_agent_save_session(CAgent *agent, const char *session_id, const char *title);
bool c_agent_load_session(CAgent *agent, const char *session_id);
char *c_agent_list_sessions(CAgent *agent);

// Skill Auto-Distillation & Trajectory Reflection
char *c_agent_reflect_and_distill(CAgent *agent);

ModelGatewayResponse c_agent_step(CAgent *agent);
void c_agent_free(CAgent *agent);

#endif
