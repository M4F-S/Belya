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
    size_t max_context_tokens;
    bool has_fts5;
    size_t turn_count;          /* Total steps taken */
    size_t auto_save_interval;  /* Auto-save every N turns (0=disabled) */
    size_t turns_since_save;    /* Turns since last auto-save */
    size_t compaction_percent;  /* Token budget % that triggers compaction (default 80) */
    size_t compaction_keep;     /* Messages to keep after compaction (default 10) */
    size_t total_prompt_tokens;
    size_t total_completion_tokens;
    size_t total_cached_tokens;
} CAgent;

CAgent *c_agent_init(ModelGateway *gw, const char *db_path, const char *system_instructions);
void c_agent_register_schema(CAgent *agent, const char *name, const char *desc, JsonValue *params);
void c_agent_add_message(CAgent *agent, const char *role, const char *content);
void c_agent_add_tool_result(CAgent *agent, const char *tool_call_id, const char *name, const char *result);
void c_agent_persist_memory(CAgent *agent, const char *topic, const char *content);
void c_agent_persist_memory_scoped(CAgent *agent, const char *topic, const char *content, const char *wing, const char *room);
char *c_agent_search_memory(CAgent *agent, const char *query);
void c_agent_compact_history(CAgent *agent, size_t keep_recent);
void c_agent_clear_history(CAgent *agent);

// Token Budgeting & Estimation
size_t c_agent_total_tokens(const CAgent *agent);

// Gomaa Memory Timeline
void c_agent_log_timeline(CAgent *agent, const char *event_type, const char *summary);
char *c_agent_get_timeline(CAgent *agent, int limit);

// Auto-Save Configuration
void c_agent_set_auto_save_interval(CAgent *agent, size_t interval);

// Session Management
bool c_agent_save_session(CAgent *agent, const char *session_id, const char *title);
bool c_agent_load_session(CAgent *agent, const char *session_id);
char *c_agent_list_sessions(CAgent *agent);

// Skill Auto-Distillation & Trajectory Reflection
char *c_agent_reflect_and_distill(CAgent *agent);

// Skill Curation & Progressive Disclosure (Procedural Memory)
bool c_agent_save_skill(CAgent *agent, const char *name, const char *trigger, const char *desc, const char *instructions);
char *c_agent_search_skills(CAgent *agent, const char *query);
char *c_agent_get_skills_manifest(CAgent *agent);
char *c_agent_match_skill_for_prompt(CAgent *agent, const char *user_prompt);

// Git Checkpointing & Instant Rollback
bool c_agent_create_checkpoint(CAgent *agent, const char *label);
bool c_agent_rollback_to_checkpoint(CAgent *agent, const char *checkpoint_id);
char *c_agent_list_checkpoints(CAgent *agent);

// Trajectory Exporter (OpenAI Fine-Tune JSONL Format)
bool c_agent_export_trajectory(CAgent *agent, const char *session_id, const char *out_path);

ModelGatewayResponse c_agent_step(CAgent *agent);
void c_agent_free(CAgent *agent);

#endif
