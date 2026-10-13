#ifndef BELYA_HARNESS_H
#define BELYA_HARNESS_H

#include "belya_agent.h"
#include "linenoise.h"
#include "mcp_client.h"
#include <sys/socket.h>
#include <stdint.h>
#include <time.h>

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

#define HARNESS_ACTION_HISTORY_SIZE 32
#define HARNESS_LOOP_THRESHOLD_CYCLES 3

typedef struct {
    uint64_t action_hash;
    char tool_name[48];
    uint32_t return_code;
    time_t timestamp;
} HarnessActionRecord;

typedef struct {
    HarnessActionRecord history[HARNESS_ACTION_HISTORY_SIZE];
    int head;
    int count;
    int consecutive_cycle_matches;
    int detected_period;
} HarnessLoopDetector;

// 2026 Competitive Parity Phase 5: POSIX PTY Subshells & Background Task Manager
#define BELYA_MAX_BACKGROUND_TASKS 16
#define BELYA_TASK_RING_BUFFER_SIZE 65536

typedef enum {
    BELYA_TASK_STATE_UNUSED = 0,
    BELYA_TASK_STATE_RUNNING,
    BELYA_TASK_STATE_COMPLETED,
    BELYA_TASK_STATE_FAILED,
    BELYA_TASK_STATE_KILLED
} BelyaTaskState;

typedef struct {
    int id;
    pid_t pid;
    pid_t pgid;
    int master_fd;
    BelyaTaskState state;
    int exit_code;
    time_t start_time;
    time_t end_time;
    char command[512];
    char ring_buffer[BELYA_TASK_RING_BUFFER_SIZE];
    size_t ring_head;
    size_t ring_tail;
    size_t ring_count;
    size_t total_bytes_captured;
} BelyaBackgroundTask;

typedef struct {
    BelyaBackgroundTask tasks[BELYA_MAX_BACKGROUND_TASKS];
    size_t count;
    int next_id;
} BelyaTaskTable;

struct BelyaHarness {
    BelyaAgent *agent;
    BelyaRegisteredTool tools[64];
    size_t tool_count;
    char cwd[4096];
    MCPClient *mcp_servers[8];
    size_t mcp_server_count;
    BelyaPermissionPromptFn permission_prompt_fn;
    void *permission_userdata;

    // Metacognitive Circuit Breaker state
    char last_failed_tool[64];
    char last_failed_args[256];
    int consecutive_tool_failures;

    // Deterministic Verification Guard state
    bool files_modified_in_turn;
    bool verification_performed_in_turn;
    bool verification_guard_tripped;

    // In-Context Locking Breaker state (Late-2026 Attractor Detector)
    HarnessLoopDetector loop_detector;
    bool loop_breaker_tripped;

    // Belya Agency Tool Bounding & Role Guard state
    char active_role[64];
    bool bash_restricted;
    char tools_whitelist[512];

    // 2026 Competitive Parity Phase 4: Plan Mode Gate & Git Worktree Sandbox
    bool plan_mode;
    char original_worktree_cwd[4096];
    char active_worktree_path[4096];
    bool in_worktree;

    // 2026 Competitive Parity Phase 5: POSIX PTY Subshells & Background Task Manager
    BelyaTaskTable task_table;
};

BelyaHarness *belya_harness_init(BelyaAgent *agent);
BelyaHarness *belya_harness_init_bounded(BelyaAgent *agent, const char *tools_whitelist, const char *role_name);
bool belya_harness_is_tool_whitelisted(const char *whitelist, const char *tool_name);
char *belya_agency_dispatch_subagent(BelyaHarness *parent_harness, const char *role_name, const char *task, const char *extra_context);
bool belya_agency_triage(BelyaHarness *harness, const char *user_input, char ***out_pipeline, size_t *out_count, char **out_direct_reply);
char *belya_agency_execute_pipeline(BelyaHarness *harness, const char *prompt, const char **pipeline, size_t count);

