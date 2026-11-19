# Shell surface inventory — Phase 3.1

**Commit audited:** `fba357a` (origin/main, deployed as `d86ce23f`)
**Date:** 2026-11-19
**Author:** Toy, for the audit-remediation plan (`PLAN - audit remediation.md`, Phase 3)
**Status:** INVENTORY ONLY — no code changed. Conversion list at the end needs approval.

Every line number below was machine-checked against this commit: the table was generated
from a regex pass and each `file:line` was re-read and asserted to contain the call it
claims. 38/38 verified.

---

## 1. How the list was produced

```
grep -rn --include=*.c --include=*.h 'system(\|popen(' .
```

Raw hits: **41**. Reconciliation, because a raw count is not a surface:

| bucket | n | detail |
|---|---|---|
| real call sites | **38** | 27 `belya_harness.c`, 7 `test_suite.c`, 3 `belya_agent.c`, 1 `lsp_client.c` |
| comment mentioning `system()` | 1 | `belya_harness.c:5960` (the H26 note) |
| false positives `test_belya_company_system(` | 2 | `test_suite.c:3556`, `:5597` |

A second pass with `(?<![A-Za-z0-9_])(system|popen)\s*\(` found **one site the plain grep
misses** — `belya_agent.c:268`, which turns out to be prose ("system (macOS / Linux)"),
not a call. It is listed here only so the two counts are explainable.

`test_suite.c` is compiled into `belya_test`, **not** into the daemon: those 7 sites are
classed **T** and are not part of the shipped surface.

A third pass covered shell-equivalent execution that never touches `system()`/`popen()`
(`execl("/bin/sh", "-c", …)`, `execlp("bash", "-c", …)`). The plan named only
`system`/`popen`; that scope is too narrow, because the highest-risk paths in this
codebase are `exec` of a shell — see table 2.

---

## 2. Table 1 — `system()` / `popen()` sites

Class: **A** fixed string, no interpolation · **B** interpolation from values we generate
or validate · **C** interpolation from model, web or user content · **T** test-only.

| id | site | function | interpolated value | class | guard / note |
|---|---|---|---|---|---|
| S1 | belya_harness.c:1631 | tool_git_status | tool arg `path`, else cwd | **C** | quoted `cd "%s"`; `is_safe_shell_path()` denylist |
| S2 | belya_harness.c:1660 | tool_git_diff | tool arg `path` | **C** | **unquoted** in `git diff %s %s`; denylist lets spaces and `--output=` through |
| S3 | belya_harness.c:2422 | agency dispatch | none | A | fixed |
| S4 | belya_harness.c:3303 | shadow worktree cleanup | registered shadow path | B | we generated it |
| S5 | belya_harness.c:3307 | shadow worktree cleanup | same | B | `rm -rf`, quoted |
| S6 | belya_harness.c:3313 | shadow worktree cleanup | none | A | fixed |
| S7 | belya_harness.c:3410 | dispatch_worker_speculative | none | A | fixed |
| S8 | belya_harness.c:3415 | dispatch_worker_speculative | `task_id`, `wt_path` | B | generated `wtask_<time>_<rand>_<i>` |
| S9 | belya_harness.c:3418 | dispatch_worker_speculative | `wt_path` | B | fallback form |
| S10 | belya_harness.c:3454 | dispatch_worker_speculative | `wt_path`, `task_id` | B | cleanup |
| S11 | belya_harness.c:3466 | dispatch_worker_speculative | `wt_path`, `task_id` | B | cleanup, error path |
| S12 | belya_harness.c:3516 | dispatch_worker_speculative | `wt_path` | B | `git -C` |
| **S13** | **belya_harness.c:3534** | dispatch_worker_speculative | **`test_cmd` from tool JSON args** | **C** | `cd "wt" && %s` — arbitrary shell, no permission gate |
| S14 | belya_harness.c:3550 | dispatch_worker_speculative | `wt_path` | B | `git -C` |
| S15 | belya_harness.c:3608 | dispatch_worker_speculative | `wt_path`, `patch_file` | B | generated patch path |
| S16 | belya_harness.c:3613 | dispatch_worker_speculative | `saved_cwd`, `patch_file` | B | quoted, internal |
| S17 | belya_harness.c:3655 | dispatch_worker_speculative | `wt_path`, `task_id` | B | cleanup |
| **S18** | **belya_harness.c:3802** | synthesize_mcp_server | `server_path`, **`tool_name` from tool JSON args** | **C** | pipeline `printf … \| %s \| grep -q "name":"%s"`; name unquoted |
| S19 | belya_harness.c:5749 | terminal_permission_prompt | none | A | fixed `git diff HEAD` |
| S20 | belya_harness.c:5989 | enter_worktree | none | A | fixed `mkdir -p` |
| S21 | belya_harness.c:5994 | enter_worktree | `wt_path` from validated `wt_id` | B | allowlist at `:5962-5973` (alnum `_ - . /`, `..` rejected) |
| S22 | belya_harness.c:5997 | enter_worktree | same | B | `rm -rf` of the same path |
| S23 | belya_harness.c:6002 | enter_worktree | `wt_id`, `wt_path` | B | validated id |
| S24 | belya_harness.c:6005 | enter_worktree | `wt_path`, `wt_id` | B | fallback form |
| S25 | belya_harness.c:6052 | leave_worktree | active worktree path | B | internal |
| S26 | belya_harness.c:7386 | repl | none | A | fixed `gcc --version` |
| S27 | belya_harness.c:7388 | repl | none | A | fixed `git rev-parse` |
| S28 | belya_agent.c:2109 | create_checkpoint | none | A | fixed `git stash create` |
| S29 | belya_agent.c:2118 | create_checkpoint | none | A | fixed `git rev-parse HEAD` |
| S30 | belya_agent.c:2179 | rollback_to_checkpoint | `sha` from DB | B | `is_valid_hex_sha()` gate at `:2176` |
| S31 | lsp_client.c:55 | check_binary_in_path | `bin` from internal enum | B | values come from our own table |
| S32 | test_suite.c:1989 | agency test | none | T | test-only |
| S33 | test_suite.c:1993 | agency test | none | T | test-only |
| S34 | test_suite.c:2018 | agency test | baseline sha (test) | T | test-only |
| S35 | test_suite.c:2030 | agency test | none | T | test-only |
| S36 | test_suite.c:2735 | phase2 test | none | T | test-only |
| S37 | test_suite.c:3020 | phase4 test | none | T | test-only |
| S38 | test_suite.c:3028 | phase4 test | none | T | test-only |

