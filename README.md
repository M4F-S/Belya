# CHarness — High-Performance Autonomous C Agent & Security Harness

A lightweight, zero-dependency autonomous AI agent and security execution harness implemented in pure C99. Designed for sub-millisecond execution, complete local privacy, and low-level POSIX execution safety.

---

## Key Features

- **Zero Heavy Dependencies:** Built on pure C99, POSIX, `libcurl`, and `sqlite3`. No Node.js or Python runtime required.
- **Micro Footprint:** Compiles to a self-contained ~55KB binary with instant (<2ms) startup and minimal memory usage.
- **Model Agnostic:** Works with OpenAI, OpenRouter, Groq, or local offline LLMs (Ollama, vLLM, llama.cpp).
- **Hermes-3 ReAct Reasoning:** Multi-turn autonomous tool execution loop with dynamic schema emission.
- **Persistent Knowledge Memory:** SQLite `FTS5` virtual tables with BM25 rank-ordered full-text retrieval.
- **Claude Code-Style Security Gates:** Tiered authorization (`PERM_ALLOW`, `PERM_ASK_USER`, `PERM_DENY`) for sensitive system operations.
- **POSIX Sandbox & Process Control:** Non-blocking pipe execution with 15-second timeout protection and persistent directory tracking.
- **Sliding Context Window:** Dynamic message compaction and history pruning.
- **Interactive REPL & Slash Commands:** In-terminal controls (`/help`, `/tools`, `/clear`, `/compact`, `/memory`, `/model`, `/cwd`).

---

## Project Structure

```
.
├── common.h          # Dynamic string buffers & memory management
├── minijson.h        # Lightweight JSON AST parser & serializer header
├── minijson.c        # Zero-dependency JSON AST engine
├── model_adapter.h   # Transport interface definition
├── model_adapter.c   # libcurl client with exponential backoff retries & error handling
├── c_agent.h         # Hermes-style Agent reasoning core & SQLite memory header
├── c_agent.c         # ReAct agent loop, history compaction, and FTS5 search
├── c_harness.h       # Terminal harness & security gate header
├── c_harness.c       # POSIX sandboxing, 8 native tools, persistent CWD, and REPL
├── main.c            # Entry point and runtime configuration
├── test_suite.c      # Unit and integration test suite
├── Makefile          # Build configuration
└── README.md         # Documentation
```

---

## Native Tool Suite

1. **`bash`** (`PERM_ASK_USER`): Executes shell commands with persistent CWD tracking and 15s timeout protection.
2. **`read_file`** (`PERM_ALLOW`): Reads file contents with optional line-range slicing (`offset` & `limit`).
3. **`write_file`** (`PERM_ASK_USER`): Writes text content directly to disk.
4. **`edit_file`** (`PERM_ASK_USER`): Performs exact search-and-replace snippet modifications without rewriting full files.
5. **`list_dir`** (`PERM_ALLOW`): Inspects directory contents.
6. **`search_files`** (`PERM_ALLOW`): Recursively searches text patterns across codebase files (grep-like).
7. **`save_memory`** (`PERM_ALLOW`): Stores verified knowledge and skills into SQLite persistent memory.
8. **`recall_memory`** (`PERM_ALLOW`): Searches SQLite memory using FTS5 BM25 ranking.

---

## Build & Test

### Prerequisites
- **Debian / Ubuntu:** `sudo apt-get install build-essential libcurl4-openssl-dev libsqlite3-dev`
- **macOS:** `brew install curl sqlite3`

### Compile
```bash
make
```

### Run Tests
```bash
make test
```

---

## Running the Agent

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

## REPL Commands

- `/help` — Display system status and command reference
- `/tools` — Show registered tools and permission levels
- `/clear` — Reset conversation history (preserves system instructions)
- `/compact [N]` — Prune older messages, keeping `N` recent turns
- `/memory [query]` — Query SQLite persistent memory directly
- `/model <name>` — Switch active AI model dynamically
- `/cwd [path]` — View or change current working directory
- `exit` — Terminate the REPL

---

## License
MIT
