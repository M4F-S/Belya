# Belya Troubleshooting & Known Failure Patterns

This registry contains known failure traces, compiler diagnostic patterns, and runtime errors paired with root causes and remediation protocols.

---

## Pattern: undefined reference to
- **Root Cause**: Linker error indicating a required library or object file was omitted during the linking stage.
- **Remedy**: Check the Makefile or compilation command. Ensure required flags (e.g., `-lcurl`, `-lsqlite3`, `-lpthread`) are present in `LIBS` and all relevant `.c` source files are listed in `SRCS`.

## Pattern: implicit declaration of function
- **Root Cause**: A function was invoked before being declared, or a required POSIX/C standard library header was not included.
- **Remedy**: Add the missing `#include` (e.g., `<unistd.h>`, `<stdlib.h>`, `<string.h>`, `<sys/stat.h>`) or declare the function prototype before its first call site.

## Pattern: AddressSanitizer: heap-use-after-free
- **Root Cause**: A memory block was accessed after being deallocated via `free()`.
- **Remedy**: Set pointers to `NULL` immediately after freeing them. Check for aliased pointers or callbacks referencing freed state.

## Pattern: AddressSanitizer: heap-buffer-overflow
- **Root Cause**: Read or write operation extended past the allocated memory buffer boundaries.
- **Remedy**: Validate buffer size computations. Always allocate `strlen(str) + 1` for null-terminated strings and ensure bounds checks (`index < capacity`).

## Pattern: Path traversal denied
- **Root Cause**: File operation violated workspace boundary jailing (attempted access outside workspace or using `../`).
- **Remedy**: Keep all file read/write paths relative to the current workspace root. Do not escape the directory hierarchy using `../`.

## Pattern: fatal: not a git repository
- **Root Cause**: Git tool executed in a directory that has not been initialized with git.
- **Remedy**: Execute `git init` or ensure commands are run within the repository root where `.git` is present.

## Pattern: conflicting types for
- **Root Cause**: The function declaration in the header file does not match the definition in the `.c` file.
- **Remedy**: Synchronize return types, parameter types, and `const` qualifiers between `.h` and `.c`.

## Pattern: comparison between signed and unsigned integer expressions
- **Root Cause**: Comparing a signed `int` with an unsigned `size_t` without explicit casting.
- **Remedy**: Declare loop indices and size variables as `size_t`, or apply an explicit cast `(size_t)` if verified non-negative.

## Pattern: unused parameter
- **Root Cause**: A function parameter is defined but never referenced in the function body under strict compiler flags.
- **Remedy**: Cast the parameter with `(void)param;` at the top of the function to explicitly signal intent.

## Pattern: syntax error near unexpected token
- **Root Cause**: Malformed bash command syntax, unescaped quotes, or unmatched parentheses/brackets in shell string.
- **Remedy**: Re-check shell quoting, escape special characters, and run simple commands without nesting complex subshells.
