# CHarness — High-Performance Autonomous C Agent & Security Harness

A high-performance, zero-dependency autonomous AI agent and security execution harness implemented in pure C99. Designed for sub-millisecond execution, complete local privacy, low-level POSIX execution safety, Model Context Protocol (MCP) tool extensibility, dynamic self-tooling, multi-session checkpointing, and 24/7 VPS Telegram Bot remote control.

---

## Key Features

- **Zero Heavy Dependencies:** Pure C99, POSIX, `libcurl`, and `sqlite3`. No Node.js, Python, or npm runtimes required.
- **Micro Footprint & Instant Startup:** Single self-contained ~110KB binary with instant (<2ms) startup and minimal RAM footprint (<8MB).
- **Dynamic Self-Tooling (`define_tool`):** Enables the agent to invent, script, persist (`.charness/tools/`), and dynamically register new executable tools at runtime for autonomous self-evolution.
- **Session Checkpointing & Full Resumption:** Persistent conversation trees in SQLite (`/sessions`, `/save <id>`, `/resume <id>`, or CLI `./c_agent_system --resume <id>`).
- **Fast BPE-Calibrated Token Budgeting:** Real-time token utilization estimation and proactive threshold compaction.
- **Skill Auto-Distillation (`/reflect`):** Post-task reflection and automatic extraction of reusable workflows into SQLite FTS5.
- **Prompt Caching Support:** Anthropic `cache_control` breakpoints (`PROMPT_CACHING=true`) and OpenAI prefix caching optimization.
- **24/7 VPS Telegram Bot Daemon:** Control your autonomous AI engineer from your phone via Telegram.
  - **Zero-Trust Security Gate:** Only authorized Telegram Chat IDs can interact or run actions on your VPS.
  - **Interactive Inline Permission Buttons:** Tap `[ ✅ Approve ]` or `[ ❌ Deny ]` directly on your phone when the agent attempts sensitive operations (`bash`, `write_file`, `apply_patch`, `define_tool`).
  - **Long Polling Transport:** Outbound connection only—no open inbound ports or public domain/SSL required.
- **Model Agnostic with Real-Time SSE Streaming:** Live Server-Sent Events (SSE) word-by-word streaming token output and cyan reasoning display. Works with OpenAI, Groq, OpenRouter, or local offline LLMs (Ollama, vLLM, llama.cpp).
- **Hierarchical Subagent Delegation (`spawn_subagent`):** Autonomous multi-agent task execution in isolated sandboxes.
- **Project Guidelines Auto-Discovery:** Automatically discovers and injects `.agentrules`, `AGENT.md`, or `CLAUDE.md` from repository roots.
- **Model Context Protocol (MCP) Client:** Built-in stdio JSON-RPC 2.0 client to connect external MCP tool servers dynamically.
- **Claude Code-Style Tool Suite:** 13 native tools including unified search/replace patch application, line-range file slicing, and Git tools.
- **Persistent SQLite FTS5 Memory:** BM25 rank-ordered full-text indexing for skill recall.
- **Linenoise Interactive CLI:** Up/Down history navigation, persistent `.charness_history`, and tab autocompletion.

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
└── README.md           # Documentation
```

---

## Native Tool Suite (13 Tools)

1. **`bash`** (`PERM_ASK_USER`): Executes shell commands with persistent CWD tracking and 15s timeout protection.
2. **`read_file`** (`PERM_ALLOW`): Reads file contents with optional line-range slicing (`offset` & `limit`).
3. **`write_file`** (`PERM_ASK_USER`): Writes text content directly to disk.
4. **`edit_file`** (`PERM_ASK_USER`): Performs exact search-and-replace snippet modifications.
5. **`apply_patch`** (`PERM_ASK_USER`): Applies multi-hunk structured replacement patches (`<<<<<<< SEARCH ... ======= ... >>>>>>> REPLACE`).
6. **`list_dir`** (`PERM_ALLOW`): Inspects directory contents.
7. **`search_files`** (`PERM_ALLOW`): Recursively searches text patterns across codebase files (grep-like).
8. **`git_status`** (`PERM_ALLOW`): Inspects Git working copy status.
9. **`git_diff`** (`PERM_ALLOW`): Inspects staged or unstaged Git diffs.
10. **`save_memory`** (`PERM_ALLOW`): Stores verified knowledge and skills into SQLite persistent memory.
11. **`recall_memory`** (`PERM_ALLOW`): Searches SQLite memory using FTS5 BM25 ranking.
12. **`spawn_subagent`** (`PERM_ALLOW`): Spawns a child autonomous agent worker in an isolated context.
13. **`define_tool`** (`PERM_ASK_USER`): Dynamically invents, scripts, persists, and registers a new tool on the fly.

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

### 1. Setup Environment
```bash
cp .env.example .env
nano .env
```
Fill in:
- `TELEGRAM_BOT_TOKEN`: From [@BotFather](https://t.me/botfather)
- `TELEGRAM_CHAT_ID`: Your numeric user ID from [@userinfobot](https://t.me/userinfobot)
- `MODEL_ENDPOINT` & `MODEL_NAME`: Your local Ollama or Cloud API endpoint

### 2. Run Directly
```bash
source .env && ./c_agent_system --telegram
```

### 3. Install as a 24/7 Systemd Service
```bash
sudo cp charness.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now charness
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

### 2. Resume Previous Session
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
MIT
