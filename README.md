# Belya — Bare-Metal Autonomous AI Software Engineer & SOTA Dominance Harness

[![Release](https://img.shields.io/badge/Release-v7.5.0-blue.svg)](https://github.com/M4F-S/Belya/releases/tag/v7.5.0)
[![License](https://img.shields.io/badge/License-Apache_2.0-green.svg)](LICENSE)
[![Standard](https://img.shields.io/badge/Standard-ISO%2FIEC_9899:1999_(C99)_|_POSIX.1--2008-orange.svg)]()
[![Tests](https://img.shields.io/badge/Unit_Tests-82%2F82_Passed_(100%25)-brightgreen.svg)]()
[![Memory_Safety](https://img.shields.io/badge/Memory_Safety-0_Leaks_|_ASan_|_UBSan_Certified-success.svg)]()
[![Operation_Crucible](https://img.shields.io/badge/Operation_Crucible-100%25_Invariants_Intact-success.svg)]()
[![MCP_Transport](https://img.shields.io/badge/MCP-Persistent_Stdio_Pipes_|_JSON--RPC-blueviolet.svg)]()
[![Sandboxing](https://img.shields.io/badge/Sandboxing-Landlock_LSM_|_Strict_Path_Jailing-red.svg)]()
[![Multi--Agent](https://img.shields.io/badge/Sovereign_Company-21_Autonomous_Roles-informational.svg)]()
[![RFC_8615](https://img.shields.io/badge/Skills_Discovery-RFC_8615_Compliant-success.svg)]()
[![Memory](https://img.shields.io/badge/Memory_Footprint-<3.5MB_Micro--Core_RSS-purple.svg)]()
[![Cold_Start](https://img.shields.io/badge/Cold_Start-<1.5ms-yellow.svg)]()

> **Belya** is a high-performance, bare-metal autonomous AI software engineering core and agency orchestrator engineered from first principles in **pure C99 (POSIX.1-2008)**. Designed to replace resource-heavy Node.js and Python agent harnesses, Belya pairs sub-millisecond cold start times with deterministic AST symbol indexing, speculative shadow git worktrees, Linux Landlock LSM kernel isolation, native stdio LSP integration, persistent Model Context Protocol (MCP) server stacks, an SQLite WAL subagent engine with authored-token budget economics, and a 21-role autonomous SDLC company mesh.

---

## Table of Contents
- [Executive Overview](#executive-overview)
- [Comparative Benchmark Matrix](#comparative-benchmark-matrix)
- [System Architecture](#system-architecture)
  - [1. Dual VTable Structural Isolation (Router vs. Worker)](#1-dual-vtable-structural-isolation-router-vs-worker)
  - [2. Persistent Model Context Protocol (MCP) Subsystem](#2-persistent-model-context-protocol-mcp-subsystem)
  - [3. Speculative Shadow Git Worktree Candidate Dispatcher](#3-speculative-shadow-git-worktree-candidate-dispatcher)
  - [4. Defense-in-Depth Sandboxing & Kernel Security](#4-defense-in-depth-sandboxing--kernel-security)
  - [5. Native Stdio LSP JSON-RPC Client & Compiler Watchdog](#5-native-stdio-lsp-json-rpc-client--compiler-watchdog)
  - [6. Persistent Subagent Engine (Economics & Pool)](#6-persistent-subagent-engine-economics--pool)
  - [7. Interactive Terminal TUI & Side-by-Side Diff Inspector](#7-interactive-terminal-tui--side-by-side-diff-inspector)
  - [8. Closed-Loop Skill Evolution & RFC 8615 Discovery](#8-closed-loop-skill-evolution--rfc-8615-discovery)
  - [9. Multi-Format Trajectory Distillation (OpenAI, ShareGPT, Hermes DPO)](#9-multi-format-trajectory-distillation-openai-sharegpt-hermes-dpo)
  - [10. Sovereign UAE Falcon Presets & Air-Gapped PDPL Operation](#10-sovereign-uae-falcon-presets--air-gapped-pdpl-operation)
- [Sovereign Multi-Agent Enterprise Roster (21 SDLC Roles)](#sovereign-multi-agent-enterprise-roster-21-sdlc-roles)
- [Native Tool Suite (51 Native Tools + 64 MCP Tool Cap)](#native-tool-suite-51-native-tools--64-mcp-tool-cap)
- [Installation & Verification](#installation--verification)
  - [System Prerequisites](#system-prerequisites)
  - [Building from Source](#building-from-source)
  - [Strict Verification Batteries](#strict-verification-batteries)
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
4. **Cloud Egress & Data Privacy Violations:** Continuously streaming proprietary code, internal schema, and credentials to multi-tenant offshore cloud APIs breaches data sovereignty mandates.

Belya addresses these problems directly at the hardware and operating system boundary:
- **Bare-Metal Performance:** Compiles to a compact native binary with `<1.5ms` cold boot latency and under `3.5MB RSS` baseline memory (<8MB peak under full agency load).
- **Dual VTable Security:** Physical C function pointer tables prevent orchestration routines from executing destructive shell operations or directly modifying code.
- **Persistent Model Context Protocol (MCP):** Stdio POSIX pipes connect standards-compliant MCP servers with dynamic `${VAR}` environment expansion and non-fatal timeout recovery.
- **Speculative Shadow Worktrees:** Simultaneously evaluates competing implementation strategies in parallel git worktrees, atomically merging the highest-scoring candidate and pruning non-winning branches.
- **Kernel-Level Sandboxing:** Linux Landlock LSM confines filesystem access strictly to the workspace root, coupled with path-canonicalization jailing and SIGPIPE-safe atomic pipe operations.

---

## Comparative Benchmark Matrix

| Metric | Belya (Pure C99) | Claude Code (Node.js) | OpenHands (Python) | Devin (Cloud SaaS) | Aider (Python CLI) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Language & Toolchain** | **Pure C99 (POSIX)** | TypeScript / Node.js | Python / Docker | Proprietary Cloud | Python CLI |
| **Baseline RSS Memory** | **< 3.5 MB** | ~380 MB | ~1,450 MB | Cloud-Hosted | ~260 MB |
| **Cold Boot Latency** | **< 1.5 ms** | ~1,850 ms | ~3,400 ms | Web Session Init | ~950 ms |
| **Compiled Binary Size** | **< 350 KB** | ~85 MB (Node bundle) | ~4.2 GB (Docker) | N/A (Closed) | ~180 MB (Venv) |
| **Sandboxing Model** | **Landlock LSM + Path Jail** | Container / None | Docker Container | Cloud Sandbox | None |
| **Speculative Branches** | **Atomic Shadow Worktrees** | Linear File Edits | Linear File Edits | Cloud Workspaces | Git Reset / Undo |
| **LSP Integration** | **Native Stdio JSON-RPC** | Plugin / External | Tool wrapper | Proprietary | Repo Map Tree |
| **MCP Integration** | **Native C Stdio Pipes** | Node.js Transport | Python Transport | Proprietary | Partial |
| **Compiler Watchdog** | **Pre-Flight Auto-Revert** | Post-Run Bash | Post-Run Test | Cloud Unit | Linter Check |
| **Multi-Agent Roster** | **21 Sovereign SDLC Roles** | Single Orchestrator | Multi-Agent Crew | Single Agent | Single Agent |

---

## System Architecture

```
                                      +------------------------------------+
                                      |          Belya C99 Core            |
                                      +------------------------------------+
                                                        |
                     +----------------------------------+----------------------------------+
                     |                                  |                                  |
          +----------------------+           +----------------------+           +----------------------+
          |   Orchestrator VTable|           |     Worker VTable    |           |  Persistent MCP Stack|
          |   (Read-Only Routing)|           | (Sandboxed Execution)|           |   (mcp_servers.json) |
          +----------------------+           +----------------------+           +----------------------+
                     |                                  |                                  |
        +------------+------------+            +--------+--------+               +---------+---------+
        |            |            |            |                 |               |         |         |
   Task Board   Subagent Pool   Audit      Edit Tools       Safe Bash         GitHub    Serena    Semgrep
   (SQLite WAL) (Authored Token)(ROMA)    (AST Slicer)    (Landlock LSM)       (LSP)     (AST)    (SAST)
```

### 1. Dual VTable Structural Isolation (Router vs. Worker)
To eliminate prompt-injection vulnerabilities and prevent rogue orchestrator loops from mutating code directly, Belya physically splits tool visibility into distinct virtual tables:
- **Router VTable (`g_router_vtable`):** Restricted to read-only codebase navigation, AST symbol inspection, subagent delegation, task board tracking, and high-level architectural decisions.
- **Worker VTable (`g_worker_vtable`):** Confined within Landlock LSM sandboxes. Only workers have access to `edit_file`, `apply_patch`, and bounded compiler invocation.

### 2. Persistent Model Context Protocol (MCP) Subsystem
Belya integrates a native C stdio JSON-RPC MCP engine (`mcp_client.c`) loaded automatically at startup across all operating modes (`--telegram`, `--headless`, `--prompt`, and interactive REPL):
- **Declarative Configuration (`mcp_servers.json`):**
  ```json
  {
    "mcpServers": {
      "github": {
        "command": "npx",
        "args": ["-y", "@modelcontextprotocol/server-github"],
        "env": { "GITHUB_PERSONAL_ACCESS_TOKEN": "${GITHUB_TOKEN}" }
      },
      "ref": {
        "command": "npx",
        "args": ["-y", "ref-tools-mcp"]
      },
      "serena": {
        "command": "uvx",
        "args": ["--from", "git+https://github.com/oraios/serena", "serena-mcp", "--project-dir", "."]
      },
      "semgrep": {
        "command": "uvx",
        "args": ["semgrep-mcp"]
      }
    }
  }
  ```
- **Dynamic Variable Expansion:** Replaces `${VAR}` tokens at runtime using host environment variables.
- **Resilient Transport:** Multi-line framing loops absorb non-JSON banner lines; handshake timeouts log non-fatal warnings without crashing native tools.
- **Strict Bounds:** Up to 8 concurrent MCP server processes; global tool registry safely capped at 64 tools.

### 3. Speculative Shadow Git Worktree Candidate Dispatcher
Instead of writing edits directly into the working tree, `belya_dispatch_worker_speculative()` generates parallel Git shadow worktrees to explore competing strategies:
1. *Strategy A (Surgical Minimal):* Focuses on the smallest viable diff.
2. *Strategy B (Defensive & Robust):* Enforces memory bounds, NULL pointer guards, and defensive error handlers.
3. *Strategy C (Idiomatic Pattern):* Aligns with clean C99 POSIX architectural patterns.

Candidates are evaluated under AddressSanitizer and unit test suites:
$$\text{Score} = (\text{Passed Tests} \times 10,000) - (\text{Diff Churn} \times 2)$$
The highest-scoring candidate is atomically merged into the working directory; competing branches are automatically pruned.

### 4. Defense-in-Depth Sandboxing & Kernel Security
- **Linux Landlock LSM:** Confines filesystem mutations strictly to the active workspace directory.
- **Path Canonicalization:** `is_path_jailed()` validates absolute paths using `realpath()` to neutralize directory traversal (`../../`) and symlink escapes.
- **Safe Command Forking:** Ripgrep and subshell executions bypass shell interpolation, neutralizing command injection vulnerabilities.

### 5. Native Stdio LSP JSON-RPC Client & Compiler Watchdog
- **Stdio LSP Client (`lsp_client.c`):** Connects to native language servers (`clangd`, `rust-analyzer`, `gopls`, `pyright`) over standard input/output pipes.
- **Pre-Flight Compiler Watchdog:** Automatically verifies modified C/C++ files against `gcc -Wall -Wextra -Werror` before applying edits, reverting syntax errors before they contaminate the repository.

### 6. Persistent Subagent Engine (Economics & Pool)
- **SQLite WAL Subagents:** Subagents persist state to SQLite with dedicated memory databases.
- **Authored-Token Economics:** Prompt re-sends and orchestration wrappers are not billed to child work budgets.
- **Middle-Out Context Compaction:** Compresses conversational context above 16k tokens while preserving foundational system prompts and latest tool results.

### 7. Interactive Terminal TUI & Side-by-Side Diff Inspector
- **Single-Keystroke Modal (`tui_interactive_approve`):** Instant tool approval (`[Y]` Apply, `[N]` Reject, `[E]` Edit args, `[A]` Always allow, `[D]` Split diff).
- **Split Side-by-Side Diff Inspector:** Dual-column terminal diff viewer with dynamic width detection and ANSI syntax highlighting.
- **Live Token HUD:** Displays upstream model status, prompt/completion tokens, cache hit efficiency, latency, and resident set size (RSS).

### 8. Closed-Loop Skill Evolution & RFC 8615 Discovery
- **Closed-Loop Auto-Synthesis:** Multi-turn problem resolutions automatically distill into modular skills (`skills/<slug>/SKILL.md`) with YAML frontmatter.
- **RFC 8615 Discovery Standard:** CLI flag `--export-skills` exports the catalog to `.well-known/agent-skills/index.json` compliant with the `agentskills.io` standard without opening network ports.

### 9. Multi-Format Trajectory Distillation (OpenAI, ShareGPT, Hermes DPO)
- **OpenAI Fine-Tune:** Exports conversational sessions in JSONL via `/export [id] [file.jsonl]`.
- **ShareGPT / Hermes Format:** CLI flag `--export-sharegpt <file.jsonl>` for multi-turn training datasets.
- **Hermes DPO Preference Format:** CLI flag `--export-dpo <file.jsonl>` mines successful recovery turns (`chosen`) against failed attempts (`rejected`) for Direct Preference Optimization.

### 10. Sovereign UAE Falcon Presets & Air-Gapped PDPL Operation
- Optimized for sovereign execution using UAE Technology Innovation Institute (TII) open weights via `--preset`:
  - `falcon` / `falcon3`: Falcon-3 7B/10B Instruct running on local Ollama (`http://localhost:11434/v1`).
  - `falcon-vllm`: High-throughput local vLLM serving with continuous batching.
  - `falcon2`: Falcon-2 11B multimodal foundation model.
  - `airgap`: Strictly local execution complying with UAE Federal Decree-Law No. 45 (PDPL).

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

## Native Tool Suite (51 Native Tools + 64 MCP Tool Cap)

All 51 native tools are implemented directly in pure C99:

| Category | Native Tools |
| :--- | :--- |
| **Filesystem & Editing** | `read_file`, `write_file`, `edit_file`, `edit_notebook`, `apply_patch`, `list_dir`, `search_files` |
| **Execution & Sandboxing** | `bash`, `manage_task`, `exec_backend`, `define_tool` |
| **Subagents & Agency Mesh**| `spawn_subagent`, `check_subagent`, `collect_subagent`, `cancel_subagent`, `resume_subagent`, `dispatch_agent`, `dispatch_worker_task`, `mesh_collaborate` |
| **Code Graph, AST & LSP** | `lsp_definition`, `lsp_references`, `lsp_query`, `goto_definition`, `find_callers`, `ast_slice`, `query_ast_graph` |
| **Memory & Skills** | `save_memory`, `recall_memory`, `save_skill`, `recall_skill`, `recall_conversation` |
| **Tasks, Planning & Cron** | `task_create`, `task_get`, `task_list`, `task_update`, `cron_create`, `cron_list`, `cron_tick`, `enter_plan_mode`, `exit_plan_mode` |
| **Web & MCP Integration** | `web_search`, `web_fetch`, `fetch_url`, `mcp_call`, `synthesize_mcp_server`, `run_test_cycle` |
| **Operator & Security** | `ask_user`, `audit_security_variants`, `git_status`, `git_diff` |
| **Dynamic MCP Extensions**| Discovered dynamically from `mcp_servers.json` (GitHub, Serena, Semgrep, Ref) up to 64 tools. |

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

### Strict Verification Batteries
Belya enforces a rigorous verification pipeline with zero-warning and zero-leak requirements:
```bash
# 1. Primary unit verification battery (82/82 modules passed - 100% + MCP selftests)
make test

# 2. Strict Memory Safety Audit under AddressSanitizer and UndefinedBehaviorSanitizer
make test_asan

# 3. Adversarial Stress Crucible under ASan (MiniJSON fuzzing, path jailing, safe fork)
make test-crucible-asan

# 4. Multi-Agent SDLC Crucible under ASan (50 continuous autonomous lifecycles)
make test-company-adv-asan

# 5. Security Permission Gates, Agent Loop, and Native Tools
make test-perms && make test-loop && make test-tools

# 6. Task Benchmark Suite (SWE-bench & BFCL simulation)
make bench
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
| `/tools` | List all registered native and persistent MCP tools |
| `/rules` | View ingested repository rules (`.agentrules`, `BELYARULES.md`) |
| `/timeline [N]` | View recent cognitive timeline events |
| `/sessions` | List all checkpointed conversation sessions in SQLite |
| `/save [id]` | Checkpoint conversation state to SQLite database |
| `/resume <id>` | Restore conversation session by identifier |
| `/skills [query]` | List or search saved procedural skills |
| `/checkpoint [id]` | Create a Git and context snapshot |
| `/rollback [id]` | Instant rollback of files and dialogue to a previous checkpoint |
| `/compact [N]` | Prune older messages, keeping N recent (default: 10) |
| `/memory [query]` | Search SQLite persistent memory directly via FTS5 BM25 |
| `/doctor` | Run deep environment, compiler, and git diagnostics |
| `/cost` | View session token accounting and estimated provider cost |
| `/exit`, `/quit` | Restore terminal raw-mode settings and exit cleanly |

---

## Production Deployment & Defense-in-Depth Administration

Belya operates in production on server nodes as an immutable systemd service.

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
lsattr /opt/belya/belya /opt/belya/belya.lkg
# Expected: ----i---------e------- /opt/belya/belya
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

**Maintained by:** [M4F-S](https://github.com/M4F-S)  
**Host Environment:** Linux (x86-64 / ARM64) & macOS (Darwin POSIX)  
**Architecture:** Pure C99 POSIX.1-2008 | SOTA Dominance Release v7.5.0
