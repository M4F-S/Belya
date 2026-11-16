#ifndef BELYA_AGENT_H
#define BELYA_AGENT_H

#include "model_adapter.h"
#include "jev_client.h"
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
    JevClient *jev;
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
    long long subagent_last_gc_mono; /* monotonic seconds of the last timed GC, 0 = never */
    char db_path[256];
    void (*pre_compact_hook)(void *userdata);
    void *pre_compact_userdata;
    void *user_data;
} BelyaAgent;

BelyaAgent *belya_agent_init(ModelGateway *gw, const char *db_path, const char *system_instructions);
void belya_agent_register_schema(BelyaAgent *agent, const char *name, const char *desc, JsonValue *params);
void belya_agent_add_message(BelyaAgent *agent, const char *role, const char *content);
void belya_agent_add_tool_result(BelyaAgent *agent, const char *tool_call_id, const char *name, const char *result);
void belya_agent_persist_memory(BelyaAgent *agent, const char *topic, const char *content);
void belya_agent_persist_memory_scoped(BelyaAgent *agent, const char *topic, const char *content, const char *wing, const char *room);
char *belya_agent_search_memory(BelyaAgent *agent, const char *query);
void belya_agent_compact_history(BelyaAgent *agent, size_t keep_recent);
void belya_agent_compact_history_middle_out(BelyaAgent *agent, size_t keep_recent);
void belya_agent_clear_history(BelyaAgent *agent);

#define SYSTEM_PROMPT_DYNAMIC_BOUNDARY "\n\n=== Dynamic Session State ===\n"

// 2026 Competitive Parity Phase 2: 5-Tier Compactor & Lossless Archival
typedef enum {
    COMPACT_TIER_1_MICRO = 1,
    COMPACT_TIER_2_TRUNCATE_TOOLS,
    COMPACT_TIER_3_MIDDLE_OUT,
    COMPACT_TIER_4_FTS5_EVICT,
    COMPACT_TIER_5_EMERGENCY
} CompactTier;

void belya_agent_compact_5tier(BelyaAgent *agent, CompactTier tier, size_t keep_recent);

typedef void (*BelyaPreCompactHookFn)(void *userdata);
void belya_agent_set_pre_compact_hook(BelyaAgent *agent, BelyaPreCompactHookFn fn, void *userdata);

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

// Trajectory Exporters (OpenAI Fine-Tune, ShareGPT/Hermes, and Hermes DPO Format)
bool belya_agent_export_trajectory(BelyaAgent *agent, const char *session_id, const char *out_path);
bool belya_agent_export_sharegpt(BelyaAgent *agent, const char *session_id, const char *out_path);
bool belya_agent_export_dpo(BelyaAgent *agent, const char *session_id, const char *out_path);

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

// Phase 3: Relational Task Board, Cron Engine & Memory Dream Sweep
int64_t belya_agent_task_create(BelyaAgent *agent, const char *subject, const char *description, const char *dependencies, const char *assigned_agent);
bool belya_agent_task_update(BelyaAgent *agent, int64_t task_id, const char *status, const char *description);
char *belya_agent_task_list(BelyaAgent *agent, const char *status_filter);
char *belya_agent_task_get(BelyaAgent *agent, int64_t task_id);

int64_t belya_agent_cron_create(BelyaAgent *agent, const char *name, const char *schedule_cron, const char *prompt);
char *belya_agent_cron_list(BelyaAgent *agent);
int belya_agent_cron_tick(BelyaAgent *agent, DynString *out_logs);

char *belya_agent_dream_sweep(BelyaAgent *agent);

ModelGatewayResponse belya_agent_step(BelyaAgent *agent);
ModelGatewayResponse belya_agent_step_forced_text(BelyaAgent *agent, const char *instruction);
void belya_agent_free(BelyaAgent *agent);

