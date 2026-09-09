#include "minifrontmatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *trim_whitespace(char *str) {
    if (!str) return NULL;
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return str;
}

static char *unquote(char *str) {
    if (!str) return NULL;
    str = trim_whitespace(str);
    size_t len = strlen(str);
    if (len >= 2) {
        if ((str[0] == '"' && str[len - 1] == '"') ||
            (str[0] == '\'' && str[len - 1] == '\'')) {
            str[len - 1] = '\0';
            str++;
        }
    }
    return str;
}

static void parse_bracket_list(FrontmatterEntry *entry, const char *raw_val) {
    if (!entry || !raw_val) return;
    const char *start = strchr(raw_val, '[');
    const char *end = strrchr(raw_val, ']');
    if (!start || !end || end <= start) return;

    size_t inner_len = (size_t)(end - start - 1);
    char *buf = malloc(inner_len + 1);
    if (!buf) return;
    memcpy(buf, start + 1, inner_len);
    buf[inner_len] = '\0';

    char *token = strtok(buf, ",");
    while (token && entry->list_count < MAX_FM_LIST_ITEMS) {
        char *cleaned = unquote(token);
        if (cleaned && strlen(cleaned) > 0) {
            entry->list_items[entry->list_count++] = strdup(cleaned);
        }
        token = strtok(NULL, ",");
    }
    free(buf);
}

Frontmatter *frontmatter_parse(const char *markdown_content) {
    if (!markdown_content) return NULL;

    const char *p = markdown_content;

    // Skip UTF-8 BOM if present
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
    }

    // Skip initial blank lines or leading whitespace before opening fence
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }

    // Must start with "---"
    if (strncmp(p, "---", 3) != 0) {
        return NULL;
    }
    p += 3;

    // Opening fence line must contain only whitespace/newlines
    while (*p && *p != '\n') {
        if (!isspace((unsigned char)*p)) return NULL;
        p++;
    }
    if (*p == '\n') p++;

    const char *fm_start = p;
    const char *fm_end = NULL;
    const char *body_start = NULL;

    // Scan for closing "---" or "..." on its own line
    const char *scan = fm_start;
    while (*scan) {
        // Check if start of a line has closing fence
        if (strncmp(scan, "---", 3) == 0 || strncmp(scan, "...", 3) == 0) {
            const char *after = scan + 3;
            bool valid_fence = true;
            while (*after && *after != '\n') {
                if (!isspace((unsigned char)*after)) {
                    valid_fence = false;
                    break;
                }
                after++;
            }
            if (valid_fence) {
                fm_end = scan;
                if (*after == '\n') after++;
                body_start = after;
                break;
            }
        }
        const char *nl = strchr(scan, '\n');
        if (nl) scan = nl + 1;
        else break;
    }

    if (!fm_end || !body_start) {
        return NULL; // No closing fence found
    }

    Frontmatter *fm = calloc(1, sizeof(Frontmatter));
    if (!fm) return NULL;

    // Copy body
    fm->body = strdup(body_start);
    if (!fm->body) {
        free(fm);
        return NULL;
    }

    // Parse lines between fm_start and fm_end
    size_t fm_len = (size_t)(fm_end - fm_start);
    char *fm_block = malloc(fm_len + 1);
    if (!fm_block) {
        frontmatter_free(fm);
        return NULL;
    }
    memcpy(fm_block, fm_start, fm_len);
    fm_block[fm_len] = '\0';

    char *line_ctx = NULL;
    char *line = strtok_r(fm_block, "\r\n", &line_ctx);

    while (line) {
        char *trimmed_line = trim_whitespace(line);
        if (*trimmed_line == '\0' || *trimmed_line == '#') {
            line = strtok_r(NULL, "\r\n", &line_ctx);
            continue;
        }

        // Check for bullet list continuation for last entry: "- item"
        if (strncmp(trimmed_line, "- ", 2) == 0) {
            if (fm->entry_count > 0) {
                FrontmatterEntry *last = &fm->entries[fm->entry_count - 1];
                if (last->list_count < MAX_FM_LIST_ITEMS) {
                    char *item = unquote(trimmed_line + 2);
                    if (item && strlen(item) > 0) {
                        last->list_items[last->list_count++] = strdup(item);
                    }
                }
            }
            line = strtok_r(NULL, "\r\n", &line_ctx);
            continue;
        }

        char *colon = strchr(trimmed_line, ':');
        if (colon && fm->entry_count < MAX_FM_ENTRIES) {
            *colon = '\0';
            char *key = unquote(trimmed_line);
            char *val = unquote(colon + 1);

            if (key && strlen(key) > 0) {
                FrontmatterEntry *entry = &fm->entries[fm->entry_count++];
                entry->key = strdup(key);
                entry->value = strdup(val ? val : "");
                entry->list_count = 0;

                // If bracketed list [a, b, c], parse into list_items
                if (val && strchr(val, '[') && strchr(val, ']')) {
                    parse_bracket_list(entry, val);
                }
            }
        }
        line = strtok_r(NULL, "\r\n", &line_ctx);
    }

    free(fm_block);
    return fm;
}