**Totals:** A 9 · B 18 · C 4 · T 7 = 38.

---

## 3. Table 2 — shell-equivalent exec paths (no `system`/`popen`)

These matter more than table 1, because several run **without the Landlock sandbox**.

| id | site | what runs | value source | class | sandboxed? |
|---|---|---|---|---|---|
| E1 | belya_harness.c:621 | `/bin/sh -c <cmd>` (tool_bash) | model | C by design | **yes** `:617` |
| E2 | belya_harness.c:6185 | `bash -c <command>` (background task) | model | C by design | **yes** `:6184` |
| **E3** | **belya_harness.c:2020 → 2082** | `sh -c <command>` (run_test_cycle) | **model, `PERM_ALLOW`** (`:5492`) | **C** | **no** |
| **E4** | **belya_harness.c:4433/4435** | exec of `.belya/tools/<name>.sh` (define_tool) | **model-authored script** | **C** | **no** |
| **E5** | **belya_harness.c:3787 → 3802 → 3808 → mcp_client.c:84** | generated script containing `output=$(%s 2>&1)`, then `sh -c` server spawn | **model (`script_cmd`)** | **C** | **no** |
| E6 | belya_harness.c:5871 | project hook script | repo file | B | no (by design) |
| E7 | belya_harness.c:7114/7117/7120 | docker/ssh/local `/bin/sh` PTY session | TUI input | C by design | no (it *is* the shell) |
| E8 | lsp_client.c:182 | `execlp(cmd)` from internal enum | our table | A | n/a |

`sandbox_apply()` has exactly **two** call sites in the tree (`:617`, `:6184`). Every other
exec path above is unconfined.

---

## 4. Ranked findings

**R1 — `run_test_cycle` is an unconfined, auto-approved shell.** `tool_run_test_cycle`
reads `command` from the tool JSON (`:2015`), wraps it as `sh -c` (`:2020`), forks and
`execvp`s it (`:2082`) with **no** `sandbox_apply()` in the child, and the tool is
registered `PERM_ALLOW` (`:5492`). The model therefore has arbitrary shell execution that
bypasses both the bash tool's permission tier and its Landlock confinement. This is the
single most serious item in the phase, and it is not a `system()` site — the plan's
grep-only scope would have missed it.