/* ==========================================================================
   Phase 1a — Persistent, parallel, inspectable subagents
   Contract: one row per child in `subagents`, every child message in
   `subagent_messages` with a contiguous per-child idx (1 = child system
   prompt, 2 = task). A child runs bounded turns on the parent's whitelist
   harness and stops at budget_tokens.

   Budget rule: checked before each turn; the turn that crosses the budget is
   recorded, then the child is marked `failed` with an error naming the budget.
   `steps` counts turns actually executed, `spent_tokens` the tokens the
   gateway reported for them.

   Parent tools (belya_harness.c): spawn_subagent gains `wait` (default true
   keeps the blocking envelope) and, with wait=false, returns {"id","status"};
   check_subagent / collect_subagent / cancel_subagent take {"id"} and always
   answer text, never NULL.
   ========================================================================== */
#define BELYA_SUBAGENT_MAX_PARALLEL 4
#define BELYA_SUBAGENT_DEFAULT_BUDGET 50000

typedef struct {
    char *id;
    char *parent_session;
    char *role;
    char *status;        /* running | paused | done | failed | cancelled */
    char *task;
    char *summary;       /* final answer, NULL while running */
    char *error;         /* failure reason, NULL when healthy */
    char *workspace;
    char *model;
    long long budget_tokens;
    long long spent_tokens;
    long long steps;
} BelyaSubagent;

/* Idempotent additive migration: creates subagents + subagent_messages. */
bool belya_subagent_schema_init(BelyaAgent *agent);

/* Fork a child from a role + task + a summary of the parent context (never the
   full transcript). Returns the new child id (caller frees), NULL on error. */
char *belya_subagent_fork(BelyaAgent *parent, const char *role, const char *task, long long budget_tokens);

/* Load one child row (caller frees with belya_subagent_free), NULL if unknown. */
BelyaSubagent *belya_subagent_get(BelyaAgent *agent, const char *id);
void belya_subagent_free(BelyaSubagent *sa);

/* One bounded turn: 1 = still running, 0 = finished (done/failed/cancelled), -1 = error. */
int belya_subagent_step(BelyaAgent *parent, const char *id);

/* Run one child to a terminal state. true = done, false = failed/cancelled/unknown. */
bool belya_subagent_run(BelyaAgent *parent, const char *id);

/* Run `count` children with at most `max_parallel` workers (cap 4).
   true only when every child reached `done`. */
bool belya_subagent_run_parallel(BelyaAgent *parent, const char *const *ids, size_t count, size_t max_parallel);

/* Hand the parent a child's result: summary when done, "failed: <reason>" when
   failed, NULL while running or after cancel. Caller frees. */
char *belya_subagent_collect(BelyaAgent *parent, const char *id);

/* Request cancellation; a running child stops at its next turn boundary. */
bool belya_subagent_cancel(BelyaAgent *parent, const char *id);

/* JSON array of children for the /agents view. Caller frees. */
char *belya_subagent_list_json(BelyaAgent *parent);

/* ---------------------------------------------------------------------------
   Phase 1b — durability: a child outlives the process.

   T1.7 resume, T1.8 orphan sweep at boot, T1.9 artifacts + GC, T1.10 /agents.

   Contract:
   - resume() only from "paused". A "running" row already has a live runtime and
     two owners of one transcript would duplicate turns; done/failed/cancelled
     are terminal. Resume reloads the transcript from SQLite and continues at the
     persisted idx, so nothing is replayed twice.
   - sweep_orphans() is the boot step: any "running" row means a process died
     mid-turn, so it becomes "paused" (resumable by tool). Idempotent, and it
     touches nothing else.
   - artifacts are paths plus a size measured at add time; a path that does not
     exist is refused, because the table must not claim what is not there.
   - gc() deletes terminal children ("done", "failed", "cancelled") older than
     the cutoff together with their messages and artifacts, and never touches
     running/paused ones. No message and no artifact may outlive its child.
   - the /agents report is plain text: exactly one line per child, newest first,
     or "No subagents." when there are none. Caller frees.
   --------------------------------------------------------------------------- */
#define BELYA_SUBAGENT_GC_DEFAULT_SECONDS (7LL * 24LL * 60LL * 60LL)

