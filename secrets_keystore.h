#ifndef SECRETS_KEYSTORE_H
#define SECRETS_KEYSTORE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Allocate memory pinned in RAM (mlock) and excluded from core dumps (MADV_DONTDUMP)
void *secrets_alloc_secure(size_t size);

// Secure zeroing with explicit_bzero/volatile wipe, munlock, and unmap/free
void secrets_free_secure(void *ptr, size_t size);

// Free secure memory without needing to know the size (header-tracked)
void secrets_free(void *ptr);

// Duplicate a sensitive string into secure, zero-disk memory
char *secrets_strdup_secure(const char *s, size_t *out_size);

// Scrub known API key tokens, Telegram bot tokens, and sensitive patterns from a string.
// Returns a heap-allocated sanitized copy. Caller must free() it.
// Returns NULL when the copy cannot be allocated: callers MUST treat NULL as
// "do not emit or store this text", never as "use the original string".
char *secrets_scrub_string(const char *input);

// In-place scrubbing for mutable buffers
void secrets_scrub_inplace(char *buf);

// Number of mlock() calls that failed since start. Non-zero means secret pages are
// swappable (the kernel refused to pin them): raise LimitMEMLOCK.
int secrets_mlock_failures(void);

// Test seam: make the next n internal allocations fail. Inert unless called.
void secrets_test_fail_next_alloc(int n);

#ifdef __cplusplus
}
#endif

#endif // SECRETS_KEYSTORE_H
