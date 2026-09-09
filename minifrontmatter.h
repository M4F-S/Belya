#ifndef MINIFRONTMATTER_H
#define MINIFRONTMATTER_H

#include <stddef.h>
#include <stdbool.h>

#define MAX_FM_LIST_ITEMS 32
#define MAX_FM_ENTRIES 32

typedef struct {
    char *key;
    char *value;                            /* Scalar value (trimmed, unquoted) */
    char *list_items[MAX_FM_LIST_ITEMS];   /* If value was a list [a, b, c], items stored here */
    size_t list_count;
} FrontmatterEntry;

typedef struct {
    FrontmatterEntry entries[MAX_FM_ENTRIES];
    size_t entry_count;
    char *body;                             /* Pointer to allocated markdown content after closing '---' fence */
} Frontmatter;

/* Parse frontmatter from markdown text buffer.
 * Returns allocated Frontmatter* on success, or NULL if no valid frontmatter or allocation fails. */
Frontmatter *frontmatter_parse(const char *markdown_content);

/* Lookup scalar value by key (returns NULL if not found) */
const char *frontmatter_get_scalar(const Frontmatter *fm, const char *key);

/* Lookup list item by key and index (returns NULL if key not found or index out of bounds) */
const char *frontmatter_get_list_item(const Frontmatter *fm, const char *key, size_t index);

/* Get number of list items for key */
size_t frontmatter_get_list_count(const Frontmatter *fm, const char *key);

/* Format list items as delimited string (caller must free returned string, or NULL if key not found) */
char *frontmatter_get_list_as_string(const Frontmatter *fm, const char *key, const char *delimiter);

/* Free allocated Frontmatter and all internal buffers */
void frontmatter_free(Frontmatter *fm);

#endif /* MINIFRONTMATTER_H */
