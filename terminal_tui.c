#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "terminal_tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>

int tui_get_terminal_width(void) {
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }
    const char *cols_env = getenv("COLUMNS");
    if (cols_env) {
        int c = atoi(cols_env);
        if (c > 20) return c;
    }
    return 80;
}

int tui_get_terminal_height(void) {
    struct winsize ws;
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        return (int)ws.ws_row;
    }
    const char *lines_env = getenv("LINES");
    if (lines_env) {
        int l = atoi(lines_env);
        if (l > 5) return l;
    }
    return 24;
}

static double tui_get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

// -----------------------------------------------------------------------------
// Pillar 1.1: Live Animated Progress Spinner
// -----------------------------------------------------------------------------

struct TuiSpinner {
    pthread_t thread;
    volatile bool running;
    pthread_mutex_t lock;
    char label[256];
    double start_time;
    bool is_tty;
};

static const char *k_spinner_frames[] = {
    "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
};
static const size_t k_num_spinner_frames = sizeof(k_spinner_frames) / sizeof(k_spinner_frames[0]);

static void *tui_spinner_worker(void *arg) {
    TuiSpinner *s = (TuiSpinner *)arg;
    size_t frame_idx = 0;

    while (s->running) {
        pthread_mutex_lock(&s->lock);
        char current_label[256];
        strncpy(current_label, s->label, sizeof(current_label) - 1);
        current_label[sizeof(current_label) - 1] = '\0';
        pthread_mutex_unlock(&s->lock);

        double elapsed = tui_get_time_sec() - s->start_time;

        if (s->is_tty) {
            fprintf(stdout, "\r\033[K" TUI_BOLD_CYAN "%s" TUI_RESET " %s (" TUI_BOLD_YELLOW "%.1fs" TUI_RESET ")",
                    k_spinner_frames[frame_idx % k_num_spinner_frames],
                    current_label,
                    elapsed);
            fflush(stdout);
        }

        frame_idx++;
        usleep(80000); // 80 ms per frame
    }
    return NULL;
}

TuiSpinner *tui_spinner_start(const char *label) {
    TuiSpinner *s = (TuiSpinner *)calloc(1, sizeof(TuiSpinner));
    if (!s) return NULL;

    pthread_mutex_init(&s->lock, NULL);
    s->start_time = tui_get_time_sec();
    s->is_tty = isatty(STDOUT_FILENO) && !getenv("BELYA_HEADLESS") && !getenv("BELYA_TEST_MODE");
    if (label) {
        snprintf(s->label, sizeof(s->label), "%s", label);
    } else {
        snprintf(s->label, sizeof(s->label), "Executing...");
    }

    if (s->is_tty) {
        s->running = true;
        if (pthread_create(&s->thread, NULL, tui_spinner_worker, s) != 0) {
            s->running = false;
        }
    } else {
        // Non-interactive fallback
        printf("[Executing]: %s...\n", s->label);
        fflush(stdout);
    }

    return s;
}

void tui_spinner_update_label(TuiSpinner *spinner, const char *new_label) {
    if (!spinner || !new_label) return;
    pthread_mutex_lock(&spinner->lock);
    strncpy(spinner->label, new_label, sizeof(spinner->label) - 1);
    spinner->label[sizeof(spinner->label) - 1] = '\0';
    pthread_mutex_unlock(&spinner->lock);
}

