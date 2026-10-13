---
name: interactive-diff-review
description: Guarded patch inspection, regression analysis, memory safety review, and pre-commit diff auditing.
triggers: [diff-review, review-diff, precommit-audit, audit-diff, audit-patch]
category: quality
---

# Interactive Diff Review & Pre-Commit Audit Protocol

## Objective
Thoroughly inspect all staged or unstaged code mutations before committing. Guarantee zero regressions, deterministic resource management, and strict conformance to repository standards.

## Execution Steps

1. **Diff Extraction**:
   - Run `git_status` to identify modified, added, or untracked files.
   - Run `git_diff` with `staged=false` for working copy changes or `staged=true` for staged changes.
   - For focused inspections, provide the exact `path` parameter to `git_diff`.

2. **Multi-Vector Audit**:
   - **Functional Correctness**: Verify the diff directly addresses the user request without scope creep or unintended side effects.
   - **Memory & Resource Safety**:
     - Check every `malloc`/`calloc` has a non-NULL check and corresponding deterministic `free`.
     - Check file descriptors, SQLite statements, sockets, and child processes are reaped on all exit paths.
     - Verify buffer bounds and ensure no unbounded string functions (`strcpy`, `sprintf`) are introduced.
   - **Error Propagation**: Ensure error return codes are checked, not silently swallowed.
   - **Style & Documentation**: Preserve existing comments, formatting, and public interface signatures.

3. **Compilation & Test Gating**:
   - Run the project test suite (`bash` with `make test` or equivalent) to verify 100% clean execution.
   - If ASan/UBSan targets are available, run sanitizer builds to verify 0 memory leaks and 0 undefined behaviors.

4. **Structured Review Report**:
   - Present a clear summary categorized by:
     - **Files Changed**: List of files and lines added/deleted.
     - **Safety Assessment**: Clean / Warnings / Blockers.
     - **Verification Evidence**: Test commands executed and exit codes.
     - **Recommended Action**: Proceed to commit or patch fixes required.
