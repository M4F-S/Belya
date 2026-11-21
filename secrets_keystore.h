#ifndef SECRETS_KEYSTORE_H
#define SECRETS_KEYSTORE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Allocate memory pinned in RAM (mlock) and excluded from core dumps (MADV_DONTDUMP)
void *secrets_alloc_secure(size_t size);

// Secure zeroing with explicit_bzero, munlock, and free
void secrets_free_secure(void *ptr, size_t size);

// Duplicate a sensitive string into secure, zero-disk memory
char *secrets_strdup_secure(const char *s, size_t *out_size);

// Scrub known API key tokens, Telegram bot tokens, and sensitive patterns from a string.
// Returns a heap-allocated sanitized copy. Caller must free() it.
char *secrets_scrub_string(const char *input);

// In-place scrubbing for mutable buffers
void secrets_scrub_inplace(char *buf);

#ifdef __cplusplus
}
#endif

#endif // SECRETS_KEYSTORE_H
