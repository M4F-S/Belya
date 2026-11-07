#ifndef TERMINAL_TUI_H
#define TERMINAL_TUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ANSI Escape Code Constants
#define TUI_RESET       "\033[0m"
#define TUI_BOLD        "\033[1m"
#define TUI_DIM         "\033[2m"
#define TUI_RED         "\033[31m"
#define TUI_GREEN       "\033[32m"
#define TUI_YELLOW      "\033[33m"
#define TUI_BLUE        "\033[34m"
#define TUI_MAGENTA     "\033[35m"
#define TUI_CYAN        "\033[36m"
#define TUI_WHITE       "\033[37m"
#define TUI_BOLD_RED    "\033[1;31m"
#define TUI_BOLD_GREEN  "\033[1;32m"
#define TUI_BOLD_YELLOW "\033[1;33m"
#define TUI_BOLD_BLUE   "\033[1;34m"
#define TUI_BOLD_MAGENTA "\033[1;35m"
#define TUI_BOLD_CYAN   "\033[1;36m"
#define TUI_BOLD_WHITE  "\033[1;37m"
#define TUI_BG_DARK     "\033[48;5;236m"

// Terminal Dimensions
int tui_get_terminal_width(void);
int tui_get_terminal_height(void);

// Pillar 1.1: Live Animated Progress Spinner
typedef struct TuiSpinner TuiSpinner;

TuiSpinner *tui_spinner_start(const char *label);
void tui_spinner_update_label(TuiSpinner *spinner, const char *new_label);
void tui_spinner_stop(TuiSpinner *spinner, bool success, const char *completion_msg);

// Pillar 1.2: Split / Side-by-Side Terminal Diff Inspector
// Takes a standard unified diff and formats it into a side-by-side terminal representation.
char *tui_render_side_by_side_diff(const char *unified_diff, int term_width);

// Pillar 1.3: Interactive Keybinding Approval ([Y] Apply / [N] Reject / [E] Edit / [A] Always / [D] Diff)
typedef enum {
    TUI_ACTION_APPLY = 0,   // [Y] or Enter
    TUI_ACTION_REJECT,      // [N]
    TUI_ACTION_EDIT,        // [E]
    TUI_ACTION_ALWAYS_ALLOW,// [A]
    TUI_ACTION_VIEW_DIFF    // [D]
} TuiApprovalAction;

typedef struct {
    TuiApprovalAction action;
    char *edited_args;      // Dynamically allocated if TUI_ACTION_EDIT, caller must free
} TuiApprovalResult;

TuiApprovalResult tui_interactive_approve(const char *tool_name, const char *args_json, const char *diff_text);
void tui_approval_result_free(TuiApprovalResult *res);

// Pillar 1.4: Live Token HUD
typedef struct {
    const char *model_name;
    size_t prompt_tokens;
    size_t completion_tokens;
    size_t cached_tokens;
    double last_turn_latency_ms;
    double rss_mb;
    size_t total_turns;
} TuiHudMetrics;

char *tui_render_token_hud(const TuiHudMetrics *metrics, int term_width);

#ifdef __cplusplus
}
#endif

#endif // TERMINAL_TUI_H