void tui_spinner_stop(TuiSpinner *spinner, bool success, const char *completion_msg) {
    if (!spinner) return;

    double elapsed = tui_get_time_sec() - spinner->start_time;

    if (spinner->running) {
        spinner->running = false;
        pthread_join(spinner->thread, NULL);
    }

    if (spinner->is_tty) {
        fprintf(stdout, "\r\033[K");
        if (success) {
            fprintf(stdout, TUI_BOLD_GREEN "✓" TUI_RESET " %s (" TUI_GREEN "%.2fs" TUI_RESET ")\n",
                    completion_msg ? completion_msg : spinner->label, elapsed);
        } else {
            fprintf(stdout, TUI_BOLD_RED "✗" TUI_RESET " %s (" TUI_RED "%.2fs" TUI_RESET ")\n",
                    completion_msg ? completion_msg : spinner->label, elapsed);
        }
        fflush(stdout);
    } else {
        if (success) {
            printf("[Completed]: %s (%.2fs)\n", completion_msg ? completion_msg : spinner->label, elapsed);
        } else {
            printf("[Failed]: %s (%.2fs)\n", completion_msg ? completion_msg : spinner->label, elapsed);
        }
        fflush(stdout);
    }

    pthread_mutex_destroy(&spinner->lock);
    free(spinner);
}

// -----------------------------------------------------------------------------
// Pillar 1.2: Split / Side-by-Side Terminal Diff Inspector
// -----------------------------------------------------------------------------

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TuiDynStr;

static TuiDynStr tui_ds_new(void) {
    TuiDynStr ds;
    ds.cap = 512;
    ds.data = (char *)malloc(ds.cap);
    if (ds.data) ds.data[0] = '\0';
    ds.len = 0;
    return ds;
}

static void tui_ds_append(TuiDynStr *ds, const char *str) {
    if (!str || !ds->data) return;
    size_t slen = strlen(str);
    if (ds->len + slen + 1 >= ds->cap) {
        size_t ncap = (ds->cap * 2) + slen + 64;
        char *ndata = (char *)realloc(ds->data, ncap);
        if (!ndata) return;
        ds->data = ndata;
        ds->cap = ncap;
    }
    memcpy(ds->data + ds->len, str, slen);
    ds->len += slen;
    ds->data[ds->len] = '\0';
}

static void tui_ds_append_len(TuiDynStr *ds, const char *str, size_t slen) {
    if (!str || !ds->data || slen == 0) return;
    if (ds->len + slen + 1 >= ds->cap) {
        size_t ncap = (ds->cap * 2) + slen + 64;
        char *ndata = (char *)realloc(ds->data, ncap);
        if (!ndata) return;
        ds->data = ndata;
        ds->cap = ncap;
    }
    memcpy(ds->data + ds->len, str, slen);
    ds->len += slen;
    ds->data[ds->len] = '\0';
}


// Pads or truncates `str` to fit exactly `width` visible characters
static void tui_format_cell(TuiDynStr *ds, const char *str, size_t str_len, int width, const char *color) {
    if (width <= 0) return;
    int cur_len = 0;
    if (color && strlen(color) > 0) tui_ds_append(ds, color);

    for (size_t i = 0; i < str_len && cur_len < width; i++) {
        char c = str[i];
        if (c == '\t') {
            int spaces = 4 - (cur_len % 4);
            while (spaces-- > 0 && cur_len < width) {
                tui_ds_append(ds, " ");
                cur_len++;
            }
        } else if (c >= 32 && c <= 126) {
            tui_ds_append_len(ds, &c, 1);
            cur_len++;
        }
    }

    if (color && strlen(color) > 0) tui_ds_append(ds, TUI_RESET);

    while (cur_len < width) {
        tui_ds_append(ds, " ");
        cur_len++;
    }
}

