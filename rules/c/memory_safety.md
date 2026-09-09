---
category: c
description: C99 memory safety, zero memory leaks, bounded string manipulations, and ASan cleanliness.
triggers: [c, c99, memory, pointer, malloc, free, makefile]
---

# C99 Systems Memory Safety Protocol
1. Allocation Validation: Every `malloc()`, `calloc()`, and `realloc()` return value MUST be validated against `NULL`.
2. Explicit Freeing: Every heap allocation must be deterministically freed. Set pointers to `NULL` after freeing.
3. String Safety: Always reserve `+ 1` byte for the null terminator. Never use unbounded functions like `strcpy` or `sprintf`.
4. Compilation Cleanliness: All targets must compile cleanly with `-Wall -Wextra -Werror -std=c99`.
