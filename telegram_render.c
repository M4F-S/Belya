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

static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static size_t rstrip_len(const char *s, size_t n) {
    while (n > 0 && isspace((unsigned char)s[n - 1])) n--;
    return n;
}

/*
 * Inline markdown inside one line: **bold**, `code`, and "- " bullets.
 * Toggles are deliberately simple - nesting bold inside code is not something
 * this agent emits, and one unbalanced tag makes Telegram reject the message.
 */
static void render_inline(Buf *out, const char *line, size_t n) {
    size_t i = 0;
    const char *p = skip_ws(line);
    size_t indent = (size_t)(p - line);

    if (indent + 1 < n && line[indent] == '-' &&
        (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        buf_add(out, line, indent);
        buf_adds(out, "\xE2\x80\xA2 ");
        i = indent + 1;
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

char *telegram_render_html(const char *markdown) {
    if (!markdown) return NULL;

    Buf out;
    buf_init(&out);
    if (!out.data) return NULL;

    char headers[MAX_COLS][MAX_CELL];
    headers[0][0] = '\0';
    int ncols = 0;
    bool in_table = false;

    const char *p = markdown;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        const char *line = p;
        p = nl ? nl + 1 : p + n;

        const char *trimmed = skip_ws(line);
        size_t tlen = rstrip_len(trimmed, n - (size_t)(trimmed - line));

        if (tlen > 0 && trimmed[0] == '|') {
            char cells[MAX_COLS][MAX_CELL];
            int c = split_row(trimmed, tlen, cells);

            if (!in_table) {
                ncols = c;
                for (int i = 0; i < c; i++) memcpy(headers[i], cells[i], MAX_CELL);
                if (*p) {
                    const char *nl2 = strchr(p, '\n');
                    size_t n2 = nl2 ? (size_t)(nl2 - p) : strlen(p);
                    if (is_separator_row(p, n2)) p = nl2 ? nl2 + 1 : p + n2;
                }
                in_table = true;
                continue;
            }

            /* data row: "• **Header** — value — value" */
            buf_adds(&out, "\xE2\x80\xA2 ");
            for (int i = 0; i < c; i++) {
                if (i > 0) buf_adds(&out, " \xC2\xB7 ");
                if (i < ncols && headers[i][0]) buf_adds(&out, "<b>");
                render_inline(&out, cells[i], strlen(cells[i]));
                if (i < ncols && headers[i][0]) buf_adds(&out, "</b>");
            }
            buf_adds(&out, "\n");
            continue;
        }

        in_table = false;

        if (tlen > 0 && trimmed[0] == '#') {
            size_t h = 0;
            while (h < tlen && trimmed[h] == '#') h++;
            const char *text = skip_ws(trimmed + h);
            size_t tl = rstrip_len(text, tlen - h);
            buf_adds(&out, "<b>");
            render_inline(&out, text, tl);
            buf_adds(&out, "</b>\n");
            continue;
        }

        if (tlen == 3 && (strncmp(trimmed, "---", 3) == 0 || strncmp(trimmed, "***", 3) == 0 ||
                          strncmp(trimmed, "___", 3) == 0)) {
            continue;
        }

        render_inline(&out, line, rstrip_len(line, n));
        buf_adds(&out, "\n");
    }

    return out.data;
}