char *tui_render_side_by_side_diff(const char *unified_diff, int term_width) {
    if (!unified_diff) return NULL;
    if (term_width <= 20) term_width = tui_get_terminal_width();
    if (term_width < 40) term_width = 80;

    int half_width = (term_width - 3) / 2;
    if (half_width < 15) half_width = 15;

    TuiDynStr out = tui_ds_new();

    // Top border
    tui_ds_append(&out, TUI_BOLD_CYAN "┌");
    for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
    tui_ds_append(&out, "┬");
    for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
    tui_ds_append(&out, "┐\n" TUI_RESET);

    // Title row
    tui_ds_append(&out, TUI_BOLD_CYAN "│" TUI_RESET);
    tui_format_cell(&out, " OLD (Previous)", 15, half_width, TUI_BOLD_RED);
    tui_ds_append(&out, TUI_BOLD_CYAN "│" TUI_RESET);
    tui_format_cell(&out, " NEW (Modified)", 15, half_width, TUI_BOLD_GREEN);
    tui_ds_append(&out, TUI_BOLD_CYAN "│\n" TUI_RESET);

    // Header divider
    tui_ds_append(&out, TUI_BOLD_CYAN "├");
    for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
    tui_ds_append(&out, "┼");
    for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
    tui_ds_append(&out, "┤\n" TUI_RESET);

    // Diff line parsing
    const char *p = unified_diff;
    char left_buf[2048] = {0};
    char right_buf[2048] = {0};
    size_t left_len = 0;
    size_t right_len = 0;

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t line_len = eol ? (size_t)(eol - p) : strlen(p);

        if (strncmp(p, "diff --git", 10) == 0 ||
            strncmp(p, "index ", 6) == 0 ||
            strncmp(p, "--- ", 4) == 0 ||
            strncmp(p, "+++ ", 4) == 0) {
            // File header line
            tui_ds_append(&out, TUI_BOLD_CYAN "│ " TUI_RESET);
            tui_format_cell(&out, p, line_len > (size_t)(half_width * 2) ? (size_t)(half_width * 2) : line_len, (half_width * 2) - 1, TUI_BOLD_WHITE);
            tui_ds_append(&out, TUI_BOLD_CYAN "│\n" TUI_RESET);
        } else if (strncmp(p, "@@ ", 3) == 0) {
            // Hunk divider
            tui_ds_append(&out, TUI_BOLD_CYAN "├");
            for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
            tui_ds_append(&out, "┴");
            for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
            tui_ds_append(&out, "┤\n" TUI_RESET);

            tui_ds_append(&out, TUI_BOLD_CYAN "│ " TUI_RESET);
            tui_format_cell(&out, p, line_len, (half_width * 2) - 1, TUI_BOLD_YELLOW);
            tui_ds_append(&out, TUI_BOLD_CYAN "│\n" TUI_RESET);

            tui_ds_append(&out, TUI_BOLD_CYAN "├");
            for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
            tui_ds_append(&out, "┬");
            for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
            tui_ds_append(&out, "┤\n" TUI_RESET);
        } else if (*p == '-') {
            // Deleted line
            left_len = line_len < sizeof(left_buf) - 1 ? line_len : sizeof(left_buf) - 1;
            strncpy(left_buf, p, left_len);
            left_buf[left_len] = '\0';

            // Check if next line is an addition '+'
            const char *next_line = eol ? eol + 1 : NULL;
            if (next_line && *next_line == '+') {
                const char *next_eol = strchr(next_line, '\n');
                size_t n_len = next_eol ? (size_t)(next_eol - next_line) : strlen(next_line);
                right_len = n_len < sizeof(right_buf) - 1 ? n_len : sizeof(right_buf) - 1;
                strncpy(right_buf, next_line, right_len);
                right_buf[right_len] = '\0';
                p = next_eol ? next_eol + 1 : next_line + strlen(next_line);
                eol = NULL; // already advanced
            } else {
                right_buf[0] = '\0';
                right_len = 0;
            }

            tui_ds_append(&out, TUI_BOLD_CYAN "│" TUI_RESET);
            tui_format_cell(&out, left_buf, left_len, half_width, TUI_RED);
            tui_ds_append(&out, TUI_BOLD_CYAN "│" TUI_RESET);
            tui_format_cell(&out, right_buf, right_len, half_width, TUI_GREEN);
            tui_ds_append(&out, TUI_BOLD_CYAN "│\n" TUI_RESET);
        } else if (*p == '+') {
            // Added line (unpaired with deletion)
            right_len = line_len < sizeof(right_buf) - 1 ? line_len : sizeof(right_buf) - 1;
            strncpy(right_buf, p, right_len);
            right_buf[right_len] = '\0';

            tui_ds_append(&out, TUI_BOLD_CYAN "│" TUI_RESET);
            tui_format_cell(&out, "", 0, half_width, NULL);
            tui_ds_append(&out, TUI_BOLD_CYAN "│" TUI_RESET);
            tui_format_cell(&out, right_buf, right_len, half_width, TUI_GREEN);
            tui_ds_append(&out, TUI_BOLD_CYAN "│\n" TUI_RESET);
        } else {
            // Context line (identical on both sides)
            tui_ds_append(&out, TUI_BOLD_CYAN "│" TUI_RESET);
            tui_format_cell(&out, p, line_len, half_width, TUI_DIM);
            tui_ds_append(&out, TUI_BOLD_CYAN "│" TUI_RESET);
            tui_format_cell(&out, p, line_len, half_width, TUI_DIM);
            tui_ds_append(&out, TUI_BOLD_CYAN "│\n" TUI_RESET);
        }

        if (!eol) break;
        p = eol + 1;
    }

    // Bottom border
    tui_ds_append(&out, TUI_BOLD_CYAN "└");
    for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
    tui_ds_append(&out, "┴");
    for (int i = 0; i < half_width; i++) tui_ds_append(&out, "─");
    tui_ds_append(&out, "┘\n" TUI_RESET);

    return out.data;
}

