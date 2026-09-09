#ifndef BELYA_AGENT_H
#define BELYA_AGENT_H

#include "model_adapter.h"
#include <sqlite3.h>

typedef struct {
    char *role;
    char *content;
    char *tool_call_id;
    ModelParsedToolCall *tool_calls;
    size_t tool_call_count;
} BelyaMessage;

typedef struct {
    char *name;
    char *description;
    JsonValue *parameters_schema;
} BelyaToolSchema;

typedef struct BelyaAgent {
    ModelGateway *gateway;
    sqlite3 *db;
    BelyaMessage *messages;
    size_t msg_count;
    size_t msg_cap;
    BelyaToolSchema *schemas;
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
    char db_path[256];
} BelyaAgent;

BelyaAgent *belya_agent_init(ModelGateway *gw, const char *db_path, const char *system_instructions);
void belya_agent_register_schema(BelyaAgent *agent, const char *name, const char *desc, JsonValue *params);
void belya_agent_add_message(BelyaAgent *agent, const char *role, const char *content);
void belya_agent_add_tool_result(BelyaAgent *agent, const char *tool_call_id, const char *name, const char *result);
void belya_agent_persist_memory(BelyaAgent *agent, const char *topic, const char *content);
void belya_agent_persist_memory_scoped(BelyaAgent *agent, const char *topic, const char *content, const char *wing, const char *room);
char *belya_agent_search_memory(BelyaAgent *agent, const char *query);
void belya_agent_compact_history(BelyaAgent *agent, size_t keep_recent);
void belya_agent_clear_history(BelyaAgent *agent);

// Token Budgeting & Estimation
size_t belya_agent_total_tokens(const BelyaAgent *agent);
double belya_get_current_rss_mb(void);

// Gomaa Memory Timeline
void belya_agent_log_timeline(BelyaAgent *agent, const char *event_type, const char *summary);
char *belya_agent_get_timeline(BelyaAgent *agent, int limit);

// Auto-Save Configuration
void belya_agent_set_auto_save_interval(BelyaAgent *agent, size_t interval);

// Session Management
bool belya_agent_save_session(BelyaAgent *agent, const char *session_id, const char *title);
bool belya_agent_load_session(BelyaAgent *agent, const char *session_id);
char *belya_agent_list_sessions(BelyaAgent *agent);

// Skill Auto-Distillation & Trajectory Reflection
char *belya_agent_reflect_and_distill(BelyaAgent *agent);

// Skill Curation & Progressive Disclosure (Procedural Memory)
bool belya_agent_save_skill(BelyaAgent *agent, const char *name, const char *trigger, const char *desc, const char *instructions);
size_t belya_agent_load_disk_skills(BelyaAgent *agent, const char *skills_dir);
size_t belya_agent_load_rule_packs(BelyaAgent *agent, const char *rules_dir, DynString *out_rules);
char *belya_agent_search_skills(BelyaAgent *agent, const char *query);
char *belya_agent_get_skills_manifest(BelyaAgent *agent);
char *belya_agent_match_skill_for_prompt(BelyaAgent *agent, const char *user_prompt);

// Git Checkpointing & Instant Rollback
bool belya_agent_create_checkpoint(BelyaAgent *agent, const char *label);
bool belya_agent_rollback_to_checkpoint(BelyaAgent *agent, const char *checkpoint_id);
char *belya_agent_list_checkpoints(BelyaAgent *agent);

// Trajectory Exporter (OpenAI Fine-Tune JSONL Format)
bool belya_agent_export_trajectory(BelyaAgent *agent, const char *session_id, const char *out_path);

// Historical Conversation Memory Search
char *belya_agent_search_conversations(BelyaAgent *agent, const char *query);

// Declarative Agent Manifest Protocol (Belya Agency)
typedef struct {
    char *name;
    char *role;
    char *tools;          /* Comma-separated whitelist of allowed tools */
    char *model;
    int max_turns;
    char *instructions;
} BelyaAgentManifest;

size_t belya_agent_load_manifests(BelyaAgent *agent, const char *agents_dir);
bool belya_agent_save_manifest(BelyaAgent *agent, const char *name, const char *role, const char *tools, const char *model, int max_turns, const char *instructions);
BelyaAgentManifest *belya_agent_get_manifest(BelyaAgent *agent, const char *name);
BelyaAgentManifest *belya_agent_route_manifest(BelyaAgent *agent, const char *query);
void belya_agent_manifest_free(BelyaAgentManifest *manifest);
char *belya_agent_list_manifests(BelyaAgent *agent);

ModelGatewayResponse belya_agent_step(BelyaAgent *agent);
ModelGatewayResponse belya_agent_step_forced_text(BelyaAgent *agent, const char *instruction);
void belya_agent_free(BelyaAgent *agent);

#endif
