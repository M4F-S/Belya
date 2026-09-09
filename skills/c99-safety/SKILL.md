---
name: c99-safety
description: Zero-tolerance C99 memory safety, bounds checking, and deterministic resource deallocation.
triggers: [memory, leak, malloc, free, c99, pointer, bounds]
category: systems
---

# C99 Memory Safety Protocols

1. Allocation Invariants:
   - Always verify `malloc()` / `calloc()` / `realloc()` return values against `NULL`.
   - On allocation failure, cleanly abort or bubble error up with resource cleanup.
2. Lifetime & Ownership:
   - Every allocated buffer must have a deterministic, single owner responsible for freeing it.
   - Set pointers to `NULL` immediately after `free(ptr)`.
3. Bounds & String Safety:
   - When copying strings, always reserve `strlen(s) + 1` bytes for the null terminator.
   - Use bounded functions (`snprintf`, `memcpy` with verified bounds) instead of `sprintf` or `strcpy`.
4. Sanitizer Verification:
   - Regularly verify all targets compile cleanly under `-fsanitize=address,undefined` with 0 leaks.