const char *frontmatter_get_scalar(const Frontmatter *fm, const char *key) {
    if (!fm || !key) return NULL;
    for (size_t i = 0; i < fm->entry_count; i++) {
        if (fm->entries[i].key && strcmp(fm->entries[i].key, key) == 0) {
            return fm->entries[i].value;
        }
    }
    return NULL;
}

const char *frontmatter_get_list_item(const Frontmatter *fm, const char *key, size_t index) {
    if (!fm || !key) return NULL;
    for (size_t i = 0; i < fm->entry_count; i++) {
        if (fm->entries[i].key && strcmp(fm->entries[i].key, key) == 0) {
            if (index < fm->entries[i].list_count) {
                return fm->entries[i].list_items[index];
            }
            return NULL;
        }
    }
    return NULL;
}

size_t frontmatter_get_list_count(const Frontmatter *fm, const char *key) {
    if (!fm || !key) return 0;
    for (size_t i = 0; i < fm->entry_count; i++) {
        if (fm->entries[i].key && strcmp(fm->entries[i].key, key) == 0) {
            return fm->entries[i].list_count;
        }
    }
    return 0;
}

char *frontmatter_get_list_as_string(const Frontmatter *fm, const char *key, const char *delimiter) {
    if (!fm || !key) return NULL;
    const char *delim = delimiter ? delimiter : ", ";
    for (size_t i = 0; i < fm->entry_count; i++) {
        if (fm->entries[i].key && strcmp(fm->entries[i].key, key) == 0) {
            FrontmatterEntry *entry = (FrontmatterEntry *)&fm->entries[i];
            if (entry->list_count == 0) {
                return entry->value ? strdup(entry->value) : strdup("");
            }
            size_t total_len = 0;
            size_t dlen = strlen(delim);
            for (size_t j = 0; j < entry->list_count; j++) {
                total_len += strlen(entry->list_items[j]) + dlen;
            }
            char *res = malloc(total_len + 1);
            if (!res) return NULL;
            res[0] = '\0';
            for (size_t j = 0; j < entry->list_count; j++) {
                if (j > 0) strcat(res, delim);
                strcat(res, entry->list_items[j]);
            }
            return res;
        }
    }
    return NULL;
}

void frontmatter_free(Frontmatter *fm) {
    if (!fm) return;
    for (size_t i = 0; i < fm->entry_count; i++) {
        if (fm->entries[i].key) free(fm->entries[i].key);
        if (fm->entries[i].value) free(fm->entries[i].value);
        for (size_t j = 0; j < fm->entries[i].list_count; j++) {
            if (fm->entries[i].list_items[j]) free(fm->entries[i].list_items[j]);
        }
    }
    if (fm->body) free(fm->body);
    free(fm);
}
