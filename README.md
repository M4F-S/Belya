# Belya — Bare-Metal Autonomous AI Software Engineer & SOTA Dominance Harness

[![Release](https://img.shields.io/badge/Release-v7.4.0-blue.svg)](https://github.com/M4F-S/Belya/releases/tag/v7.4.0)
[![License](https://img.shields.io/badge/License-Apache_2.0-green.svg)](LICENSE)
[![Standard](https://img.shields.io/badge/Standard-ISO%2FIEC_9899:1999_(C99)_|_POSIX.1--2008-orange.svg)]()
[![Tests](https://img.shields.io/badge/Unit_Tests-74%2F74_Passed_(100%25)-brightgreen.svg)]()
[![Memory_Safety](https://img.shields.io/badge/Memory_Safety-0_Leaks_|_ASan_|_UBSan_Clean-success.svg)]()
[![Sandbox](https://img.shields.io/badge/Sandboxing-Landlock_LSM_|_Path_Jailing-red.svg)]()
[![Protocols](https://img.shields.io/badge/Protocols-DeepSeek_|_Anthropic_|_OpenAI_|_Hermes--3_|_Falcon--3-blueviolet.svg)]()
[![Multi--Agent](https://img.shields.io/badge/Sovereign_Company-21_Autonomous_Roles-informational.svg)]()
[![RFC_8615](https://img.shields.io/badge/Skills_Discovery-RFC_8615_Compliant-success.svg)]()
[![Memory](https://img.shields.io/badge/Memory_Footprint-<8.0MB_Peak_RSS-purple.svg)]()
[![Cold_Start](https://img.shields.io/badge/Cold_Start-<1.5ms-yellow.svg)]()

> **Belya** is a high-performance, bare-metal autonomous AI software engineering core and agency orchestrator engineered from first principles in **pure C99 (POSIX.1-2008)**. Designed to replace resource-heavy Node.js and Python agent harnesses (such as Claude Code and Hermes Agent), Belya pairs sub-millisecond cold start times with deterministic AST symbol indexing, speculative shadow git worktrees, Linux Landlock LSM kernel isolation, native stdio LSP integration, a persistent SQLite WAL subagent engine with authored-token budget economics, a 21-role autonomous SDLC company mesh, and RFC 8615 discovery.

---

## Table of Contents
- [Executive Overview](#executive-overview)
- [Comparative Benchmark Matrix](#comparative-benchmark-matrix)
- [System Architecture](#system-architecture)
  - [1. Dual VTable Structural Isolation (Router vs. Worker)](#1-dual-vtable-structural-isolation-router-vs-worker)
  - [2. Persistent Subagent Engine (Economics, Durability & Pool)](#2-persistent-subagent-engine-economics-durability--pool)
  - [3. Defense-in-Depth Sandboxing & Signal Safety](#3-defense-in-depth-sandboxing--signal-safety)
  - [4. Native Stdio LSP JSON-RPC Client & Compiler Watchdog](#4-native-stdio-lsp-json-rpc-client--compiler-watchdog)
  - [5. Speculative Shadow Git Worktree Candidate Dispatcher](#5-speculative-shadow-git-worktree-candidate-dispatcher)
  - [6. Omnichannel Gateway & Stdin-Piped Remote Execution](#6-omnichannel-gateway--stdin-piped-remote-execution)
  - [7. Interactive Terminal TUI & Side-by-Side Diff Inspector](#7-interactive-terminal-tui--side-by-side-diff-inspector)
  - [8. Closed-Loop Skill Evolution & RFC 8615 Discovery](#8-closed-loop-skill-evolution--rfc-8615-discovery)
  - [9. Multi-Format Trajectory Distillation (OpenAI, ShareGPT, Hermes DPO)](#9-multi-format-trajectory-distillation-openai-sharegpt-hermes-dpo)
  - [10. Sovereign UAE Falcon Presets & Air-Gapped PDPL Operation](#10-sovereign-uae-falcon-presets--air-gapped-pdpl-operation)
- [Sovereign Multi-Agent Enterprise Roster (21 SDLC Roles)](#sovereign-multi-agent-enterprise-roster-21-sdlc-roles)
- [Native Tool Suite (51 Registered Tools)](#native-tool-suite-51-registered-tools)
- [Installation & Verification](#installation--verification)
  - [System Prerequisites](#system-prerequisites)
  - [Building from Source](#building-from-source)
  - [7-Gate Verification Battery](#7-gate-verification-battery)
- [Operating Modes](#operating-modes)
- [CLI Options & Slash Controls Reference](#cli-options--slash-controls-reference)
- [Production Deployment & Defense-in-Depth Administration](#production-deployment--defense-in-depth-administration)
- [Synergy with Twin Sibling Almaz](#synergy-with-twin-sibling-almaz)
- [License & Provenance](#license--provenance)

---

## Executive Overview

Modern enterprise AI coding agents suffer from fundamental systems engineering flaws:
1. **Massive Runtime Overhead:** Interpreted runtimes (Node.js/React Ink, Python/asyncio) consume between 350 MB and 1.8 GB RSS, introducing garbage collection pauses, multi-second cold boot latencies, and bloated deployment footprints.
2. **Context Window Contamination:** Raw tool outputs, multi-kilobyte compiler error traces, and redundant system prompts pollute reasoning contexts, inflating costs and inducing context-window drift.
3. **Unchecked Destructive Code Mutation:** Directly mutating working directory files risks leaving dirty, half-broken states upon step exhaustion or unexpected process termination.
4. **Cloud Egress & Data Privacy Violations:** Continuously streaming proprietary code, internal schema, and credentials to multi-tenant offshore cloud APIs breaches data sovereignty mandates (e.g., UAE Federal Decree-Law No. 45 PDPL and Dubai Data Law No. 26).

Belya addresses these problems directly at the hardware and operating system boundary:
- **Bare-Metal Performance:** Compiles to a compact static binary with `<1.5ms` cold boot latency and under `8.0MB RSS` baseline memory.
- **Dual VTable Security:** Physical C function pointer tables prevent orchestration routines from executing destructive shell operations or directly modifying code.
- **Persistent Subagent Engine:** Subagents run in SQLite WAL mode with **Option A Authored-Token Economics** (prompt re-sends are not billed to child work budgets), automatic in-memory compaction above 16k tokens, bounded background thread execution (`wait=false`), and graceful partial status reporting with forced wrap-up summaries.
- **Kernel-Level Sandboxing:** Linux Landlock LSM confines filesystem access strictly to the workspace root, coupled with path-canonicalization jailing and SIGPIPE-safe atomic pipe operations.
- **Speculative Shadow Worktrees:** Simultaneously evaluates competing implementation strategies in parallel git worktrees, atomically merging the highest-scoring candidate and pruning non-winning branches.
- **Zero-Dependency Native LSP:** Pure C99 JSON-RPC 2.0 communication over standard pipes with language servers (`clangd`, `pyright`, `rust-analyzer`, `gopls`).

---

## Comparative Benchmark Matrix

| Metric / Dimension | Belya (Pure C99) | Claude Code (Node/TS) | Hermes Agent (Python) | Devin / Cognition (Cloud) |
| :--- | :---: | :---: | :---: | :---: |
| **Language & Standard** | **Pure C99 (POSIX.1-2008)** | TypeScript / Node.js 20+ | Python 3.11 / asyncio | Proprietary VM / Python |
| **Cold Boot Latency** | **< 1.5 ms** | ~850 ms | ~1,200 ms | ~15,000 ms (Cloud boot) |
| **Baseline RSS Memory** | **< 8.0 MB** | 350 MB – 850 MB | 450 MB – 1.8 GB | N/A (Hosted VM) |
| **External Dependencies** | **Zero** (`libc`, `libcurl`, `sqlite3`) | >1,400 npm packages | >120 pip packages | Massive Cloud Infra |
| **Kernel Sandboxing** | **Linux Landlock LSM + Jailing** | Userland cwd checks | Docker socket required | Cloud container boundary |
| **Orchestration Isolation**| **Dual VTable C Function Pointers** | System prompt instructions | System prompt instructions | Cloud container boundary |
| **Subagent Architecture** | **Persistent SQLite WAL + Pool** | In-process subshells | In-process sequential | MicroVM workers |
| **Subagent Economics** | **Authored-Token Accounting** | Full prompt accumulation | Full prompt accumulation | Metered VM credits |
| **Worktree Exploration** | **Speculative Multi-Branch** | Single working tree | Sequential execution | Single branch sandbox |
| **Language Server Protocol**| **Native Stdio JSON-RPC 2.0** | Plugin / External Extension | Tool-call workaround | Remote indexer |
| **Skills Discovery** | **RFC 8615 (`agentskills.io`)** | `CLAUDE.md` only | `SKILL.md` (Native) | Proprietary prompts |
| **Trajectory Distillation**| **OpenAI, ShareGPT, Hermes DPO** | None (Transient logs) | Atropos JSONL | Proprietary logs |
| **Inter-Agent Mesh IPC** | **POSIX Unix Domain Socket** | None | HTTP REST Broker | Proprietary RPC |
| **Kernel Tamper Locks** | **`chattr +i` Immutable Daemon** | Not Supported | Not Supported | Managed Cloud Sandbox |

---

## System Architecture

```
                                  ┌────────────────────────────────────────┐
                                  │      Operator / Omnichannel Client     │
                                  │  (ANSI Raw TUI / Telegram Bot / Slack) │
                                  └───────────────────┬────────────────────┘
                                                      │
                                                      ▼
                                  ┌────────────────────────────────────────┐
                                  │           Belya Router Core            │
                                  │   (Dual VTable Structural Isolation)   │
                                  └───────────────────┬────────────────────┘
                                                      │
         ┌────────────────────────┬───────────────────┼───────────────────┬────────────────────────┐
         │                        │                   │                   │                        │
         ▼                        ▼                   ▼                   ▼                        ▼
┌──────────────────┐    ┌──────────────────┐ ┌─────────────────┐ ┌─────────────────┐    ┌──────────────────┐
│  Subagent Engine │    │ Speculative Tree │ │ Native Stdio LSP│ │ Kernel Sandbox  │    │ Omnichannel Gate │
│ • SQLite WAL DB  │    │ • 3 Parallel WT  │ │ • clangd/pyright│ │ • Landlock LSM  │    │ • Telegram Daemon│
│ • Authored Token │    │ • Def-in-Depth   │ │ • Stdio JSON-RPC│ │ • Path Jailing  │    │ • Discord v10    │
│ • Compaction >16k│    │ • Auto Composite │ │ • Preflight Hook│ │ • Safe Pipe I/O │    │ • Slack Sockets  │
│ • Bounded Pool   │    │ • Atomic Merge   │ │ • Zero-Guess Fix│ │ • chattr +i Lock│    │ • Stdin Exec-Pipe│
└──────────────────┘    └──────────────────┘ └─────────────────┘ └─────────────────┘    └──────────────────┘
                                                      │
                                                      ▼
                                  ┌────────────────────────────────────────┐
                                  │     Inter-Agent Mesh Socket IPC        │
                                  │       /tmp/belya_almaz_mesh.sock       │
                                  └───────────────────┬────────────────────┘
                                                      │
                                                      ▼
                                  ┌────────────────────────────────────────┐
                                  │   Almaz Cognitive Twin Organism        │
                                  │  (FadeMem Biological Decay, VIGIL)     │
                                  └────────────────────────────────────────┘
```

### 1. Dual VTable Structural Isolation (Router vs. Worker)
Separation of high-level task planning and code mutation is enforced through separate C function pointer tables:
- **Router VTable:** Provides planning, read-only inspection, AST navigation, and task dispatching. Destructive mutation tools (`bash`, `write_file`, `edit_file`, `apply_patch`) are initialized to `NULL`, preventing prompt injection attacks from escaping orchestration bounds.
- **Worker VTable:** Explicitly provisioned only within isolated, ephemeral worktrees to execute bounded changes and unit test runs.

### 2. Persistent Subagent Engine (Economics, Durability & Pool)
Belya's subagent architecture (Phases 1a–1d) provides robust multi-agent concurrency:
- **Authored-Token Economics (Option A):** Subagent budgets meter authored work (`spent_tokens = completion_tokens + bounded_tool_output`). Cumulative prompt re-transmissions are recorded in `billed_tokens` for provider cost audits but never trigger premature step eviction.
- **Child In-Memory Compaction:** When child prompt size exceeds 16,000 tokens across iterative tool loops, `belya_agent_compact_history()` prunes intermediate history while retaining the system prompt and the last 8 messages. The full audit trail remains preserved in `subagent_messages`.
- **Graceful Partial Result Delivery:** Turn exhaustion (`turns <= 0`) or budget depletion transitions status to `partial` and executes one wrap-up summary turn (`belya_agent_step_forced_text`). Parent collectors receive rich status envelopes rather than discarded work.
- **Bounded Background Worker Pool:** Background execution (`wait=false`) runs on a dedicated POSIX thread pool capped at `BELYA_SUBAGENT_BG_MAX = 8` concurrent workers with atomic refcounting.
- **Durability & Orphan Sweeping:** Child states persist in SQLite WAL mode. On process restart, orphaned in-flight rows are reclaimed or paused, and `/agents` offers real-time paged status monitoring.

### 3. Defense-in-Depth Sandboxing & Signal Safety
- **Linux Landlock LSM:** Confines process file descriptors to the repository root directory, preventing arbitrary filesystem access even under compromised shell execution.
- **Path Jailing (`is_path_jailed`):** Realpath canonicalization blocks directory traversal attacks (`../../`) and symlink escapes across all file modification tools.
- **SIGPIPE-Safe Atomic Pipe Writes (`belya_safe_pipe_write`):** Masks `SIGPIPE` around child standard input writes and drains pending signals via `sigtimedwait()` (Linux) and `sigwait()` (Darwin/macOS), preventing broken child reader processes from taking down the parent daemon.
- **Kernel Immutable Locks:** Production binaries are locked via `chattr +i` and run under systemd without `CAP_LINUX_IMMUTABLE`, preventing binary replacement during active execution.

### 4. Native Stdio LSP JSON-RPC Client & Compiler Watchdog
- A native, zero-dependency C99 JSON-RPC 2.0 client communicates over standard pipes with language servers (`clangd`, `pyright`, `rust-analyzer`, `gopls`).
- Diagnostic outputs automatically feed `belya_harness_preflight_check()`, enabling instant compiler auto-healing loops before committing changes.

### 5. Speculative Shadow Git Worktree Candidate Dispatcher
- Dispatches up to 3 candidate implementation strategies simultaneously in isolated shadow git worktrees (`.belya/worktrees/*`):
  - *Candidate 0:* Surgical Minimal (smallest line diff footprint).
  - *Candidate 1:* Defensive & Robust (explicit bounds checks and error handling).
  - *Candidate 2:* Idiomatic Pattern (standard structural conventions).
- Solutions are scored deterministically:
  $$\text{Score} = (\text{Tests Passed} \times 10000) - (\text{Diff Churn} \times 2)$$
- The top-ranking patch is squash-merged atomically; non-winning branches are purged.

### 6. Omnichannel Gateway & Stdin-Piped Remote Execution
- **Polymorphic Omnichannel Engine (`belya_channel.c`):** Serves 24/7 across Telegram (long-polling daemon with markdown rendering and session resumption), Discord Gateway v10, and Slack (Socket Mode & Webhooks with <100ms URL challenge responses).
- **Stdin-Piped Remote Backend (`exec_backend`):** Streams command scripts into remote Docker containers or SSH hosts over standard input pipes, eliminating shell-escaping vulnerabilities.

### 7. Interactive Terminal TUI & Side-by-Side Diff Inspector
- **Single-Keystroke Modal (`tui_interactive_approve`):** Instant tool approval (`[Y]` Apply, `[N]` Reject, `[E]` Edit args, `[A]` Always allow, `[D]` Split diff).
- **Split Side-by-Side Diff Inspector:** Dual-column terminal diff viewer with dynamic width detection and ANSI syntax highlighting.
- **Live Token HUD:** Displays upstream model status, prompt/completion tokens, cache hit efficiency, latency, and resident set size (RSS).

### 8. Closed-Loop Skill Evolution & RFC 8615 Discovery
- **Closed-Loop Auto-Synthesis:** Multi-turn problem resolutions automatically distill into modular skills (`skills/<slug>/SKILL.md`) with YAML frontmatter.
- **RFC 8615 Discovery Standard:** CLI flag `--export-skills` exports the catalog to `.well-known/agent-skills/index.json` compliant with the `agentskills.io` standard without opening network ports.

### 9. Multi-Format Trajectory Distillation (OpenAI, ShareGPT, Hermes DPO)
- **OpenAI Fine-Tune:** Exports conversational sessions in JSONL via `/export [id] [file.jsonl]` or `belya_agent_export_trajectory()`.
- **ShareGPT / Hermes Format:** CLI flag `--export-sharegpt <file.jsonl>` or `/export-sharegpt` for conversational fine-tuning.
- **Hermes DPO Preference Format:** CLI flag `--export-dpo <file.jsonl>` or `/export-dpo`. Mines successful recovery turns (`chosen`) against failed tool attempts (`rejected`) to produce pairs for Direct Preference Optimization.

### 10. Sovereign UAE Falcon Presets & Air-Gapped PDPL Operation
- Optimized for sovereign on-premise execution using UAE Technology Innovation Institute (TII) open weights via `--preset`:
  - `falcon` / `falcon3`: Falcon-3 7B/10B Instruct running on local Ollama (`http://localhost:11434/v1`).
  - `falcon-vllm`: High-throughput local vLLM serving (`http://localhost:8000/v1`) with continuous batching.
  - `falcon2`: Falcon-2 11B multimodal foundation model.
  - `airgap`: Strictly local air-gapped execution complying with UAE Federal Decree-Law No. 45 (PDPL).
- Includes DeepSeek `<think>` CoT scavenger extraction to isolate functional tool payloads.

---

## Sovereign Multi-Agent Enterprise Roster (21 SDLC Roles)

Belya incorporates 21 autonomous SDLC roles orchestrated via `belya_company.c`:

| Role Key | Specialization | Default Turns |
| :--- | :--- | :---: |
| `account_executive` | Commercial scoping, statement of work, milestone definitions | 6 |
| `solutions_sales_engineer` | Technical architecture pre-sales feasibility audit | 8 |
| `customer_success_manager` | Client health monitoring, SLA adherence (99.95%) | 6 |
| `product_manager` | PRD authoring, SQLite Task Board backlog orchestration | 8 |
| `ui_ux_designer` | Design tokens, wireframes, WCAG 2.1 AA accessibility specs | 8 |
| `architect` | Low-level systems design, C99 interface contracts, zero-leak RFCs | 8 |
| `data_engineer` | SQLite schema provisioning, WAL mode, query optimization | 10 |
| `backend_engineer` | C99 microservice handlers, bounds-checked REST API endpoints | 10 |
| `frontend_engineer` | Responsive client components, state machine assembly | 10 |
| `fullstack_engineer` | Client-server contract parity verification in ephemeral worktrees | 12 |
| `cybersecurity_engineer` | Mythos variant security sweeps (0 SQLi, 0 overflows, 0 format bugs) | 8 |
| `qa_test_engineer` | Full test battery execution under AddressSanitizer/UBSan | 10 |
| `devops_cloud_engineer` | Systemd service units, multi-stage Dockerfiles, immutable deployment | 10 |
| `technical_writer` | API documentation, developer runbooks, operator guides | 8 |
| `delivery_monitor` | Delivery assurance, stage telemetry, executive retrospectives | 8 |
| `capability_evolver` | Agent self-telemetry, cognitive health, skill synthesis | 10 |
| `triage` | Chief-of-staff triage layer, fast conversational routing | 6 |
| `builder` / `reviewer` / `tester` | Implementation, peer code review, and unit testing | 8–10 |

---

## Native Tool Suite (51 Registered Tools)

All 51 tools are implemented directly in pure C99 and registered in `belya_harness.c`:

| Category | Count | Tools |
| :--- | :---: | :--- |
| **Filesystem & Editing** | 7 | `read_file`, `write_file`, `edit_file`, `edit_notebook`, `apply_patch`, `list_dir`, `search_files` |
| **Execution & Sandboxing** | 4 | `bash`, `manage_task`, `exec_backend`, `define_tool` |
| **Subagents & Agency Mesh**| 8 | `spawn_subagent`, `check_subagent`, `collect_subagent`, `cancel_subagent`, `resume_subagent`, `dispatch_agent`, `dispatch_worker_task`, `mesh_collaborate` |
| **Code Graph, AST & LSP** | 7 | `lsp_definition`, `lsp_references`, `lsp_query`, `goto_definition`, `find_callers`, `ast_slice`, `query_ast_graph` |
| **Memory & Skills** | 5 | `save_memory`, `recall_memory`, `save_skill`, `recall_skill`, `recall_conversation` |
| **Tasks, Planning & Cron** | 8 | `task_create`, `task_get`, `task_list`, `task_update`, `cron_create`, `cron_list`, `cron_tick`, `enter_plan_mode`, `exit_plan_mode` |
| **Web & MCP Integration** | 6 | `web_search`, `web_fetch`, `fetch_url`, `mcp_call`, `synthesize_mcp_server`, `run_test_cycle` |
| **Operator & Security** | 6 | `ask_user`, `audit_security_variants`, `git_status`, `git_diff` |

---

## Installation & Verification

### System Prerequisites
- GCC >= 9.0 or Clang >= 11.0 (C99 standard support)
- POSIX system libraries (`libcurl`, `sqlite3`, `pthread`, `libm`)
- Git >= 2.25

**Debian / Ubuntu Installation:**
```bash
sudo apt-get update && sudo apt-get install -y build-essential libcurl4-openssl-dev libsqlite3-dev git
```

**macOS (Darwin) Installation:**
```bash
brew install curl sqlite
```

### Building from Source
```bash
git clone https://github.com/M4F-S/Belya.git
cd Belya
make clean && make -j4
```
The compiled binary will be located at `build/belya`.

### 7-Gate Verification Battery
Belya enforces a rigorous 7-gate verification pipeline:
```bash
# Gate 1 & 2: Primary unit verification battery (74/74 passed - 100% + MCP selftests)
make test

# Gate 3: Verification of all targets & simulated environments
make verify

# Gate 4: Memory safety audit under AddressSanitizer and UndefinedBehaviorSanitizer
make test_asan

# Gate 5: Stripped binary footprint check (< 350 KB micro-core)
./scripts/verify_phase.sh

# Gate 6: Task benchmark suite (13 autonomous coding missions)
make bench

# Gate 7: Architecture drift check
make drift
```

---

## Operating Modes

### Mode 1: Interactive Terminal CLI
```bash
./build/belya
```

### Mode 2: 24/7 Production Telegram Daemon
```bash
./build/belya --telegram
```

### Mode 3: Headless Autonomous SWE Mission
```bash
./build/belya --prompt "Fix buffer bounds check in parser.c, verify tests under AddressSanitizer, and commit."
```

### Mode 4: Enterprise Company Simulation (21 Roles)
```bash
./build/belya --company "Audit authentication flow, design WCAG compliant frontend, and write test battery."
```

### Mode 5: Dataset & Skills Exporters
```bash
# Export RFC 8615 skills catalogue
./build/belya --export-skills

# Export active session to ShareGPT / Hermes format
./build/belya --export-sharegpt session_sharegpt.jsonl

# Export active session to Hermes DPO preference format
./build/belya --export-dpo session_dpo.jsonl
```

---

## CLI Options & Slash Controls Reference

### Command-Line Arguments
| Option | Description |
| :--- | :--- |
| `-h`, `--help` | Display command-line usage and options |
| `-P`, `--preset <name>` | Load model preset (`falcon`, `falcon-vllm`, `falcon2`, `qwen`, `deepseek`, `airgap`) |
| `--presets` | List all available model presets and target endpoints |
| `-t`, `--telegram`, `-o`, `--omnichannel` | Run in 24/7 daemon mode (Telegram long-polling & Omnichannel) |
| `-p`, `--prompt`, `--headless <text>` | Run in non-interactive headless mission mode |
| `-r`, `--resume <session_id>` | Resume an existing conversation session from SQLite |
| `-c`, `--company [task]` | Run in Sovereign Enterprise Company multi-role mode |
| `-a`, `--agency [task]` | Run in Belya Agency multi-agent pipeline mode |
| `-d`, `--docker <container>` | Direct execution into a target Docker container via stdin pipe |
| `-S`, `--stage`, `--telemetry` | Enable cognitive stage telemetry and event logging |
| `--probe` | Run gateway health and authorization preflight checks, then exit |
| `--no-probe` | Skip gateway preflight checks during startup |
| `--stats-json <path>` | Write execution metrics and statistics to a JSON file |
| `--export-skills` | Generate RFC 8615 `.well-known/agent-skills/` catalogue |
| `--export-sharegpt <path>` | Export session history in ShareGPT / Hermes JSONL format |
| `--export-dpo <path>` | Export session history in Hermes DPO preference format |

### Interactive & Telegram Slash Commands
| Command | Action |
| :--- | :--- |
| `/help`, `/start` | Display command catalog and active capabilities |
| `/status` | View system health, RSS memory, token consumption, and gateway latency |
| `/agents [page]` | View persistent subagent children, token budgets, and execution states |
| `/diff [path] [--side]` | View ANSI colorized git diff or split side-by-side terminal diff |
| `/tools` | List all registered tools and active permission tiers |
| `/rules` | View ingested repository rules (`.agentrules`, `BELYARULES.md`) |
| `/timeline [N]` | View recent cognitive timeline events |
| `/sessions` | List all checkpointed conversation sessions in SQLite |
| `/save [id]` | Checkpoint conversation state to SQLite database |
| `/resume <id>` | Restore conversation session by identifier |
| `/skills [query]` | List or search saved procedural skills |
| `/checkpoint [id]` | Create a Git and context snapshot |
| `/rollback [id]` | Instant rollback of files and dialogue to a previous checkpoint |
| `/export [id]` | Export trajectory in OpenAI JSONL format |
| `/export-sharegpt` | Export trajectory in ShareGPT / Hermes format |
| `/export-dpo` | Export trajectory in Hermes DPO preference format |
| `/cache` | View prompt cache hit economics and token savings |
| `/reflect` | Distill recent trajectory into reusable SQLite FTS5 skill |
| `/compact [N]` | Prune older messages, keeping N recent (default: 10) |
| `/memory [query]` | Search SQLite persistent memory directly via FTS5 BM25 |
| `/model <name>` | Dynamically switch the upstream LLM model |
| `/openrouter [model]` | Switch provider to OpenRouter |
| `/ollama [model]` | Switch provider to local Ollama |
| `/key <api_key>` | Dynamically update active provider API key |
| `/doctor` | Run deep environment, compiler, and git diagnostics |
| `/probe` | Perform pre-flight health probe on upstream model endpoints |
| `/cost` | View session token accounting and estimated provider cost |
| `/turbo [on\|off]` | Toggle auto-approval Turbo mode for tool execution |
| `/clear`, `/reset` | Clear active conversation history (preserves system prompt) |
| `/exit`, `/quit` | Restore terminal raw-mode settings and exit cleanly |

---

## Production Deployment & Defense-in-Depth Administration

Belya operates in production on remote VPS hosts as an immutable systemd service.

### Systemd Service Configuration (`/etc/systemd/system/belya.service`)
```ini
[Unit]
Description=Belya Autonomous AI Agent & Telegram Daemon (Pure C99)
After=network.target

[Service]
Type=simple
User=belya
Group=belya
WorkingDirectory=/opt/belya
EnvironmentFile=/opt/belya/.env
ExecStart=/opt/belya/belya --telegram
Restart=always
RestartSec=5s
LimitNOFILE=65536
CapabilityBoundingSet=~CAP_LINUX_IMMUTABLE

[Install]
WantedBy=multi-user.target
```

### Atomic Deployment & Kernel Tamper Locks
```bash
# Clean build, test battery pass, and atomic installation
make release

# Verify kernel immutable attributes on active binaries
lsattr /opt/charness/belya /opt/charness/belya.lkg
# Expected: ----i---------e------- /opt/charness/belya
```

---

## Synergy with Twin Sibling Almaz

Belya and [Almaz](https://github.com/M4F-S/Almaz) operate as a synchronized dual-agent pair sharing the high-performance C99 micro-kernel:
- **Belya (Deterministic Core):** Engineered for deterministic software engineering, compilation guarantees, speculative worktrees, and enterprise multi-agent company orchestration.
- **Almaz (Cognitive Organism):** Engineered for autopoietic survival, emotional appraisal (**VIGIL**), biological memory decay (**FadeMem**), dual-stream memory routing (**CORAL**), and automated crash recovery under an external watchdog (`almaz-watchdog`).
- **Inter-Agent Mesh Socket:** Both daemons communicate directly over `/tmp/belya_almaz_mesh.sock` to pair-program across complex tasks with sub-millisecond consultation latency.

---

## License & Provenance

Licensed under the **Apache License, Version 2.0**. See the [LICENSE](LICENSE) file for complete terms.

**Project Lead & Maintainer:** [M4F-S](https://github.com/M4F-S)  
**Host Environment:** Linux (x86-64 / ARM64) & macOS (Darwin POSIX)  
**Architecture:** Pure C99 POSIX.1-2008 | SOTA Dominance Release v7.4.0
