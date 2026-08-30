# CHarness — High-Performance Autonomous C Agent & Security Harness

A high-performance, zero-dependency autonomous AI agent and security execution harness implemented in pure C99. Designed for sub-millisecond execution, complete local privacy, low-level POSIX execution safety, Model Context Protocol (MCP) tool extensibility, and 24/7 VPS Telegram Bot remote control.

---

## Key Features

- **Zero Heavy Dependencies:** Pure C99, POSIX, `libcurl`, and `sqlite3`. No Node.js, Python, or npm runtimes required.
- **Micro Footprint & Instant Startup:** Single self-contained ~110KB binary with instant (<2ms) startup and minimal RAM footprint (<8MB).
- **24/7 VPS Telegram Bot Daemon:** Control your autonomous AI engineer from your phone via Telegram.
  - **Zero-Trust Security Gate:** Only authorized Telegram Chat IDs can interact or run actions on your VPS.
  - **Interactive Inline Permission Buttons:** Tap `[ ✅ Approve ]` or `[ ❌ Deny ]` directly on your phone when the agent attempts sensitive operations (`bash`, `write_file`, `apply_patch`).
  - **Long Polling Transport:** Outbound connection only—no open inbound ports or public domain/SSL required.
- **Model Agnostic with Real-Time SSE Streaming:** Live Server-Sent Events (SSE) word-by-word streaming token output and cyan reasoning display. Works with OpenAI, Groq, OpenRouter, or local offline LLMs (Ollama, vLLM, llama.cpp).
- **Hierarchical Subagent Delegation (`spawn_subagent`):** Autonomous multi-agent task execution in isolated sandboxes.
- **Project Guidelines Auto-Discovery:** Automatically discovers and injects `.agentrules`, `AGENT.md`, or `CLAUDE.md` from repository roots.
- **Model Context Protocol (MCP) Client:** Built-in stdio JSON-RPC 2.0 client to connect external MCP tool servers dynamically.
- **Claude Code-Style Tool Suite:** 12 native tools including unified search/replace patch application, line-range file slicing, and Git tools.
- **Persistent SQLite FTS5 Memory:** BM25 rank-ordered full-text indexing for skill recall.
- **Linenoise Interactive CLI:** Up/Down history navigation, persistent `.charness_history`, and tab autocompletion.

---

## Project Structure

```
.
├── common.h            # Dynamic string buffers & memory management
├── linenoise.h         # Single-file zero-dependency line editor header
├── linenoise.c         # Terminal raw mode, ANSI escape codes, & tab completion
├── minijson.h          # Lightweight JSON AST parser & serializer header
├── minijson.c          # Zero-dependency JSON AST engine
├── mcp_client.h        # Stdio JSON-RPC 2.0 Model Context Protocol client header
├── mcp_client.c        # Bidirectional pipe transport & tool discovery
├── model_adapter.h     # Transport interface definition & SSE streaming
├── model_adapter.c     # libcurl client with exponential backoff & SSE parser
├── c_agent.h           # Hermes-style Agent reasoning core & SQLite memory header
├── c_agent.c           # ReAct agent loop, history compaction, and FTS5 search
├── c_harness.h         # Terminal harness & security gate header
├── c_harness.c         # POSIX sandboxing, 12 native tools, subagents, and REPL
├── telegram_adapter.h  # Telegram bot API & long-polling transport header
├── telegram_adapter.c  # Telegram daemon with inline button permissions
├── main.c              # Entry point (CLI REPL & Telegram daemon modes)
├── test_suite.c        # Unit and integration test suite
├── Makefile            # Build configuration
├── charness.service    # Systemd service unit file for VPS deployment
├── .env.example        # Environment template
└── README.md           # Documentation
```

---

## Native Tool Suite (12 Tools)

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

You can run CHarness as a 24/7 background service on your VPS and interact with it from your Telegram app.

### 1. Setup Environment
Copy `.env.example` to `.env`:
```bash
cp .env.example .env
```

Edit `.env`:
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

### 3. Install as a Systemd Service (Always On)
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

### 2. Cloud (OpenAI / OpenRouter / Groq)
```bash
export MODEL_ENDPOINT="https://api.openai.com/v1/chat/completions"
export MODEL_NAME="gpt-4o"
export MODEL_API_KEY="sk-..."
./c_agent_system
```

---

## Commands & Slash Controls (CLI & Telegram)

- `/help` — Display system status and command reference
- `/status` — View active model, endpoint, CWD, and context count
- `/tools` — Show registered tools (including dynamic MCP tools) and permissions
- `/rules` — View active repository guidelines (`.agentrules`)
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
