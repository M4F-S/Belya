# Belya — Bare-Metal Autonomous AI Software Engineer & SOTA Dominance Harness

[![Release](https://img.shields.io/badge/Release-v7.4.0-blue.svg)](https://github.com/M4F-S/Belya/releases/tag/v7.4.0)
[![License](https://img.shields.io/badge/License-Apache_2.0-green.svg)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C99_POSIX-orange.svg)]()
[![Tests](https://img.shields.io/badge/Unit_Tests-54%2F54_Passed_(100%25)-brightgreen.svg)]()
[![Operation_Crucible](https://img.shields.io/badge/Operation_Crucible-100%25_Invariants_Intact-success.svg)]()
[![Memory_Safety](https://img.shields.io/badge/Memory_Safety-0_Leaks_ASan_UBSan_Clean-success.svg)]()
[![Protocols](https://img.shields.io/badge/Protocols-Anthropic_|_Hermes--3_|_DeepSeek_|_OpenAI-blueviolet.svg)]()
[![Multi--Agent](https://img.shields.io/badge/Sovereign_Company-21_Autonomous_Roles-informational.svg)]()
[![RFC_8615](https://img.shields.io/badge/Skills_Discovery-RFC_8615_Compliant-success.svg)]()
[![Memory](https://img.shields.io/badge/Memory_Footprint-<8.0MB_Peak_RSS-purple.svg)]()
[![Cold_Start](https://img.shields.io/badge/Cold_Start-<1.5ms-yellow.svg)]()

> **Belya** is a high-performance, bare-metal autonomous AI software engineering core implemented in pure C99. Designed to outperform heavy Node.js and Python agent harnesses (such as Claude Code and Hermes Agent), Belya combines sub-millisecond local startup with deterministic AST dependency slicing, speculative shadow worktrees, a Dual VTable security architecture, native stdio LSP integration, an interactive raw-mode terminal TUI, closed-loop skill evolution, and RFC 8615 discovery.

---

## Table of Contents
- [Executive Overview](#executive-overview)
- [Comparative Benchmark Matrix](#comparative-benchmark-matrix)
- [Core Architecture](#core-architecture)
  - [1. Dual VTable Structural Isolation (Router vs. Worker)](#1-dual-vtable-structural-isolation-router-vs-worker)
  - [2. Interactive Terminal TUI & Split Diff Inspector](#2-interactive-terminal-tui--split-diff-inspector)
  - [3. Native Stdio LSP JSON-RPC Client](#3-native-stdio-lsp-json-rpc-client)
  - [4. Speculative Multi-Worktree Candidate Dispatcher](#4-speculative-multi-worktree-candidate-dispatcher)
  - [5. Inter-Agent Mesh Socket IPC](#5-inter-agent-mesh-socket-ipc)
  - [6. Closed-Loop Skill Evolution & RFC 8615 Discovery](#6-closed-loop-skill-evolution--rfc-8615-discovery)
  - [7. Universal Rules Auto-Ingestion](#7-universal-rules-auto-ingestion)
  - [8. Multi-Format Trajectory Distillation (OpenAI, ShareGPT, Hermes DPO)](#8-multi-format-trajectory-distillation-openai-sharegpt-hermes-dpo)
  - [9. Jev Coprocessor & Discrete Decision Primitives](#9-jev-coprocessor--discrete-decision-primitives)
- [Sovereign Multi-Agent Enterprise Roster (21 SDLC Roles)](#sovereign-multi-agent-enterprise-roster-21-sdlc-roles)
- [Native Tool Suite (46 Built-In Tools)](#native-tool-suite-46-built-in-tools)
- [Installation & Quick Start](#installation--quick-start)
  - [Prerequisites](#prerequisites)
  - [Building from Source](#building-from-source)
  - [Verification Test Battery](#verification-test-battery)
- [Operating Modes](#operating-modes)
- [Command & Slash Controls Reference](#command--slash-controls-reference)
- [Production Deployment & Defense-in-Depth Administration](#production-deployment--defense-in-depth-administration)
- [Synergy with Twin Sibling Almaz](#synergy-with-twin-sibling-almaz)
- [License & Provenance](#license--provenance)

---

## Executive Overview

Standard AI coding agents incur significant architectural penalties:
1. **Runtime Overhead:** Heavy runtimes (Node.js, Python) consume 350 MB to 1.8 GB RSS, introducing garbage collection latency and high resource demands.
2. **Context Window Contamination:** Intermediate compiler traces and raw tool outputs flood context windows, degrading reasoning.
3. **Unchecked Code Mutation:** Directly editing working trees risks leaving unverified or half-broken changes in place.
4. **Agent Amnesia:** Without structured distillation, solutions to novel problems are lost between sessions.

Belya addresses these limitations directly in C99:
- **Bare-Metal Footprint:** Cold boot in `<1.5ms`, baseline memory under `8.0MB RSS`.
- **Dual VTable Security:** Enforced by physical C function pointers; the Router Agent cannot execute destructive shell commands or mutate repository files directly.
- **Speculative Shadow Worktrees:** Tests candidate solutions across parallel worktrees, automatically scoring and merging the cleanest working patch.
- **Native Stdio LSP Integration:** Communicates directly with language servers (`clangd`, `pyright`, `rust-analyzer`, `gopls`) over standard I/O pipes.
- **Closed-Loop Skill Evolution:** Multi-step troubleshooting workflows automatically distill into `skills/<slug>/SKILL.md` with RFC 8615 discovery.
- **Direct Dataset Distillation:** Exports sessions in OpenAI, ShareGPT, and Hermes DPO formats for fine-tuning local models.

---

## Comparative Benchmark Matrix

| Metric / Dimension | Belya (Pure C99) | Claude Code (Node/TS) | Hermes Agent (Python) | Devin / Cognition (Cloud) |
| :--- | :---: | :---: | :---: | :---: |
| **Language & Runtime** | **Pure C99 POSIX** | Node.js / React Ink | Python 3.11 / asyncio | Proprietary VM / Python |
| **Cold Boot Latency** | **<1.5 ms** | ~850 ms | ~1,200 ms | ~15,000 ms (Cloud boot) |
| **Baseline RSS Memory** | **<8.0 MB** | 350 MB – 850 MB | 450 MB – 1.8 GB | N/A (Hosted VM) |
| **External Dependencies** | **Zero** (libc, libcurl, sqlite3) | >1,400 npm packages | >120 pip packages | Massive Cloud Infra |
| **Router/Worker Isolation** | **Dual VTable Omission** | Prompt Instructions | Prompt Instructions | Cloud Sandbox Container |
| **Worktree Exploration** | **Speculative Multi-Branch** | Single Worktree | Sequential Execution | Single Branch Sandbox |
| **Language Server Protocol** | **Native Stdio JSON-RPC** | Plugin / External Extension | Tool-Call Workaround | Cloud Indexer |
| **Skills Standard** | **`agentskills.io` (RFC 8615)** | `CLAUDE.md` Only | `SKILL.md` (Native) | Proprietary Prompts |
| **Trajectory Distillation** | **OpenAI, ShareGPT, DPO** | None (Transient) | Atropos JSONL | Proprietary Logs |
| **Inter-Agent Mesh IPC** | **Unix Domain Socket** | None | HTTP Broker | Proprietary RPC |
| **Kernel Tamper Locks** | **`chattr +i` Immutable** | Not Supported | Not Supported | Managed Cloud Sandbox |

---

## Core Architecture

```
                                  ┌─────────────────────────────┐
                                  │      Operator Terminal      │
                                  │   (ANSI Raw-Mode TUI HUD)   │
                                  └──────────────┬──────────────┘
                                                 │
                                                 ▼
                                  ┌─────────────────────────────┐
                                  │      Belya Router Core      │
                                  │    (Dual VTable Isolation)  │
                                  └──────────────┬──────────────┘
                                                 │
                   ┌─────────────────────────────┼─────────────────────────────┐
                   │                             │                             │
                   ▼                             ▼                             ▼
     ┌───────────────────────────┐ ┌───────────────────────────┐ ┌───────────────────────────┐
     │ 1. Terminal TUI & Diff    │ │ 2. Native Stdio LSP       │ │ 3. Speculative Worktrees    │
     │ • Split Side-by-Side View │ │ • JSON-RPC stdio pipes    │ │ • Multi-Candidate Branches  │
     │ • Single-Key Approval     │ │ • clangd / pyright / etc. │ │ • Automated Scoring Matrix  │
     │ • VT100 Threaded Spinner  │ │ • Watchdog Diagnostic Hook│ │ • Clean Patch Atomic Merge  │
     │ • Live Token & RSS HUD    │ │ • Zero-Guess Auto-Healing │ │ • Silent Loser Worktree Drop│
     └───────────────────────────┘ └───────────────────────────┘ └───────────────────────────┘
                                                 │
                                                 ▼
                                  ┌─────────────────────────────┐
                                  │  4. Inter-Agent Mesh Socket │
                                  │    /tmp/belya_almaz_mesh    │
                                  │   Unix Domain Socket IPC    │
                                  └──────────────┬──────────────┘
                                                 │
                                                 ▼
                                  ┌─────────────────────────────┐
                                  │   Almaz Cognitive Organism  │
                                  │ (FadeMem, VIGIL, SARSI)     │
                                  └─────────────────────────────┘
```

### 1. Dual VTable Structural Isolation (Router vs. Worker)
Separation of orchestration and mutation via distinct C function pointer tables:
- **Router VTable:** Contains pointers for planning, reading, AST navigation, and task dispatch. Destructive function pointers (`bash`, `write_file`, `edit_file`, `apply_patch`) are initialized to `NULL`.
- **Worker VTable:** Enabled inside ephemeral worktrees for code mutation and test execution, isolated from orchestration controls.

### 2. Interactive Terminal TUI & Split Diff Inspector
- **Single-Keystroke Modal (`tui_interactive_approve`):** Instant tool approval without pressing Enter (`[Y]` Apply, `[N]` Reject, `[E]` Edit args, `[A]` Always allow, `[D]` Split diff).
- **Split Side-by-Side Diff View (`tui_render_side_by_side_diff`):** Dual-column diff rendering with dynamic terminal width detection.
- **VT100 Threaded Spinner:** Real-time animated spinner operating on a background POSIX thread.
- **Live Status HUD (`tui_render_token_hud`):** Displays upstream model, prompt/completion tokens, cache hit rates, latency, and RSS.

### 3. Native Stdio LSP JSON-RPC Client
- Implements a pure C99 JSON-RPC 2.0 client communicating over bidirectional pipes with language servers (`clangd`, `pyright`, `rust-analyzer`, `gopls`, `typescript-language-server`).
- Extracted diagnostics feed into `belya_harness_preflight_check()`, enabling instant auto-healing of syntax and type errors before committing changes.

### 4. Speculative Multi-Worktree Candidate Dispatcher
- Dispatches 1–3 candidate strategies simultaneously in parallel shadow git worktrees (`.belya/worktrees/*`):
  - *Candidate 0:* Surgical Minimal (smallest diff footprint).
  - *Candidate 1:* Defensive & Robust (explicit bounds checks, error handling).
  - *Candidate 2:* Idiomatic Pattern (idiomatic structural conventions).
- Deterministic composite scoring:
  $$\text{Score} = (\text{Tests Passed} \times 10000) - (\text{Diff Churn} \times 2)$$
- The highest-scoring candidate is squash-merged atomically; non-winning branches are purged silently.

### 5. Inter-Agent Mesh Socket IPC
- High-speed Unix domain socket IPC (`/tmp/belya_almaz_mesh.sock`).
- Enables Belya and Almaz to pair-program collaboratively with sub-millisecond consultation latency.

### 6. Closed-Loop Skill Evolution & RFC 8615 Discovery
- **Closed-Loop Auto-Synthesis:** Multi-step troubleshooting workflows automatically distill into `skills/<slug>/SKILL.md` with YAML frontmatter.
- **RFC 8615 Discovery:** The `--export-skills` CLI flag generates `.well-known/agent-skills/index.json` and mirrored `SKILL.md` documents without opening an incoming HTTP port.

### 7. Universal Rules Auto-Ingestion
On startup, Belya automatically searches and ingests repository rules in priority order:
$$\text{BELYARULES.md} \rightarrow \text{Belya.md} \rightarrow \text{ALMAZRULES.md} \rightarrow \text{Almaz.md} \rightarrow \text{.agentrules} \rightarrow \text{AGENTS.md} \rightarrow \text{GEMINI.md} \rightarrow \text{CLAUDE.md}$$

### 8. Multi-Format Trajectory Distillation (OpenAI, ShareGPT, Hermes DPO)
- **OpenAI Fine-Tune:** `/export [session_id] [file.jsonl]` or `belya_agent_export_trajectory()`.
- **ShareGPT / Hermes Format:** `/export-sharegpt` or `--export-sharegpt <file.jsonl>`.
- **Hermes DPO Preference Format:** `/export-dpo` or `--export-dpo <file.jsonl>`. Mines successful recovery turns (`chosen`) against initial failed tool attempts (`rejected`) to produce pairs for DPO training.

### 9. Jev Coprocessor & Discrete Decision Primitives
Fast deterministic decision heuristics coupled with coprocessor classification:
- `jev_decide_choice()`: Multi-label discrete categorization.
- `jev_decide_noul()`: Calibrated binary probability evaluation.
- `jev_decide_score()`: Ordinal scale probability distribution.
- Includes local offline fallbacks, maintaining 100% operation without cloud dependencies.

---

## Sovereign Multi-Agent Enterprise Roster (21 SDLC Roles)

Belya includes 21 built-in SDLC roles orchestrated via `belya_company.c`:

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

## Native Tool Suite (46 Built-In Tools)

| Category | Native Tools |
| :--- | :--- |
| **Filesystem & Code Editing** | `read_file`, `write_file`, `edit_file`, `apply_patch`, `patch_file`, `list_dir`, `search_files`, `ripgrep`, `file_outline` |
| **Execution & Sandbox** | `bash`, `pty_exec`, `run_test_cycle`, `enter_worktree`, `leave_worktree`, `plan_mode`, `define_tool` |
| **Language Server & AST** | `lsp_definition`, `lsp_references`, `lsp_query`, `ast_slice`, `query_ast_graph`, `generate_repo_map` |
| **SuperAgent Orchestration** | `dispatch_worker_task`, `dispatch_agent`, `mesh_collaborate`, `ask_user`, `audit_security_variants` |
| **Memory & Skills** | `save_memory`, `recall_memory`, `search_memory`, `save_skill`, `search_skills`, `git_checkpoint`, `git_rollback` |
| **Networking & MCP** | `fetch_url`, `rest_call`, `mcp_call`, `synthesize_mcp_server`, `gateway_probe` |

---

## Installation & Quick Start

### Prerequisites
- GCC >= 9.0 or Clang >= 11.0 (C99 standard support)
- POSIX libraries (`libcurl`, `sqlite3`, `pthread`)
- Git >= 2.25

On Debian / Ubuntu:
```bash
sudo apt-get update && sudo apt-get install -y build-essential libcurl4-openssl-dev libsqlite3-dev git
```

### Building from Source
```bash
git clone https://github.com/M4F-S/Belya.git
cd Belya
make clean && make -j4
```
The compiled binary is placed at `build/belya`.

### Verification Test Battery
```bash
# Primary unit verification battery (54 tests + 4 MCP HTTP tests)
make test

# Memory safety audit under AddressSanitizer and UndefinedBehaviorSanitizer
make test_asan

# Operation Crucible adversarial breaking & penetration battery
make test-crucible-asan
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
./build/belya --prompt "Fix buffer overflow in parser.c, verify tests under AddressSanitizer, and commit."
```

### Mode 4: Trajectory & Skills Exporters
```bash
# Export RFC 8615 skills catalogue
./build/belya --export-skills

# Export active session to ShareGPT / Hermes format
./build/belya --export-sharegpt session_sharegpt.jsonl

# Export active session to Hermes DPO preference format
./build/belya --export-dpo session_dpo.jsonl
```

---

## Command & Slash Controls Reference

| Command | Action |
| :--- | :--- |
| `/help` | Displays command catalog and active capabilities |
| `/status` | Shows system health, RSS memory, token consumption, and gateway latency |
| `/diff` | Displays colored ANSI diff of uncommitted workspace changes |
| `/diff --side` | Displays interactive side-by-side terminal split diff |
| `/hud` | Toggles live token, latency, and RSS HUD status bar |
| `/mesh [msg]` | Sends an IPC consultation request to Almaz (`/tmp/belya_almaz_mesh.sock`) |
| `/export [id]` | Exports trajectory in OpenAI JSONL format |
| `/export-sharegpt` | Exports trajectory in ShareGPT / Hermes format |
| `/export-dpo` | Exports trajectory in Hermes DPO preference format |
| `/checkpoint [id]` | Creates a Git & conversation context snapshot |
| `/rollback [id]` | Reverts workspace files and dialogue to a previous checkpoint |
| `/cache` | Displays prompt cache statistics and hit rates |
| `/reflect` | Distills recent trajectory into a reusable SQLite FTS5 skill |
| `/doctor` | Runs deep environment and gateway diagnostics |
| `/quit` / `/exit` | Restores terminal mode and exits cleanly |

---

## Production Deployment & Defense-in-Depth Administration

Belya runs in production on remote VPS hosts as an immutable systemd service.

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
# Deploy latest verified build (handled by Makefile)
make deploy

# Verify kernel immutable attributes
lsattr /opt/charness/belya /opt/charness/belya.lkg
```

---

## Synergy with Twin Sibling Almaz

Belya and [Almaz](https://github.com/M4F-S/Almaz) operate as a synchronized dual-agent pair:
- **Belya (Deterministic Core):** Engineered for deterministic software engineering, compilation guarantees, unit test generation, and enterprise multi-agent company orchestration.
- **Almaz (Cognitive Organism):** Engineered for autopoietic survival, emotional appraisal (**VIGIL**), biological memory decay (**FadeMem**), dual-stream memory routing (**CORAL**), and automated crash recovery under an external watchdog (`almaz-watchdog`) with kernel immutable tamper locks (`chattr +i`).
- **Inter-Agent Mesh:** Both communicate directly over `/tmp/belya_almaz_mesh.sock` to pair-program across complex tasks.

---

## License & Provenance

Licensed under the **Apache License, Version 2.0**. See the [LICENSE](LICENSE) file for details.

**Maintained by:** [M4F-S](https://github.com/M4F-S)  
**Production Host:** `srv1412364` (`187.124.2.26`)  
**Architecture:** Pure C99 POSIX | SOTA Dominance Release v7.4.0
