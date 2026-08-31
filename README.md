# CHarness & CAgent — High-Performance Autonomous C Agent & Security Execution Sandbox

[![Release](https://img.shields.io/badge/Release-v3.0.0-blue.svg)](https://github.com/M4F-S/CHarness/releases/tag/v3.0.0)
[![License](https://img.shields.io/badge/License-Apache_2.0-green.svg)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C99-orange.svg)]()
[![Tests](https://img.shields.io/badge/Tests-11%2F11_Passed_(100%25)-brightgreen.svg)]()

A high-performance, zero-dependency autonomous AI agent and security execution harness implemented in pure C99. Designed for sub-millisecond execution, complete local privacy, low-level POSIX execution safety, Model Context Protocol (MCP) tool extensibility, dynamic self-tooling, multi-session checkpointing, pre-flight compiler auto-healing, Gomaa memory scoping, and 24/7 VPS Telegram Bot remote control.

---

## Architectural Overview

CHarness and CAgent feature a clean **Brain & Sandbox** separation. You can run them **together as an autonomous software engineer** or use **CHarness alone as a standalone security execution runtime** for any external agent or application.

```mermaid
graph TD
    subgraph "Mode A: Full Agent System (CAgent + CHarness)"
        User["Operator (Terminal CLI / Telegram)"] --> H["CHarness Runtime & Security Sandbox"]
        H --> A["CAgent Reasoning Core (ReAct Loop)"]
        A --> GW["Model Gateway (Ollama / vLLM / OpenAI / Anthropic)"]
        A --> DB[("SQLite FTS5 + Gomaa Memory & Sessions")]
        A --> SUB["spawn_subagent (Isolated Worker Sandboxes)"]
    end

    subgraph "Mode B: Standalone Harness (CHarness Alone)"
        Ext["External App / Python / Node / Custom Agent"] --> H2["CHarness Sandboxed Execution Engine"]
        H2 --> SEC["Tiered Security Policy (ALLOW / ASK_USER / DENY)"]
        SEC --> T1["14 Native Tools (bash, edit_file, apply_patch, fetch_url, git, etc.)"]
        SEC --> T2["Dynamic Custom Tools (.charness/tools/)"]
        SEC --> T3["MCP Client (JSON-RPC stdio servers)"]
    end
```

---

## Key Capabilities

- **Zero Heavy Dependencies:** Pure C99, POSIX, `libcurl`, and `sqlite3`. No Node.js, Python, or npm runtimes required.
- **Micro Footprint & Instant Startup:** Self-contained ~120KB binary with instant (<2ms) startup and minimal RAM footprint (<8MB).
- **Pre-Flight Compiler Watchdog (Auto-Healing):** `tool_write_file`, `tool_edit_file`, and `tool_apply_patch` automatically run pre-flight syntax checks on C/C++ files (`gcc -fsyntax-only`), returning compiler warnings directly to the model for immediate self-correction.
- **Gomaa Memory Architecture:** SQLite-backed memory with Wing & Room hierarchical scoping (`wing/room: topic`), dynamic salience scoring with access-frequency recency boosts, and persistent timeline logging (`/timeline`).
- **Native Web Retrieval (`fetch_url`):** Built-in HTTP GET client leveraging libcurl directly with automatic redirect following and 100KB buffer protection.
- **Dynamic Self-Tooling (`define_tool`):** Enables the agent to invent, script, persist (`.charness/tools/`), and register new executable tools on the fly.
- **Token Budget Auto-Compaction Watchdog:** Automatically monitors BPE token utilization and proactively compacts context at 80% threshold to prevent context overflow errors.
- **Session Checkpointing & Resumption:** Persistent conversation trees in SQLite (`/sessions`, `/save <id>`, `/resume <id>`, or `--resume <id>`).
- **24/7 VPS Telegram Bot Daemon:** Control your autonomous AI engineer from your phone with a **Zero-Trust Security Gate** (only your Chat ID is accepted) and **Interactive Inline Approval Buttons** (`[ ✅ Approve ]` / `[ ❌ Deny ]`).
- **Model Agnostic with Real-Time SSE Streaming:** Live Server-Sent Events (SSE) word-by-word streaming token output and cyan reasoning display across local offline LLMs (Ollama, vLLM) and Cloud APIs (OpenAI, Groq, OpenRouter, Anthropic).
- **Prompt Caching Breakpoints:** Automatic Anthropic `cache_control: {"type": "ephemeral"}` breakpoint injection on system prompt and tool definitions for 90%+ prompt cache hit rates.
- **Model Context Protocol (MCP) Client:** Stdio JSON-RPC 2.0 client to connect external MCP tool servers dynamically.

---

## Step-by-Step Beginner's Guide

### Prerequisites & Quick Build

#### 1. Install System Dependencies
- **Ubuntu / Debian:**
  ```bash
  sudo apt-get update && sudo apt-get install -y build-essential libcurl4-openssl-dev libsqlite3-dev
  ```
- **macOS:**
  ```bash
  brew install curl sqlite3
  ```

#### 2. Clone & Build
```bash
git clone https://github.com/M4F-S/CHarness.git
cd CHarness
make
```
This builds the single self-contained executable: `./c_agent_system`.

---

### Mode 1: Full Autonomous AI Agent (Terminal CLI)

Use this mode for local interactive coding and development in your terminal.

#### Step 1: Set Your LLM Backend

**Option A — Local Offline LLM (Ollama / vLLM / llama.cpp):**
```bash
export MODEL_ENDPOINT="http://localhost:11434/v1/chat/completions"
export MODEL_NAME="hermes-3"
export MODEL_API_KEY="none"
```

**Option B — Cloud Provider (OpenAI / Groq / OpenRouter):**
```bash
export MODEL_ENDPOINT="https://api.openai.com/v1/chat/completions"
export MODEL_NAME="gpt-4o"
export MODEL_API_KEY="sk-your-api-key-here"
```

#### Step 2: Start the Agent
```bash
./c_agent_system
```

#### Step 3: Interact with the Agent
Type your request in plain English. The agent will autonomously read files, execute shell commands, edit code, and verify its work:
```
charness [1 msgs | 67 toks]> Inspect the Makefile and optimize compiler flags for C99
```

**Helpful Slash Commands in CLI:**
- `/help` — Show command reference
- `/status` — View active model, endpoint, token usage, and working directory
- `/tools` — List all registered tools and permissions
- `/timeline` — View recent Gomaa timeline event log
- `/save my_work` — Checkpoint your current conversation to SQLite
- `/sessions` — List all saved sessions
- `/resume my_work` — Resume a saved session
- `/reflect` — Distill recent task steps into persistent memory
- `/compact` — Prune older conversation turns to conserve token budget

---

### Mode 2: 24/7 VPS Telegram Bot (Remote AI Engineer)

Use this mode to run the agent as a continuous daemon on your VPS, allowing you to command it and approve sensitive actions directly from your phone.

```mermaid
sequenceDiagram
    autonumber
    actor User as You (on Telegram Mobile)
    participant Bot as CHarness Daemon (VPS)
    participant Tools as VPS Host Tools

    User->>Bot: "Check disk usage and restart nginx"
    Bot->>Bot: Verify Chat ID against TELEGRAM_CHAT_ID (Zero-Trust)
    Bot->>User: Inline Prompt: [✅ Approve] [❌ Deny] bash("systemctl restart nginx")
    User->>Bot: Tap [✅ Approve]
    Bot->>Tools: Execute command in sandbox (15s timeout)
    Tools-->>Bot: Observation output
    Bot-->>User: "Nginx restarted successfully. Memory usage: 38%."
```

#### Step 1: Create a Telegram Bot & Get Your Chat ID
1. Open Telegram and message [@BotFather](https://t.me/botfather). Send `/newbot` to get your **`TELEGRAM_BOT_TOKEN`**.
2. Message [@userinfobot](https://t.me/userinfobot) to get your personal numeric **`TELEGRAM_CHAT_ID`** (e.g. `987654321`).

#### Step 2: Configure `.env`
```bash
cp .env.example .env
nano .env
```
Fill in your configuration:
```env
MODEL_ENDPOINT=http://localhost:11434/v1/chat/completions
MODEL_NAME=hermes-3
MODEL_API_KEY=none

TELEGRAM_BOT_TOKEN=123456789:ABCDefGhIJKlmNoPQRsTUVwxyZ
TELEGRAM_CHAT_ID=987654321
```

#### Step 3: Run Interactive or as a 24/7 Background Service

- **Run in Foreground:**
  ```bash
  source .env && ./c_agent_system --telegram
  ```

- **Run as a 24/7 Systemd Service (Auto-restart on boot):**
  ```bash
  sudo mkdir -p /opt/charness
  sudo cp -r . /opt/charness/
  sudo cp charness.service /etc/systemd/system/
  sudo systemctl daemon-reload
  sudo systemctl enable --now charness
  ```

Check status anytime:
```bash
sudo systemctl status charness
sudo journalctl -u charness -f
```

---

### Mode 3: Standalone Security Execution Sandbox (CHarness Alone)

Use `CHarness` purely as an embedded C99 execution sandbox for external agents, scripts, or custom applications.

`CHarness` provides:
- Non-blocking subprocess execution with **15-second execution timeouts** (kills hung processes automatically).
- Persistent working directory (`cwd`) tracking.
- Multi-hunk structured patch application (`apply_patch`).
- Tiered security policies (`PERM_ALLOW`, `PERM_ASK_USER`, `PERM_DENY`).
- Dynamic MCP stdio proxying.

#### Example C Code (`standalone_example.c`):
```c
#include "c_harness.h"

int main(void) {
    // 1. Initialize standalone harness without an active LLM
    CHarness *h = c_harness_init(NULL);

    // 2. Prepare tool arguments
    JsonValue *args = json_create_object();
    json_obj_add(args, "command", json_create_string("git status -s"));

    // 3. Execute 'bash' tool inside the security sandbox
    for (size_t i = 0; i < h->tool_count; i++) {
        if (strcmp(h->tools[i].name, "bash") == 0) {
            char *result = h->tools[i].callback(NULL, args);
            printf("Sandbox Output:\n%s\n", result);
            free(result);
            break;
        }
    }

    json_free(args);
    c_harness_free(h);
    return 0;
}
```

Compile and run:
```bash
gcc -Wall -O2 standalone_example.c linenoise.c minijson.c mcp_client.c model_adapter.c c_agent.c c_harness.c telegram_adapter.c -lcurl -lsqlite3 -o standalone_example
./standalone_example
```

---

## Native Tool Suite Reference (13 Tools)

| Tool Name | Security Gate | Parameters | Description |
|:---|:---|:---|:---|
| **`bash`** | `ASK_USER` | `command` (string) | Executes shell commands with persistent CWD tracking & 15s timeout protection |
| **`read_file`** | `ALLOW` | `path` (string), `offset` (num), `limit` (num) | Reads file contents with optional line-range slicing |
| **`write_file`** | `ASK_USER` | `path` (string), `content` (string) | Writes text content directly to disk |
| **`edit_file`** | `ASK_USER` | `path`, `old_text`, `new_text` | Exact search-and-replace snippet modifications |
| **`apply_patch`** | `ASK_USER` | `path`, `patch` | Multi-hunk structured replacement patch engine (`<<<<<<< SEARCH ... ======= ... >>>>>>> REPLACE`) |
| **`list_dir`** | `ALLOW` | `path` (string) | Inspects directory contents |
| **`search_files`** | `ALLOW` | `pattern`, `path`, `file_glob` | Recursively searches text patterns across codebase files (grep-like) |
| **`git_status`** | `ALLOW` | *(none)* | Inspects Git working copy status |
| **`git_diff`** | `ALLOW` | `staged` (bool), `path` (string) | Inspects staged or unstaged Git diffs |
| **`save_memory`** | `ALLOW` | `topic`, `content` | Stores verified knowledge into SQLite persistent memory |
| **`recall_memory`** | `ALLOW` | `query` (string) | Searches SQLite memory using FTS5 BM25 ranking |
| **`spawn_subagent`**| `ALLOW` | `task`, `instructions`, `max_turns` | Spawns child autonomous agent in an isolated context |
| **`define_tool`** | `ASK_USER` | `name`, `description`, `parameters`, `script_body` | **Dynamically creates, scripts, persists, and registers a new tool for self-evolution** |

---

## Commands & Slash Controls Reference

| Command | Environment | Description |
|:---|:---|:---|
| `/help` | CLI & Telegram | Display command reference and system status |
| `/status` | CLI & Telegram | View active model, endpoint, token usage, CWD, and session count |
| `/tools` | CLI & Telegram | Show registered tools (including dynamic MCP & custom tools) |
| `/rules` | CLI & Telegram | View active repository guidelines (`.agentrules` / `AGENT.md`) |
| `/sessions` | CLI & Telegram | List all checkpointed conversation sessions in SQLite |
| `/save [id]` | CLI & Telegram | Checkpoint current conversation tree to database |
| `/resume <id>` | CLI & Telegram | Restore past conversation session by ID |
| `/reflect` | CLI & Telegram | Distill recent trajectory into a reusable SQLite FTS5 skill |
| `/clear` | CLI & Telegram | Reset conversation history (preserves system instructions) |
| `/compact [N]` | CLI & Telegram | Prune older messages, keeping `N` recent turns |
| `/memory [q]` | CLI & Telegram | Query SQLite persistent memory directly |
| `/model <m>` | CLI & Telegram | Switch active AI model dynamically |
| `/cwd [path]` | CLI & Telegram | View or change current working directory |
| `/mcp <cmd>` | CLI & Telegram | Connect to an external stdio MCP server |
| `exit` | CLI | Terminate the interactive REPL |

---

## Testing & Verification

Run the automated test suite to verify memory safety, tool behavior, session persistence, and token estimation:

```bash
make test
```

**Expected Output:**
```
================ Running CHarness & CAgent Evolution 2.0 Test Suite ================
[Test] DynString Operations...
  -> DynString PASSED
[Test] MiniJSON Parser & Serializer...
  -> MiniJSON PASSED
[Test] BPE-calibrated Token Estimator...
  -> Token Estimator PASSED (Total: 67 tokens)
[Test] Agent Memory (FTS5) & Rules Auto-Discovery...
  -> Agent Memory & Rules PASSED
[Test] Session Checkpointing & Resumption...
  -> Session Checkpointing & Resumption PASSED
[Test] Dynamic Self-Tooling (define_tool & Custom Script Execution)...
  -> Dynamic Self-Tooling PASSED
[Test] Harness Tool Suite (13 Tools) & Patch Engine...
  -> Harness Tools & Patch Engine PASSED
[Test] Telegram Bot Adapter Security & Setup...
  -> Telegram Adapter PASSED
================ All Tests Passed Successfully (100%) ================
```

---

## License

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at:

[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0)

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
