---
name: safe-refactor-guard
description: Atomic multi-file refactoring, AST call-graph verification, preflight compilation check, and rollback guard.
triggers: [safe-refactor, rename-symbol, callgraph-refactor, verify-callers, ast-refactor]
category: engineering
---

# Safe Refactoring & Call-Graph Verification Protocol

## Objective
Execute complex, multi-file architectural refactors with zero broken references, zero syntax breakage, and instant atomic rollback capability.

## Execution Steps

1. **Pre-Refactor Checkpoint**:
   - Verify `git_status` is clean.
   - Run baseline test suite (`make test`) to ensure a working starting state.
   - Record HEAD commit hash for instant rollback if any unrecoverable regression occurs.

2. **Call-Graph & Symbol Scope Analysis**:
   - Before renaming or altering any function, struct, or macro:
     - Run `goto_definition` to verify declaration and definition locations across header and source files.
     - Run `find_callers` to retrieve every call site across the entire codebase.
     - Enumerate all affected files and call sites before making the first edit.

3. **Incremental Guided Mutation**:
   - Edit files one by one using `edit_file` with `verify_compile=true` whenever applicable.
   - For C/C++ files, the harness preflight watchdog automatically checks syntax before accepting the edit.
   - Update header prototypes first, then implementation files, then external call sites.

4. **Post-Refactor Verification**:
   - Re-run `find_callers` to confirm no orphaned references to old symbol names remain.
   - Run full unit tests and sanitizer checks (`make test`).
   - If any test fails, diagnose the error trace immediately and patch or roll back atomically.
