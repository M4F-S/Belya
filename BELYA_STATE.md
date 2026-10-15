# 🚀 Belya Autonomous Coding Engine: Project State & Session Handoff

> **⚠️ VERIFIED STATE — 2026-10-15** (measured on the live VPS, not copied from earlier docs)
> - **Deployed commit:** `920db5e` (branch `main`, == `origin/main`, tree clean)
> - **Deployed artifact:** `/opt/belya/belya` = `/opt/charness/belya`, sha256 `919ff1a1…`, 372,024 B
> - **Verified by:** rebuilding `920db5e` from GitHub — output was byte-identical to the running binary
> - **Tests at this commit:** **49/49** (`make test`). **Tools:** 41 unique names.
> - **Host:** Linux x86-64 (Ubuntu 24.04 VPS). macOS ARM64 appears below only as a dev-host note.
> - Numbers further down this file that disagree with the block above are **stale** — the block wins.

**Repository:** `M4F-S/Belya`  
**Current Version:** `v2.1.0-phase7` (Competitive Parity & Late-2026 Sovereign Architecture)  
**Host Environment:** Remote VPS Ubuntu 24.04, Linux x86-64 (`187.124.2.26`) — the repo also builds on macOS (Darwin ARM64) dev hosts; **this deployment is Linux**  
**Deployment Daemon:** `/opt/belya/belya` under `belya.service`  
**Active Inference Backend:** `deepseek-v4.1-flash` via OpenCode Go Flat-Rate Gateway  
**Memory Architecture:** SQLite 3 with FTS5, Gomaa Scoped Wings, and WAL Mode (`belya_memory.sqlite`)  

---

## 🏛️ 1. Architectural Blueprint & Status

Belya is the **production-grade, rock-solid, high-performance C99 autonomous coding agent and systems execution harness**.
- **Cold Start:** < 1.5 ms (< 5 ms strict budget)
- **Runtime Footprint (2026-10-15, live daemon):** VmRSS ≈ 17.0 MiB · PSS ≈ 8.1 MiB · cgroup ≈ 51 MiB (peak 130 MiB). RSS is **session-dependent** — the same process measured 16.5 → 17.4 MiB across one session. The `9.70 MB` figure quoted in the README is the **benchmark-arena peak** (<25 MB target), a different workload. Always state which metric: VmRSS, PSS or cgroup.
- **Binary Footprint (2026-10-15):** 372,024 B (≈363 KiB) and **NOT stripped** — the Makefile has no `strip` step. The `<350 KB` budget is enforced by `scripts/verify_phase.sh`, which strips a *copy* it makes itself.
- **Tooling Suite:** **41 Native Built-in Tools** with permission gating, path jailing, and AST index acceleration
- **Quality Standard:** Strict C99 + POSIX.1-2008, 0 compiler warnings (`-Wall -Wextra -Werror -Wpedantic -O2`), 0 memory leaks under ASan/UBSan

### Competitive Parity Master Plan (Phases 0–7) — 100% Complete & Verified
- **Phase 0 (Foundation & Prelude):** POSIX.1-2008 standards enforcement, POSIX feature macros (`_POSIX_C_SOURCE 200809L`), automated twin sync (`scripts/sync_almaz.sh`), and 7-Gate CI/CD verification runner (`scripts/verify_phase.sh`).
- **Phase 1 (Diff Colorizer & Terminal UX):** Stateful ANSI diff colorizer (`belya_ansi_diff_colorize`), raw signal-safe interactive modal prompt (`belya_terminal_permission_prompt`), human clarification tool (`ask_user`), and CLI slash commands (`/diff`, `/doctor`, `/cost`).
- **Phase 2 (Lifecycle Hooks & 5-Tier Compactor):** Deterministic event-driven hooks (`pre_tool_use`, `post_tool_use`, `stop`, `pre_compact`), 5-tier context compactor (sliding window, tool observation head/tail folding, system deduplication, middle-out summarization, FTS5 spill), and dynamic prompt cache boundary markers.
- **Phase 3 (SQLite Task Board & Cron Engine):** SQLite WAL task board with dependency graph tracking (`task_create`, `task_update`, `task_list`, `task_get`), FTS5 associative search, cron scheduling engine (`cron_create`, `cron_list`, `cron_tick`, `/cron`).
- **Phase 4 (Plan Mode & Isolated Worktrees):** Read-only Plan Mode security sandbox (`enter_plan_mode`, `exit_plan_mode`, `/plan`), isolated ephemeral Git worktrees (`enter_worktree`, `leave_worktree`), and file-first procedural SKILL.md catalog.
- **Phase 5 (PTY Subshells & Background Process Manager):** True POSIX PTY subshell virtualization (`openpty`/`forkpty`), process groups (`setpgid`), circular ring buffers, background task management tool (`manage_task`, `background: true`).
- **Phase 6 (Web Retrieval, Notebook Splicer & LSP-over-MCP):** Multi-provider streaming web retrieval (`web_search`, `web_fetch`), byte-span `.ipynb` Jupyter notebook cell splicer (`edit_notebook`), zero-bloat LSP-over-MCP symbol navigation (`lsp_definition`, `lsp_references`).
- **Phase 7 (Omnichannel Gateway & Remote Backends):** Polymorphic multi-platform gateway (`belya_channel.h`, `belya_channel.c` supporting Discord Gateway v10, Slack Socket Mode/Webhooks with <100ms URL challenge response, and Telegram) + Stdin-piped remote execution backend (`belya_remote_exec_piped`, `exec_backend` tool for Docker containers and SSH hosts with zero host shell quoting).

---

## 🔬 2. Verification & Test Metrics

- **Unit Test Suite:** **49/49 tests passed (100%)** at `920db5e` under GCC with strict flags (`-Werror -Wpedantic -Wshadow -Wformat=2`). ASan/UBSan coverage is exercised separately by `scripts/verify_phase.sh` (Gate 2).
- **Multi-Arena Benchmarks:** **35/35 Arenas passed (100%)** across 6 domain arenas.
- **Universal Exercism Coding Benchmark:** **11/11 Challenges Pass@1 (100.0%)** with zero compiler warnings and zero leaks.
- **Compilation Cleanliness:** Zero warnings under `-Wall -Wextra -Werror -Wpedantic -O2`.
- **Memory Safety:** 100% verified allocation guards, deterministic cleanup, zero leaks.

---

## 🚀 3. Production Deployment Status

- **VPS Host:** `187.124.2.26` (Ubuntu 24.04 LTS, root access).
- **Service Name:** `belya.service` (`systemd`).
- **Target Working Directory:** `/opt/belya`.
- **Git Branch:** `main` tracking `origin/main`.
- **Daemon Binary:** `/opt/belya/belya --telegram`.

---

## 📋 4. Next Session Objectives & Roadmap

1. **Autonomous Daemon Telemetry Monitoring:**
   - Monitor live VPS service health, memory stability, and cron triggers.
2. **Channel Configuration Onboarding:**
   - Onboard Discord Bot Token and Slack App Tokens into environment secrets for live omnichannel dispatch.
3. **Remote Backend Orchestration:**
   - Utilize `exec_backend` tool to execute untrusted user code in isolated Docker containers.
4. **Autonomous Self-Evolution Synchronization:**
   - Keep Belya engineering foundations aligned with Almaz evolutionary insights.