**R2 — `dispatch_worker_task` interpolates a model-supplied command.** `test_cmd` comes
straight from tool args (`:3676`) and lands in `cd "wt" && %s` executed by `popen`
(`:3534`). No validation, no quoting of the inner command, no permission gate.

**R3 — `synthesize_mcp_server` builds and runs a script from model input.** `tool_name`,
`desc`, `script_cmd` (`:3919-3932`) are pasted into a generated `#!/bin/sh` body
(`:3776-3793`), the file is executed by `system()` for validation (`:3802`), then connected
as an MCP server whose command line is executed by `sh -c` (`mcp_client.c:84`).

**R4 — `git_diff` puts a tool arg into an unquoted shell word.** `git diff %s %s`
(`:1655-1657`) with `path` from the model. The denylist blocks `;`/`|`/`&`/`$`/backticks/
quotes, but spaces survive, so argument injection is reachable (e.g. `--output=<file>`).
No command execution, but file-write via git option is a real capability.

**R5 — `git_status` is quoted and denylisted.** Lower risk than R4; convert anyway because
it is cheap and the denylist is a maintenance liability.

**R6 — `define_tool` executes model-written scripts unconfined.** `:4545-4560` writes
`.belya/tools/<name>.sh` and `:4433` execs it. The script is model-authored, so this is
equivalent to code execution with the daemon's own privileges.

---

## 5. Proposed conversion order (one commit per item, each with a RED proof)

| step | site(s) | change | test |
|---|---|---|---|
| 3.3a | S1, S2 | `run_argv()` with `git -C <dir> status` / `git diff`, no shell at all | `path = "x; touch /tmp/pwned"` and `--output=/tmp/pwned` both fail closed; `/tmp/pwned` never created |
| 3.3b | S18 | replace the `printf \| script \| grep` pipeline with fork/exec of the server script plus an in-process JSON comparison | `tool_name = 'x"; touch /tmp/pwned; #'` → refused, server not created |
| 3.3c | S13 | decide policy (see Q1) then either argv or `sandbox_apply()` + permission tier in the child | metacharacter payload must not create `/tmp/pwned` |
| 3.3d | E3 | `run_test_cycle`: apply `sandbox_apply()` in the child **and** put the tool behind the same permission tier as `bash`, or drop the free-form `command` parameter | run `command="touch /tmp/pwned"` → blocked; `command="make test"` still works |
| 3.3e | E4 | `sandbox_apply()` in the custom-tool child, and refuse `script_path` outside the workspace | tool writing to `/tmp` is denied, tool writing inside the workspace still works |
| 3.3f | E5 | MCP server command: argv + explicit approval, or config-only | model-supplied `cmd` cannot inject a second command |
| 3.3g | tools/lint_no_shell.sh | fail the gate on `system(`/`popen(` **and** `execl?("/bin/sh"`, `execlp("bash"`, `"-c"` exec, outside an allowlist with a written reason | add a dummy `system("echo hi")` → gate fails; remove → passes |

Straightforward B-site conversions (S21-S25, S30, S31) are optional follow-ups; they are
already validated and each costs one small commit. I would not spend the phase on them.

**Not in scope, deliberately:** the bash tool (E1) and the PTY (E2/E7) are *products*, not
defects. `sh -c` is the feature. Their control is the permission tier plus Landlock, and
that combination is in place.

---

## 6. Also in Phase 3

`research_fetch()` sets `CURLOPT_PROTOCOLS_STR` (http/https) but has **no** private-address
or loopback blocking. Phase 1's `ALMAZ_RESEARCH_ALLOW_LOOPBACK` relies on the absence of
that guard and logs a warning; the guard has to land with it, not after it.

---

## 7. Open questions (need a decision before 3.3c/3.3e/3.3f)

- **Q1.** Is `test_cmd` a deliberate "give the worker a test command" feature (keep it,
  but confine it and gate it) or an accident (make it argv-only)?
- **Q2.** Should `run_test_cycle`'s free-form `command` stay at all, given `bash` already
  exists and is gated? Removing it is the smallest safe change.
- **Q3.** May the model create MCP servers at all in production, or is that a config-time
  operation? If config-time, E5 disappears from the model surface entirely.
- **Q4.** Do we convert class B sites in this phase or defer them as a cleanup batch?

No code will change until these are answered and the conversion list is approved.
