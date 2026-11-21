#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "secrets_keystore.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>

static void secure_bzero(void *ptr, size_t size) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (size--) *p++ = 0;
}

void *secrets_alloc_secure(size_t size) {
    if (size == 0) return NULL;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    size_t alloc_size = ((size + (size_t)page_size - 1) / (size_t)page_size) * (size_t)page_size;

    void *ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return calloc(1, size);
    }

    (void)mlock(ptr, alloc_size);

#ifdef MADV_DONTDUMP
    (void)madvise(ptr, alloc_size, MADV_DONTDUMP);
#endif

    return ptr;
}

void secrets_free_secure(void *ptr, size_t size) {
    if (!ptr || size == 0) return;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    size_t alloc_size = ((size + (size_t)page_size - 1) / (size_t)page_size) * (size_t)page_size;

    secure_bzero(ptr, alloc_size);
    (void)munlock(ptr, alloc_size);
    (void)munmap(ptr, alloc_size);
}

char *secrets_strdup_secure(const char *s, size_t *out_size) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)secrets_alloc_secure(len);
    if (copy) {
        memcpy(copy, s, len);
        if (out_size) *out_size = len;
    }
    return copy;
}

static size_t match_secret_token(const char *p) {
    if (!p) return 0;

    // sk-ant-api...
    if (strncmp(p, "sk-ant-", 7) == 0) {
        size_t len = 7;
        while (isalnum((unsigned char)p[len]) || p[len] == '-' || p[len] == '_') len++;
        if (len >= 25) return len;
    }

    // sk-or-v1-...
    if (strncmp(p, "sk-or-v1-", 9) == 0) {
        size_t len = 9;
        while (isxdigit((unsigned char)p[len]) || isalnum((unsigned char)p[len])) len++;
        if (len >= 30) return len;
    }

    // ghp_... or gho_...
    if (strncmp(p, "ghp_", 4) == 0 || strncmp(p, "gho_", 4) == 0 || strncmp(p, "ghu_", 4) == 0) {
        size_t len = 4;
        while (isalnum((unsigned char)p[len]) || p[len] == '_') len++;
        if (len >= 30) return len;
    }

    // github_pat_...
    if (strncmp(p, "github_pat_", 11) == 0) {
        size_t len = 11;
        while (isalnum((unsigned char)p[len]) || p[len] == '_') len++;
        if (len >= 40) return len;
    }

    // tvly-...
    if (strncmp(p, "tvly-", 5) == 0) {
        size_t len = 5;
        while (isalnum((unsigned char)p[len]) || p[len] == '-') len++;
        if (len >= 25) return len;
    }

    // Telegram bot token: [0-9]{8,12}:[a-zA-Z0-9_-]{30,45}
    if (isdigit((unsigned char)*p)) {
        size_t dlen = 0;
        while (isdigit((unsigned char)p[dlen])) dlen++;
        if (dlen >= 8 && dlen <= 12 && p[dlen] == ':') {
            size_t klen = 0;
            const char *key = p + dlen + 1;
            while (isalnum((unsigned char)key[klen]) || key[klen] == '_' || key[klen] == '-') klen++;
            if (klen >= 30 && klen <= 45) {
                return dlen + 1 + klen;
            }
        }
    }

    return 0;
}

void secrets_scrub_inplace(char *buf) {
    if (!buf) return;
    char *p = buf;
    while (*p) {
        size_t match_len = match_secret_token(p);
        if (match_len > 0) {
            const char mask[] = "[REDACTED_SECRET]";
            size_t mask_len = sizeof(mask) - 1;
            if (match_len >= mask_len) {
                memcpy(p, mask, mask_len);
                size_t remaining = strlen(p + match_len);
                memmove(p + mask_len, p + match_len, remaining + 1);
                p += mask_len;
            } else {
                memset(p, '*', match_len);
                p += match_len;
            }
        } else {
            p++;
        }
    }
}

char *secrets_scrub_string(const char *input) {
    if (!input) return NULL;
    size_t in_len = strlen(input);
    size_t cap = in_len * 2 + 128;
    char *out = (char *)malloc(cap);
    if (!out) return strdup(input);

    size_t o = 0;
    size_t i = 0;
    while (i < in_len) {
        size_t match_len = match_secret_token(input + i);
        if (match_len > 0) {
            const char mask[] = "[REDACTED_SECRET]";
            size_t mask_len = sizeof(mask) - 1;
            if (o + mask_len + 1 >= cap) {
                cap = (cap + mask_len) * 2;
                char *new_out = (char *)realloc(out, cap);
                if (!new_out) { free(out); return strdup(input); }
                out = new_out;
            }
            memcpy(out + o, mask, mask_len);
            o += mask_len;
            i += match_len;
        } else {
            if (o + 1 >= cap) {
                cap *= 2;
                char *new_out = (char *)realloc(out, cap);
                if (!new_out) { free(out); return strdup(input); }
                out = new_out;
            }
            out[o++] = input[i++];
        }
    }
    out[o] = '\0';
    return out;
}
