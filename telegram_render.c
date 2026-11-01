#include "telegram_render.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static void buf_init(Buf *b) {
    b->cap = 256;
    b->len = 0;
    b->data = malloc(b->cap);
    if (b->data) b->data[0] = '\0';
}

static int buf_reserve(Buf *b, size_t extra) {
    if (!b->data) return 0;
    if (b->len + extra + 1 <= b->cap) return 1;
    size_t cap = b->cap;
    while (cap < b->len + extra + 1) cap *= 2;
    char *p = realloc(b->data, cap);
    if (!p) return 0;
    b->data = p;
    b->cap = cap;
    return 1;
}

static void buf_add(Buf *b, const char *s, size_t n) {
    if (!b->data || !buf_reserve(b, n)) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void buf_adds(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }

static void buf_add_escaped(Buf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
            case '&': buf_adds(b, "&amp;"); break;
            case '<': buf_adds(b, "&lt;"); break;
            case '>': buf_adds(b, "&gt;"); break;
            default: buf_add(b, &s[i], 1); break;
        }
    }
}

/*
 * Bounded on purpose. An unbounded whitespace scan walks past the line's '\n',
 * the caller then computes n - (trimmed - line) on a pointer past the line end,
 * the subtraction wraps to ~SIZE_MAX, and split_row() reads off the end of the
 * buffer. That was a real SIGSEGV; every scan here takes an explicit length.
 */
static size_t skip_ws_n(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && isspace((unsigned char)s[i])) i++;
    return i;
}

static size_t rstrip_len(const char *s, size_t n) {
    while (n > 0 && isspace((unsigned char)s[n - 1])) n--;
    return n;
}

/*
 * Inline markdown inside one line: **bold**, `code`, and "- " bullets.
 * Toggles are deliberately simple - nesting bold inside code is not something
 * this agent emits, and one unbalanced tag makes Telegram reject the message.
 * `lead` is a precomputed, bounded indent.
 */
static void render_inline(Buf *out, const char *line, size_t n, size_t lead) {
    size_t i = lead;

    if (lead + 1 < n && line[lead] == '-' &&
        (line[lead + 1] == ' ' || line[lead + 1] == '\t')) {
        buf_add(out, line, lead);
        buf_adds(out, "\xE2\x80\xA2 ");
        i = lead + 1;
        while (i < n && isspace((unsigned char)line[i])) i++;
    }

    bool bold = false, code = false;
    while (i < n) {
        if (!code && i + 1 < n && line[i] == '*' && line[i + 1] == '*') {
            buf_adds(out, bold ? "</b>" : "<b>");
            bold = !bold;
            i += 2;
            continue;
        }
        if (line[i] == '`') {
            buf_adds(out, code ? "</code>" : "<code>");
            code = !code;
            i++;
            continue;
        }
        buf_add_escaped(out, &line[i], 1);
        i++;
    }
    if (bold) buf_adds(out, "</b>");
    if (code) buf_adds(out, "</code>");
}

#define MAX_COLS 8
#define MAX_CELL 160

/* Split "| a | b |" into cells. Returns the cell count. */
static int split_row(const char *line, size_t n, char cells[][MAX_CELL]) {
    int count = 0;
    size_t b = 0;
    size_t e = rstrip_len(line, n);
    while (b < e && isspace((unsigned char)line[b])) b++;
    if (b < e && line[b] == '|') b++;

    size_t cell_start = b;
    for (size_t i = b; i <= e && count < MAX_COLS; i++) {
        if (i == e || line[i] == '|') {
            size_t s = cell_start, t = i;
            while (s < t && isspace((unsigned char)line[s])) s++;
            size_t clen = t - s;
            while (clen > 0 && isspace((unsigned char)line[s + clen - 1])) clen--;
            if (clen >= MAX_CELL) clen = MAX_CELL - 1;
            memcpy(cells[count], line + s, clen);
            cells[count][clen] = '\0';
            count++;
            cell_start = i + 1;
        }
    }
    /* "| a | b |" yields a trailing empty cell; drop it so rows do not render
     * with a dangling separator. */
    if (count > 0 && cells[count - 1][0] == '\0') count--;
    return count;
}

