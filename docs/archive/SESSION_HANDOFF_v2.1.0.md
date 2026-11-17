# 🚀 Sovereign Twin Sibling Session Handoff (v2.1.0-phase7)

> **Date:** October 14, 2026  
> **Status:** Competitive Parity (Phases 0–7) 100% Complete & Verified  
> **Repositories:** `M4F-S/Belya` (`main`, commit `8ddb4e7`) & `M4F-S/Almaz` (`harden/v0.7.1`, commit `09521f9`)  
> **VPS Deployment:** `root@187.124.2.26` (`belya.service` & `almaz.service` Active & Supervised)  
> **Google Drive Mirror:** `gdrive:AI_Coding_Agents_Belya_Almaz` (Auto-Syncing via Git hooks & cron)  
> **Customizations:** Cleaned & trimmed to <800 tokens (irrelevant domain plugins disabled)

---

## 📌 Master Copy-Paste Prompt for New Session

```markdown
You are pairing with me on the twin bare-metal C99 autonomous software engineering systems: **Belya** and **Almaz**.
Please read this comprehensive initialization dossier carefully to resume our development work with 100% continuity and zero knowledge loss.

---

### 🏛️ 1. System Topology, Lineage & Working Trees
1. **Belya (Deterministic Engineering Workhorse & Security Execution Harness):**
   - **Local Workspace:** `/Users/mohamedfathy/Desktop/Privacy C/C Harness and Agent`
   - **Git Remote:** `git@github.com:M4F-S/Belya.git` | Branch: `main` (Latest commit: `8ddb4e7`)
   - **VPS Path:** `/opt/belya` | Service: `belya.service` (PID active running `/opt/belya/belya --telegram`)
   - **Primary Role:** Ultra-fast, deterministic C99 execution engine, 41 native tools, PTY subshells, sandboxed worktrees, omnichannel gateway, remote execution backends.
2. **Almaz (Autopoietic Self-Evolving, Self-Healing Software Organism):**
   - **Local Workspace:** `/Users/mohamedfathy/Desktop/Privacy C/Almaz`
   - **Git Remote:** `git@github.com:M4F-S/Almaz.git` | Branch: `harden/v0.7.1` (Latest commit: `09521f9`)
   - **VPS Path:** `/opt/almaz` | Service: `almaz.service` (supervised by `almaz-watchdog`, daily Darwinian mutation timer `almaz-daily.timer`)
   - **Primary Role:** Living cognitive organism featuring SARSI closed-loop self-model, VIGIL emotional appraisal, KnowSelf situational gating, FadeMem biological decay, CORAL dual-stream memory, and autonomic subconscious idle loop.

---

### 🏆 2. Current Verified Milestone: Competitive Parity (Phases 0–7)
All 8 phases of the Competitive Parity Master Plan are 100% implemented, compiled with 0 warnings, verified with 0 leaks, pushed to GitHub, and running on the VPS:
- **Phase 0 (Foundation & Prelude):** POSIX.1-2008 standards prelude, automated twin sync (`scripts/sync_almaz.sh`), and 7-Gate CI/CD runner (`scripts/verify_phase.sh`).
- **Phase 1 (Diff Colorizer & Terminal UX):** Stateful ANSI diff colorizer (`belya_ansi_diff_colorize`), raw signal-safe modal prompt (`belya_terminal_permission_prompt`), human clarification tool (`ask_user`), and CLI slash commands (`/diff`, `/doctor`, `/cost`).
- **Phase 2 (Lifecycle Hooks & 5-Tier Compactor):** Deterministic lifecycle hooks (`pre_tool_use`, `post_tool_use`, `stop`, `pre_compact`), 5-tier context compactor (sliding window, head/tail folding, system deduplication, middle-out summarization, FTS5 spill), and dynamic prompt cache boundaries.
- **Phase 3 (SQLite WAL Task Board & Cron Engine):** SQLite task board with dependency graph tracking (`task_create`, `task_update`, `task_list`, `task_get`), FTS5 associative recall, cron scheduling engine (`cron_create`, `cron_list`, `cron_tick`, `/cron`).
- **Phase 4 (Plan Mode Sandbox & Isolated Worktrees):** Read-only Plan Mode security sandbox (`enter_plan_mode`, `exit_plan_mode`, `/plan`), isolated ephemeral Git worktrees (`enter_worktree`, `leave_worktree`), file-first procedural SKILL.md catalog.
- **Phase 5 (POSIX PTY Subshells & Task Manager):** Virtualized PTY subshells (`openpty`/`forkpty`), process groups (`setpgid`), circular ring buffers, background task manager (`manage_task`, `background: true`).
- **Phase 6 (Streaming Web, Notebook Splicer & LSP-over-MCP):** Streaming web retrieval (`web_search`, `web_fetch`), byte-span `.ipynb` cell editor (`edit_notebook`), zero-bloat LSP-over-MCP (`lsp_definition`, `lsp_references`).
- **Phase 7 (Omnichannel Gateway & Remote Backends):** Polymorphic multi-channel gateway (`belya_channel.h`, `belya_channel.c` supporting Discord Gateway v10, Slack Socket Mode/Webhooks with <100ms URL challenge response, and Telegram) + Stdin-piped remote execution backend (`belya_remote_exec_piped`, `exec_backend` tool for Docker containers and SSH hosts with zero host shell quoting).

---

### 🔬 3. Quality & Test Scorecard (100% Pass Rate)
- **Belya Unit Tests:** **48/48 tests passed (100%)** (`make test`)
- **Almaz Unit Tests:** **61/61 tests passed (100%)** (`make test`)
- **Almaz Hidden Holdout Suite:** **65/65 Assertions passed (100%)** (`make test-holdout`)
- **6-Arena Benchmarks:** **35/35 Arenas (100%)** on Belya, **36/36 Arenas (100%)** on Almaz (`make arena`)
- **Universal Exercism Coding Benchmark:** **11/11 Challenges Pass@1 (100.0%)** (`make exercism`)
- **Compiler Cleanliness:** Exactly 0 warnings under `-Wall -Wextra -Werror -Wpedantic -O2`
- **Sanitizers:** 0 memory leaks, 0 undefined behavior under AddressSanitizer and UndefinedBehaviorSanitizer
- **Performance:** Cold start < 1.5 ms, Active RSS < 12 MB, Stripped binary ~325 KB
- **Native Tools:** 41 Native Built-In Tools registered and whitelisted in both harnesses

---

### 🗺️ 4. Codebase Architecture & Key Files Map
#### Shared Engine Core:
- `belya_harness.h` / `belya_harness.c`: Core harness struct, 41 native tools, 5-tier context compactor, SQLite WAL task board, cron engine, PTY subshell virtualization, LSP symbol navigation, stdin-piped remote execution.
- `belya_agent.h` / `belya_agent.c`: Agent lifecycle, turn execution loop, Gomaa FTS5 memory, event hooks, Plan Mode security sandbox, token estimation, dynamic prompt caching.
- `belya_channel.h` / `belya_channel.c`: Polymorphic omnichannel gateway (Discord, Slack, Telegram).
- `model_adapter.c`: Multi-provider flat-rate gateway, failover logic, tool call parser.
- `mcp_client.c`: MCP client protocol over stdio/SSE.
- `test_suite.c`: Full unit test suites (48 Belya tests, 61 Almaz tests).
- `scripts/sync_almaz.sh`: Automated twin synchronization tool (preserves cognitive organs).
- `scripts/verify_phase.sh`: 7-gate CI/CD verification runner.

#### Almaz-Specific Cognitive Organs (NEVER overwrite during twin sync):
- `belya_agent.c` (Almaz): SARSI self-model sync (`almaz_agent_sync_self_model`), VIGIL emotional appraisal (`almaz_agent_record_appraisal`), KnowSelf memory gating, autonomic subconscious idle loop.
- `timeline_anomaly.c` / `timeline_anomaly.h`: Real-time telemetry anomaly detector.
- `budget_store.c` / `budget_store.h`: Token spend velocity and budget controller.
- `health_watcher.c` / `health_watcher.h`: Continuous health monitoring and failure diagnosis.
- `watchdog.c`: Out-of-process watchdog supervisor.
- `test_holdout.c`: 65 cognitive holdout assertion tests.

#### Critical Reference Documentation:
- `BELYA_STATE.md`: Detailed living state of Belya.
- `ALMAZ_STATE.md`: Detailed living state of Almaz and cognitive organs.
- `COMPETITIVE_PARITY_MASTER_PLAN.md`: Full 8-Phase competitive roadmap and specifications.
- `README.md` & `CHANGELOG.md` in both repositories.

---

### 🧠 5. Gomaa Memory & Cloud Mirroring
- **SQLite Gomaa Memory:** Persistent architectural memory stored in `belya_memory.sqlite` and `almaz_memory.sqlite` (FTS5 search, scoped wings: Core Invariants, System State, Architectural Decisions).
- **Google Drive Mirror (`gdrive:AI_Coding_Agents_Belya_Almaz`):**
  - Contains complete, clean clones of both repos, unified single-file headers (`BELYA_ALL_HEADERS.h`, `ALMAZ_ALL_HEADERS.h`), and AI chatbot review dossiers (`00_README_AI_REVIEW.md`, `01_LATEST_CHANGES_AND_DIFFS_v2.1.0.md`).
  - Auto-updates on every local Git commit via `post-commit` hooks, manually via `make gdrive`, and every 2 hours on the VPS via cron.

---

### 🛡️ 6. Core Rules & Invariants
1. **Host Access & Native Execution Mandate:** You run natively on macOS/Linux with direct POSIX, bash, filesystem, and shell execution privileges. Always invoke your native tools directly.
2. **Zero-Tolerance Memory Safety:** Always check allocation returns (`malloc`/`calloc` != NULL), validate bounds, free every resource deterministically, ensure zero leaks under ASan.
3. **Almaz Organism Protection:** Never overwrite Almaz cognitive organs during synchronization. Always use `scripts/sync_almaz.sh`.
4. **Autonomous Multi-Step Execution:** When given a mission, execute all steps continuously using tool calls without intermediate conversational pauses until fully tested and verified.

---

### 🎯 7. Next Session Immediate Action Plan
1. **Verification Quick-Check:** Run `make test` in Belya and `make test` in Almaz to verify baseline health.
2. **Channel Token Onboarding:** Onboard Discord Bot Token and Slack App Tokens into environment secrets for live omnichannel routing.
3. **Containerized Execution Validation:** Use the `exec_backend` tool to execute untrusted code in isolated Docker containers.
4. **VPS Telemetry & Organism Monitoring:** Check live VPS status (`ssh root@187.124.2.26 systemctl status belya almaz`), inspect autonomic idle cycles and emotional appraisal stabilization.

Please confirm receipt of this handoff, run the verification quick-check, and report readiness for our next objectives!
```

---

## 📋 Quick Commands Reference for New Session

| Action | Command | Working Directory |
| :--- | :--- | :--- |
| **Verify Belya Unit Tests** | `make test` | `/Users/mohamedfathy/Desktop/Privacy C/C Harness and Agent` |
| **Verify Almaz Unit Tests** | `make test` | `/Users/mohamedfathy/Desktop/Privacy C/Almaz` |
| **Verify Almaz Holdout Tests** | `make test-holdout` | `/Users/mohamedfathy/Desktop/Privacy C/Almaz` |
| **Verify 6-Arena Benchmarks** | `make arena` | Either repo |
| **Verify Exercism Suite** | `make exercism` | Either repo |
| **Run Full 7-Gate CI/CD** | `./scripts/verify_phase.sh 7` | Either repo |
| **Sync Almaz with Belya** | `./scripts/sync_almaz.sh` | `/Users/mohamedfathy/Desktop/Privacy C/C Harness and Agent` |
| **Sync to Google Drive** | `make gdrive` | Either repo |
| **Check VPS Status** | `ssh root@187.124.2.26 "systemctl status belya almaz"` | Terminal |
