#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} DynString;

static inline DynString dyn_str_new(void) {
    DynString ds;
    ds.cap = 512;
    ds.len = 0;
    ds.data = malloc(ds.cap);
    if (ds.data) ds.data[0] = '\0';
    return ds;
}

static inline void dyn_str_grow(DynString *ds, size_t needed) {
    if (ds->len + needed + 1 > ds->cap) {
        size_t new_cap = ds->cap ? ds->cap : 512;
        while (ds->len + needed + 1 > new_cap) {
            new_cap *= 2;
        }
        char *new_data = realloc(ds->data, new_cap);
        if (!new_data) {
            fprintf(stderr, "[Fatal] Out of memory in dyn_str_grow\n");
            abort();
        }
        ds->data = new_data;
        ds->cap = new_cap;
    }
}

static inline void dyn_str_append(DynString *ds, const char *str) {
    if (!str) return;
    size_t slen = strlen(str);
    dyn_str_grow(ds, slen);
    memcpy(ds->data + ds->len, str, slen);
    ds->len += slen;
    ds->data[ds->len] = '\0';
}

static inline void dyn_str_append_len(DynString *ds, const char *str, size_t slen) {
    if (!str || slen == 0) return;
    dyn_str_grow(ds, slen);
    memcpy(ds->data + ds->len, str, slen);
    ds->len += slen;
    ds->data[ds->len] = '\0';
}

static inline void dyn_str_appendf(DynString *ds, const char *fmt, ...) {
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed > 0) {
        dyn_str_grow(ds, (size_t)needed);
        vsnprintf(ds->data + ds->len, (size_t)needed + 1, fmt, ap2);
        ds->len += (size_t)needed;
    }
    va_end(ap2);
}

static inline void dyn_str_clear(DynString *ds) {
    ds->len = 0;
    if (ds->data) {
        ds->data[0] = '\0';
    }
}

static inline void dyn_str_append_escaped(DynString *ds, const char *str) {
    if (!str) return;
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '\"': dyn_str_append(ds, "\\\""); break;
            case '\\': dyn_str_append(ds, "\\\\"); break;
            case '\b': dyn_str_append(ds, "\\b"); break;
            case '\f': dyn_str_append(ds, "\\f"); break;
            case '\n': dyn_str_append(ds, "\\n"); break;
            case '\r': dyn_str_append(ds, "\\r"); break;
            case '\t': dyn_str_append(ds, "\\t"); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*p);
                    dyn_str_append(ds, buf);
                } else {
                    char buf[2] = {*p, '\0'};
                    dyn_str_append(ds, buf);
                }
                break;
        }
    }
}

static inline void dyn_str_free(DynString *ds) {
    if (ds->data) {
        free(ds->data);
        ds->data = NULL;
    }
    ds->len = ds->cap = 0;
}

static inline size_t count_estimated_tokens(const char *text) {
    if (!text || *text == '\0') return 0;
    size_t tokens = 0;
    size_t char_count = 0;
    bool in_word = false;

    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        char_count++;
        if (c <= 32 || (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126)) {
            if (in_word) {
                tokens++;
                in_word = false;
            }
            if (c > 32) tokens++;
        } else {
            in_word = true;
        }
    }
    if (in_word) tokens++;

    size_t char_based = (char_count + 3) / 4;
    return (tokens > char_based) ? tokens : char_based;
}

static inline char *extract_json_object(const char *text, size_t *out_next_offset) {
    if (!text) return NULL;
    const char *start_ptr = strchr(text, '{');
    if (!start_ptr) return NULL;

    int depth = 0;
    bool in_string = false;
    bool escape = false;
    const char *p = start_ptr;

    while (*p) {
        char c = *p;
        if (escape) {
            escape = false;
        } else if (c == '\\' && in_string) {
            escape = true;
        } else if (c == '"') {
            in_string = !in_string;
        } else if (!in_string) {
            if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    size_t len = (size_t)(p - start_ptr) + 1;
                    char *res = malloc(len + 1);
                    if (!res) return NULL;
                    memcpy(res, start_ptr, len);
                    res[len] = '\0';
                    if (out_next_offset) {
                        *out_next_offset = (size_t)((p + 1) - text);
                    }
                    return res;
                }
            }
        }
        p++;
    }
    return NULL;
}

#endif