void belya_harness_register_tool(BelyaHarness *h, const char *name, const char *desc, JsonValue *params, SecurityLevel sec, BelyaToolCallback fn);
bool belya_harness_define_custom_tool(BelyaHarness *h, const char *name, const char *desc, JsonValue *params, const char *script_body);
void belya_harness_load_custom_tools(BelyaHarness *h);
bool belya_harness_connect_mcp(BelyaHarness *h, const char *server_cmd);
void belya_harness_repl(BelyaHarness *h);
void belya_harness_execute_turn(BelyaHarness *h, const char *prompt);
void belya_harness_reset_turn_state(BelyaHarness *h);
bool belya_harness_record_tool_observation(BelyaHarness *h, const char *tool_name, const char *args_json, const char *observation, char **out_breaker_msg);
int belya_harness_detect_loop(BelyaHarness *h, int *out_period);
bool is_path_jailed(const char *path, const char *workspace_root, bool is_write);
bool is_path_safe(const char *path);
bool belya_harness_is_secret_path(const char *path);
bool belya_harness_sockaddr_is_private(const struct sockaddr *sa);
char *belya_troubleshooting_resolve(const char *error_trace, const char *troubleshooting_path);
void belya_harness_free(BelyaHarness *h);

// Late-2026 Phase 3: AST Symbol Graph Navigation & ROMA Task Trees
size_t belya_harness_index_symbols(BelyaHarness *h, const char *dir_path);
char *belya_harness_goto_definition(BelyaHarness *h, const char *symbol_name);
char *belya_harness_find_callers(BelyaHarness *h, const char *symbol_name);

// Late-2026 Phase 3: Repo-to-MCP Dynamic Synthesis
bool belya_harness_synthesize_mcp_server(BelyaHarness *h, const char *server_name, const char *tool_name, const char *desc, const char *script_cmd, const char *schema_json);

// Late-2026 Phase 3: Mythos Variant Security Analysis
char *belya_harness_audit_security_variants(BelyaHarness *h, const char *pattern_type, const char *target_path);

// 2026 Competitive Parity Phase 1: Stateful ANSI Diff Colorizer & Signal-Safe Modal
char *belya_ansi_diff_colorize(const char *raw_diff);
bool belya_terminal_permission_prompt(BelyaHarness *h, const char *tool_name, const char *args_json, void *userdata);

// 2026 Competitive Parity Phase 2: Deterministic Lifecycle Hook Runner
typedef enum {
    BELYA_HOOK_PRE_TOOL_USE = 0,
    BELYA_HOOK_POST_TOOL_USE,
    BELYA_HOOK_STOP,
    BELYA_HOOK_PRE_COMPACT
} BelyaHookType;

typedef struct {
    bool blocked;           // True if hook returned exit code 2
    int exit_code;          // Raw exit code
    char *output;           // Hook output (heap-allocated, caller must free)
} BelyaHookResult;

BelyaHookResult belya_run_lifecycle_hook(BelyaHarness *h, BelyaHookType hook_type, const char *tool_name, const char *tool_args, const char *observation);
void belya_hook_result_free(BelyaHookResult *res);

// 2026 Competitive Parity Phase 4: Plan Mode Gate & Git Worktrees
bool belya_harness_enter_plan_mode(BelyaHarness *h);
bool belya_harness_exit_plan_mode(BelyaHarness *h);
char *belya_harness_enter_worktree(BelyaHarness *h, const char *branch_or_id);
char *belya_harness_leave_worktree(BelyaHarness *h, bool keep);

// 2026 Competitive Parity Phase 5: POSIX PTY Subshells & Background Task Manager
int belya_harness_spawn_task(BelyaHarness *h, const char *command);
char *belya_harness_task_list(BelyaHarness *h);
char *belya_harness_task_status(BelyaHarness *h, int id);
char *belya_harness_task_send_input(BelyaHarness *h, int id, const char *input);
char *belya_harness_task_kill(BelyaHarness *h, int id);

extern const char *g_active_custom_script_path;

#endif