typedef struct {
    size_t swept;      /* running -> paused at boot */
    size_t collected;  /* done/failed children removed by GC */
    size_t resumed;    /* paused children auto-resumed AND run to done */
    size_t resume_failed; /* auto-resume attempts that did not reach done */
} BelyaSubagentBootReport;

bool belya_subagent_resume(BelyaAgent *agent, const char *id);
size_t belya_subagent_sweep_orphans(BelyaAgent *agent);
BelyaSubagentBootReport belya_subagent_boot(BelyaAgent *agent);
bool belya_subagent_add_artifact(BelyaAgent *agent, const char *id, const char *path);
char *belya_subagent_artifacts_json(BelyaAgent *agent, const char *id);
size_t belya_subagent_gc(BelyaAgent *agent, long long older_than_seconds);
char *belya_subagent_agents_report(BelyaAgent *agent);

/* ---------------------------------------------------------------------------
   Phase 1c — hardening: paged /agents, timed GC, resume-then-run.
   --------------------------------------------------------------------------- */
#define BELYA_SUBAGENT_AGENTS_PER_PAGE 20
#define BELYA_SUBAGENT_GC_INTERVAL_SECONDS (6LL * 60LL * 60LL)

typedef struct {
    char  *text;    /* body only: one line per child, or "No subagents.\n" */
    size_t page;    /* 1-based, clamped into [1, pages] */
    size_t pages;   /* >= 1 */
    size_t total;   /* total children in the table */
} BelyaSubagentAgentsPage;

/* One page of the /agents view. per_page = 0 means BELYA_SUBAGENT_AGENTS_PER_PAGE.
   An out-of-range page is clamped (never an error, never an empty page unless
   the table is empty). `out` must be zeroed by the caller or simply declared and
   passed; it is fully overwritten. Returns false only on a database error. */
bool belya_subagent_agents_page(BelyaAgent *agent, size_t page, size_t per_page,
                                BelyaSubagentAgentsPage *out);
void belya_subagent_agents_page_free(BelyaSubagentAgentsPage *page);

/* Resume a paused child and run it to a terminal state in the SAME call, so a
   resumed row is never left "running" with no runtime owning it. If the runtime
   cannot be opened the row is reverted to "paused". true = child reached done. */
bool belya_subagent_resume_and_run(BelyaAgent *agent, const char *id);

/* ---------------------------------------------------------------------------
   Phase 1c.1 — wait=false now really runs.

   Before this, spawn_subagent(wait=false) forked a "running" row and handed the
   caller a handle that NOTHING executed: no scheduler, and no tool could run a
   running child (resume_subagent only accepted "paused"). The work was stranded
   until the next daemon restart paused it.

   Contract:
   - start_background() executes the child on its own thread, at most
     BELYA_SUBAGENT_BG_MAX at once; it refuses (false) a terminal child, a child
     already owned by this process, or when every worker slot is taken.
   - is_live() answers "this process currently owns a runtime for that id".
   - run_stranded() adopts a "running" row that nobody owns and runs it to a
     terminal state (false for paused/terminal/owned ids).
   - background_drain() waits for in-flight background children to finish; tests
     and shutdown must call it before freeing the parent agent.
   --------------------------------------------------------------------------- */
#define BELYA_SUBAGENT_BG_MAX 4
#define BELYA_SUBAGENT_BG_MAX_SECONDS 600

bool belya_subagent_start_background(BelyaAgent *agent, const char *id);
bool belya_subagent_is_live(const char *id);
size_t belya_subagent_background_count(void);
bool belya_subagent_background_drain(int timeout_ms);
bool belya_subagent_run_stranded(BelyaAgent *agent, const char *id);

/* Rate-limited GC for long-running processes. `now_mono` is monotonic seconds
   (caller supplies it so it is testable). Returns the number collected, 0 when
   the interval has not elapsed or nothing was collected. */
size_t belya_subagent_maybe_gc(BelyaAgent *agent, long long now_mono, long long interval_seconds);

/* Monotonic clock helper exposed so adapters do not add their own clock code. */
long long belya_agent_monotonic_now(void);

#endif