// -----------------------------------------------------------------------------
// Pillar 1.3: Interactive Keybinding Approval
// -----------------------------------------------------------------------------

void tui_approval_result_free(TuiApprovalResult *res) {
    if (!res) return;
    if (res->edited_args) {
        free(res->edited_args);
        res->edited_args = NULL;
    }
}

TuiApprovalResult tui_interactive_approve(const char *tool_name, const char *args_json, const char *diff_text) {
    TuiApprovalResult res;
    memset(&res, 0, sizeof(res));
    res.action = TUI_ACTION_APPLY;

    if (!isatty(STDIN_FILENO) || getenv("BELYA_HEADLESS") || getenv("BELYA_TEST_MODE")) {
        return res; // Headless or batch mode automatically applies
    }

    struct termios orig_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);

    bool awaiting_input = true;

    while (awaiting_input) {
        // Render Prompt Card
        int width = tui_get_terminal_width();
        if (width > 80) width = 80;

        printf("\n" TUI_BOLD_YELLOW "╭───────────────────── [⚡ Tool Execution Approval Required] ─────────────────────╮" TUI_RESET "\n");
        printf(TUI_BOLD_YELLOW "│" TUI_RESET " Tool:      " TUI_BOLD_WHITE "%-66s" TUI_BOLD_YELLOW "│" TUI_RESET "\n", tool_name ? tool_name : "unknown");
        if (args_json && strlen(args_json) > 0) {
            char preview[65];
            strncpy(preview, args_json, sizeof(preview) - 1);
            preview[sizeof(preview) - 1] = '\0';
            printf(TUI_BOLD_YELLOW "│" TUI_RESET " Arguments: " TUI_CYAN "%-66s" TUI_BOLD_YELLOW "│" TUI_RESET "\n", preview);
        }
        printf(TUI_BOLD_YELLOW "╰─────────────────────────────────────────────────────────────────────────────────╯" TUI_RESET "\n");
        printf("Options: [" TUI_BOLD_GREEN "Y" TUI_RESET "] Apply | [" TUI_BOLD_RED "N" TUI_RESET "] Reject | [" TUI_BOLD_BLUE "E" TUI_RESET "] Edit Args | [" TUI_BOLD_MAGENTA "A" TUI_RESET "] Always Allow | [" TUI_BOLD_CYAN "D" TUI_RESET "] Side-by-Side Diff: ");
        fflush(stdout);

        // Raw mode for single keystroke
        struct termios raw = orig_termios;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);

        char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) != 1) {
            ch = 'y';
        }

        // Restore terminal
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        printf("%c\n", ch);

        if (ch == 'y' || ch == 'Y' || ch == '\r' || ch == '\n') {
            res.action = TUI_ACTION_APPLY;
            awaiting_input = false;
        } else if (ch == 'n' || ch == 'N' || ch == 'q' || ch == 'Q') {
            res.action = TUI_ACTION_REJECT;
            awaiting_input = false;
        } else if (ch == 'a' || ch == 'A') {
            res.action = TUI_ACTION_ALWAYS_ALLOW;
            awaiting_input = false;
        } else if (ch == 'd' || ch == 'D') {
            if (diff_text && strlen(diff_text) > 0) {
                char *sbs = tui_render_side_by_side_diff(diff_text, tui_get_terminal_width());
                if (sbs) {
                    printf("\n%s\n", sbs);
                    free(sbs);
                }
            } else {
                printf(TUI_YELLOW "\n[Diff]: No diff preview available for this operation.\n" TUI_RESET);
            }
            // Loop back to prompt
        } else if (ch == 'e' || ch == 'E') {
            printf("\n" TUI_BOLD_BLUE "Enter modified JSON arguments (or Enter to keep current):" TUI_RESET "\n> ");
            char edit_buf[4096];
            if (fgets(edit_buf, sizeof(edit_buf), stdin)) {
                size_t elen = strlen(edit_buf);
                while (elen > 0 && (edit_buf[elen - 1] == '\r' || edit_buf[elen - 1] == '\n')) {
                    edit_buf[--elen] = '\0';
                }
                if (elen > 0) {
                    res.edited_args = strdup(edit_buf);
                    res.action = TUI_ACTION_EDIT;
                    awaiting_input = false;
                } else {
                    res.action = TUI_ACTION_APPLY;
                    awaiting_input = false;
                }
            } else {
                res.action = TUI_ACTION_APPLY;
                awaiting_input = false;
            }
        }
    }

    return res;
}

