# CHarness & CAgent — High-Performance Autonomous C Agent & Security Execution Sandbox

[![Release](https://img.shields.io/badge/Release-v2.0.0-blue.svg)](https://github.com/M4F-S/CHarness/releases/tag/v2.0.0)
[![License](https://img.shields.io/badge/License-Apache_2.0-green.svg)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C99-orange.svg)]()
[![Tests](https://img.shields.io/badge/Tests-100%25_Passed-brightgreen.svg)]()

A high-performance, zero-dependency autonomous AI agent and security execution harness implemented in pure C99. Designed for sub-millisecond execution, complete local privacy, low-level POSIX execution safety, Model Context Protocol (MCP) tool extensibility, dynamic self-tooling, multi-session checkpointing, and 24/7 VPS Telegram Bot remote control.

---

## Architectural Overview

CHarness and CAgent are designed with a clean **Brain & Body (Sandbox)** separation, allowing you to use them **together as a complete autonomous software engineer** or **use CHarness alone as a standalone security execution runtime**.

```mermaid
graph TD
    subgraph "Mode A: Full Agent System (CAgent + CHarness)"
        User[Operator / Telegram / Terminal] --> H[CHarness Runtime & Sandbox]
        H --> A[CAgent Reasoning Brain]
        A --> GW[Model Gateway (Ollama / vLLM / OpenAI / Anthropic)]
        A --> DB[(SQLite FTS5 Memory & Sessions)]
        A --> SUB[spawn_subagent (Child Sandboxes)]
    end

    subgraph "Mode B: Standalone Harness (CHarness Alone)"
        Ext[External Agent / Python / Node / Custom App] --> H2[CHarness Sandboxed Execution Engine]
        H2 --> SEC[Tiered Security Policy (ALLOW / ASK_USER / DENY)]
        SEC --> T1[13 Native Tools (bash, edit_file, apply_patch, git, etc.)]
        SEC --> T2[Dynamic Custom Tools (.charness/tools/)]
        SEC --> T3[MCP Client (JSON-RPC stdio servers)]
    end
```

---

## Key Capabilities

- **Zero Heavy Dependencies:** Pure C99, standard POSIX, `libcurl`, and `sqlite3`. No Node.js, Python, or npm runtimes required.
- **Micro Footprint & Instant Startup:** Self-contained ~110KB binary with instant (<2ms) startup and minimal RAM footprint (<8MB).
- **Dynamic Self-Tooling (`define_tool`):** Enables the agent to invent, script, persist (`.charness/tools/`), and register new executable tools on the fly.
- **Session Checkpointing & Full Resumption:** Persistent conversation trees in SQLite (`/sessions`, `/save <id>`, `/resume <id>`, or CLI flag `./c_agent_system --resume <id>`).
- **24/7 VPS Telegram Bot Daemon:** Control your autonomous AI engineer from your phone via Telegram.
  - **Zero-Trust Security Gate:** Only authorized Telegram Chat IDs can interact or run actions on your VPS.
  - **Interactive Inline Permission Buttons:** Tap `[ ✅ Approve ]` or `[ ❌ Deny ]` directly on your phone when the agent attempts sensitive operations (`bash`, `write_file`, `apply_patch`, `define_tool`).
  - **Long Polling Transport:** Outbound connection only—no open inbound ports or public domain/SSL required.
- **Model Agnostic with Real-Time SSE Streaming:** Live Server-Sent Events (SSE) word-by-word streaming token output and cyan reasoning display. Works with OpenAI, Groq, OpenRouter, Anthropic, or local offline LLMs (Ollama, vLLM, llama.cpp).
- **Fast BPE Token Budgeting:** Real-time token utilization estimation and proactive threshold compaction.
- **Skill Auto-Distillation (`/reflect`):** Post-task reflection and automatic extraction of reusable workflows into SQLite FTS5.
- **Prompt Caching Support:** Anthropic `cache_control` breakpoints (`PROMPT_CACHING=true`) and OpenAI prefix caching optimization.
- **Model Context Protocol (MCP) Client:** Built-in stdio JSON-RPC 2.0 client to connect external MCP tool servers dynamically.

---

## Modes of Usage

### Mode 1: Using CAgent Together with CHarness (Full Autonomous Agent)

In this mode, `CAgent` drives the ReAct reasoning loop while `CHarness` enforces permissions, runs tools in sandboxes, and handles UI/Telegram interaction.

```bash
# Set your LLM backend
export MODEL_ENDPOINT="http://localhost:11434/v1/chat/completions"
export MODEL_NAME="hermes-3"

# Launch interactive CLI
./c_agent_system
```

---

### Mode 2: Using CHarness Alone (Standalone Security Execution Sandbox)

You can embed `CHarness` directly into any external C application, Python extension, or custom runtime to serve purely as a **secure tool execution sandbox**.

#### Embedding Example (`standalone_harness.c`):
```c
#include "c_harness.h"

int main(void) {
    // 1. Initialize standalone harness without an active LLM gateway
    CHarness *h = c_harness_init(NULL);

    // 2. Connect external MCP tool servers if desired
    c_harness_connect_mcp(h, "npx -y @modelcontextprotocol/server-filesystem /tmp");

    // 3. Execute sandboxed tools programmatically with permission gates
    JsonValue *args = json_create_object();
    json_obj_add(args, "command", json_create_string("git status -s"));

    // Find and execute the 'bash' tool (enforces 15s timeout and CWD tracking)
    for (size_t i = 0; i < h->tool_count; i++) {
        if (strcmp(h->tools[i].name, "bash") == 0) {
            char *output = h->tools[i].callback(NULL, args);
            printf("Execution Observation:\n%s\n", output);
            free(output);
            break;
        }
    }

    json_free(args);
    c_harness_free(h);
    return 0;
}
```

---

## Native Tool Suite (13 Tools)

| Tool Name | Permission Level | Description |
|:---|:---|:---|
| **`bash`** | `ASK_USER` | Executes shell commands with persistent CWD tracking and 15s timeout protection |
| **`read_file`** | `ALLOW` | Reads file contents with optional line-range slicing (`offset` & `limit`) |
| **`write_file`** | `ASK_USER` | Writes text content directly to disk |
| **`edit_file`** | `ASK_USER` | Exact search-and-replace snippet modifications |
| **`apply_patch`** | `ASK_USER` | Multi-hunk structured replacement patch engine (`<<<<<<< SEARCH ... ======= ... >>>>>>> REPLACE`) |
| **`list_dir`** | `ALLOW` | Inspects directory contents |
| **`search_files`** | `ALLOW` | Recursively searches text patterns across codebase files (grep-like) |
| **`git_status`** | `ALLOW` | Inspects Git working copy status |
| **`git_diff`** | `ALLOW` | Inspects staged or unstaged Git diffs |
| **`save_memory`** | `ALLOW` | Stores verified knowledge and skills into SQLite persistent memory |
| **`recall_memory`** | `ALLOW` | Searches SQLite memory using FTS5 BM25 ranking |
| **`spawn_subagent`**| `ALLOW` | Spawns a child autonomous agent worker in an isolated sandbox context |
| **`define_tool`** | `ASK_USER` | **Dynamically creates, scripts, persists, and registers a new tool for self-evolution** |

---

## Project Structure

```
.
├── common.h            # Dynamic string buffers, token estimator, & memory management
├── linenoise.h         # Single-file zero-dependency line editor header
├── linenoise.c         # Terminal raw mode, ANSI escape codes, & tab completion
├── minijson.h          # Lightweight JSON AST parser & serializer header
├── minijson.c          # Zero-dependency JSON AST engine
├── mcp_client.h        # Stdio JSON-RPC 2.0 Model Context Protocol client header
├── mcp_client.c        # Bidirectional pipe transport & tool discovery
├── model_adapter.h     # Transport interface, prompt caching, & SSE streaming
├── model_adapter.c     # libcurl client with exponential backoff & SSE parser
├── c_agent.h           # Hermes-style Agent reasoning core & SQLite memory/sessions
├── c_agent.c           # ReAct loop, session persistence, FTS5 search, & reflection
├── c_harness.h         # Terminal harness, security gates, & custom tool management
├── c_harness.c         # POSIX sandboxing, 13 native tools, subagents, and REPL
├── telegram_adapter.h  # Telegram bot API & long-polling transport header
├── telegram_adapter.c  # Telegram daemon with inline button permissions
├── main.c              # Entry point (CLI REPL, --resume, & Telegram modes)
├── test_suite.c        # Comprehensive unit and integration test suite
├── Makefile            # Build configuration
├── charness.service    # Systemd service unit file for VPS deployment
├── .env.example        # Environment template
├── LICENSE             # Apache License 2.0
└── README.md           # Documentation
```

---

## Build & Test

### Prerequisites
- **Debian / Ubuntu:** `sudo apt-get install build-essential libcurl4-openssl-dev libsqlite3-dev`
- **macOS:** `brew install curl sqlite3`

### Compile
```bash
make
```

### Run Test Suite
```bash
make test
```

---

## Deployment on VPS (Telegram Bot Mode)

Run CHarness as a 24/7 background service on your VPS and interact with it from your Telegram app.

### 1. Setup Environment
```bash
cp .env.example .env
nano .env
```
Fill in:
```env
# 1. LLM Model Configuration
MODEL_ENDPOINT=http://localhost:11434/v1/chat/completions
MODEL_NAME=hermes-3
MODEL_API_KEY=none

# 2. Telegram Bot Token (from @BotFather)
TELEGRAM_BOT_TOKEN=123456789:ABCDefGhIJKlmNoPQRsTUVwxyZ

# 3. Your Telegram User ID (from @userinfobot)
# CRITICAL: CHarness will block any user whose Chat ID does not match this!
TELEGRAM_CHAT_ID=987654321
```

### 2. Run Directly
```bash
source .env && ./c_agent_system --telegram
```

### 3. Install as a 24/7 Systemd Service (Always On)
```bash
sudo mkdir -p /opt/charness
sudo cp -r . /opt/charness/
sudo cp charness.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now charness
```

Check status and logs:
```bash
sudo systemctl status charness
sudo journalctl -u charness -f
```

---

## Interactive Terminal CLI Mode

### 1. Local Offline (Ollama / vLLM / llama.cpp)
```bash
export MODEL_ENDPOINT="http://localhost:11434/v1/chat/completions"
export MODEL_NAME="hermes-3"
export MODEL_API_KEY="none"
./c_agent_system
```

### 2. Cloud Providers (OpenAI / Groq / OpenRouter)
```bash
export MODEL_ENDPOINT="https://api.openai.com/v1/chat/completions"
export MODEL_NAME="gpt-4o"
export MODEL_API_KEY="sk-..."
./c_agent_system
```

### 3. Resume Previous Session
```bash
./c_agent_system --resume my_session_id
```

---

## Commands & Slash Controls (CLI & Telegram)

- `/help` — Display system status and command reference
- `/status` — View active model, endpoint, token usage, CWD, and session count
- `/tools` — Show registered tools (including dynamic MCP & custom tools)
- `/rules` — View active repository guidelines (`.agentrules`)
- `/sessions` — List all checkpointed conversation sessions in SQLite
- `/save [id]` — Checkpoint current conversation tree to database
- `/resume <id>` — Restore past conversation session by ID
- `/reflect` — Distill recent trajectory into a reusable SQLite FTS5 skill
- `/clear` — Reset conversation history (preserves system instructions)
- `/compact [N]` — Prune older messages, keeping `N` recent turns
- `/memory [query]` — Query SQLite persistent memory directly
- `/model <name>` — Switch active AI model dynamically
- `/cwd [path]` — View or change current working directory
- `/mcp <command>` — Connect to an external stdio MCP server
- `exit` — Terminate the REPL

---

## License

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at:

[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0)

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