static bool is_separator_row(const char *line, size_t n) {
    bool saw_dash = false;
    for (size_t i = 0; i < n; i++) {
        char c = line[i];
        if (c == '-') saw_dash = true;
        else if (c == '|' || c == ':' || isspace((unsigned char)c)) continue;
        else return false;
    }
    return saw_dash;
}

/* Length of the line at p and the start of the following line. */
static void line_at(const char *p, size_t *n, const char **next) {
    const char *nl = strchr(p, '\n');
    *n = nl ? (size_t)(nl - p) : strlen(p);
    *next = nl ? nl + 1 : p + *n;
}

char *telegram_render_html(const char *markdown) {
    if (!markdown) return NULL;

    Buf out;
    buf_init(&out);
    if (!out.data) return NULL;

    char headers[MAX_COLS][MAX_CELL];
    headers[0][0] = '\0';
    int ncols = 0;
    bool in_table = false;
    bool in_fence = false;

    const char *p = markdown;
    while (*p) {
        size_t n;
        const char *next;
        line_at(p, &n, &next);

        size_t lead = skip_ws_n(p, n);
        size_t tlen = rstrip_len(p + lead, n - lead);

        /* fenced code: verbatim, and a leading pipe inside it is not a table */
        if (tlen >= 3 && strncmp(p + lead, "```", 3) == 0) {
            if (!in_fence) {
                buf_adds(&out, "<pre>");
                in_fence = true;
            } else {
                buf_adds(&out, "</pre>\n");
                in_fence = false;
            }
            in_table = false;
            p = next;
            continue;
        }
        if (in_fence) {
            buf_add_escaped(&out, p, rstrip_len(p, n));
            buf_adds(&out, "\n");
            p = next;
            continue;
        }

        /* A table starts only when the NEXT line is a separator row, so a line
         * that merely begins with a pipe ("| grep foo") survives as text. */
        if (!in_table && tlen > 0 && p[lead] == '|' && *next) {
            size_t n2;
            const char *next2;
            line_at(next, &n2, &next2);
            size_t lead2 = skip_ws_n(next, n2);
            if (is_separator_row(next + lead2, n2 - lead2)) {
                char cells[MAX_COLS][MAX_CELL];
                int c = split_row(p + lead, tlen, cells);
                ncols = c;
                for (int i = 0; i < c; i++) memcpy(headers[i], cells[i], MAX_CELL);
                in_table = true;
                p = next2; /* skip header + separator */
                continue;
            }
        }

        if (in_table && tlen > 0 && p[lead] == '|') {
            char cells[MAX_COLS][MAX_CELL];
            int c = split_row(p + lead, tlen, cells);
            buf_adds(&out, "\xE2\x80\xA2 ");
            for (int i = 0; i < c; i++) {
                if (i > 0) buf_adds(&out, " \xC2\xB7 ");
                /* only the first column carries the row label */
                bool label = (i == 0 && ncols > 0 && headers[0][0]);
                if (label) buf_adds(&out, "<b>");
                render_inline(&out, cells[i], strlen(cells[i]), 0);
                if (label) buf_adds(&out, "</b>");
            }
            buf_adds(&out, "\n");
            p = next;
            continue;
        }
        in_table = false;

        if (tlen > 0 && p[lead] == '#') {
            size_t h = lead;
            while (h < n && p[h] == '#') h++;
            size_t tl_lead = skip_ws_n(p + h, n - h);
            size_t tl = rstrip_len(p + h + tl_lead, n - h - tl_lead);
            buf_adds(&out, "<b>");
            render_inline(&out, p + h + tl_lead, tl, 0);
            buf_adds(&out, "</b>\n");
            p = next;
            continue;
        }

        if (tlen == 3 && (strncmp(p + lead, "---", 3) == 0 || strncmp(p + lead, "***", 3) == 0 ||
                          strncmp(p + lead, "___", 3) == 0)) {
            p = next;
            continue;
        }

        render_inline(&out, p, rstrip_len(p, n), lead);
        buf_adds(&out, "\n");
        p = next;
    }

    if (in_fence) buf_adds(&out, "</pre>\n");
    return out.data;
}