// -----------------------------------------------------------------------------
// Pillar 1.4: Live Token HUD
// -----------------------------------------------------------------------------

char *tui_render_token_hud(const TuiHudMetrics *metrics, int term_width) {
    if (!metrics) return NULL;
    if (term_width <= 20) term_width = tui_get_terminal_width();
    if (term_width < 60) term_width = 80;

    double cache_ratio = 0.0;
    if (metrics->prompt_tokens > 0) {
        cache_ratio = ((double)metrics->cached_tokens / (double)metrics->prompt_tokens) * 100.0;
    }

    const char *model_clean = metrics->model_name ? metrics->model_name : "default";
    const char *slash = strrchr(model_clean, '/');
    if (slash) model_clean = slash + 1;

    TuiDynStr ds = tui_ds_new();

    tui_ds_append(&ds, TUI_BOLD_CYAN "╔");
    for (int i = 0; i < term_width - 2; i++) tui_ds_append(&ds, "═");
    tui_ds_append(&ds, "╗\n" TUI_RESET);

    char inner[512];
    snprintf(inner, sizeof(inner), " 🤖 %-18.18s │ 🪙 In: %zu (Cached: %.0f%%) │ Out: %zu │ ⏱ %.0fms │ 💾 %.1fMB RSS ",
             model_clean,
             metrics->prompt_tokens,
             cache_ratio,
             metrics->completion_tokens,
             metrics->last_turn_latency_ms,
             metrics->rss_mb);

    tui_ds_append(&ds, TUI_BOLD_CYAN "║" TUI_RESET);
    tui_format_cell(&ds, inner, strlen(inner), term_width - 2, TUI_BOLD_WHITE);
    tui_ds_append(&ds, TUI_BOLD_CYAN "║\n" TUI_RESET);

    tui_ds_append(&ds, TUI_BOLD_CYAN "╚");
    for (int i = 0; i < term_width - 2; i++) tui_ds_append(&ds, "═");
    tui_ds_append(&ds, "╝\n" TUI_RESET);

    return ds.data;
}
