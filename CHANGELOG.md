# Changelog — Belya Autonomous AI Software Engineer (Pure C99)

All notable changes, architectural milestones, and release notes for Belya and Belya Harness are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [v6.0.0] — 2026-09-06
### 🚀 Added & Enhanced
- **Resilient Edit Fallback & Diagnostic Nearest Matches (`belya_harness.c`):**
  - Implemented automatic whitespace-normalized fallback line matching in `edit_file`. If exact byte-for-byte `strstr()` fails due to tab vs. space or trailing whitespace discrepancies, line-by-line whitespace normalization resolves the hunk cleanly.
  - Implemented diagnostic near-match reporting: when an edit target cannot be located, Belya scans the file for candidate lines containing the search anchor and reports line numbers and code previews directly back to the model.
- **POSIX Extended Regex Search Engine (`belya_harness.c`):**
  - Added optional `regex: true` parameter to `search_files` tool utilizing POSIX `regcomp`/`regexec` with zero external dependencies.
- **Head-Tail Tool Output Truncation & Token Schema Awareness (`belya_agent.c`):**
  - Replaced hard 2.5KB head-only truncation with configurable head-tail truncation (`TOOL_OUTPUT_LIMIT`, default 8KB) keeping both initial logs and the trailing compiler errors.
  - Added tool schema parameter token estimation to `belya_agent_total_tokens()`, eliminating context budget undercounting.
  - Raised default context threshold to 80 messages and compaction retention to 20 messages.
- **Model-Aware Tool Scavenger Guard (`belya_agent.c`):**
  - Restructured `belya_agent_step` scavenger invocation to avoid executing hypothetical thoughts from `<think>` reasoning blocks on models with native tool calling capabilities, while preserving compatibility with DeepSeek-R1.
- **Memory Leak Elimination on HTTP Retries (`model_adapter.c`):**
  - Ensured all accumulated `tool_calls` buffers are deterministically freed upon network failure and HTTP 429/500 retries.
- **Actionable Execution Protocol & Mechanical Tool Constraints (`main.c`, `belya_agent.c`, `belya_harness.c`):**
  - Replaced abstract philosophical instructions with strict OBSERVE → THINK → ACT → VERIFY loop and explicit read-before-edit constraints across all system prompts and tool schemas.
- **Test Suite Expansion (`test_suite.c`):**
  - Added Test 22: `test_v6_enhancements` verifying resilient whitespace edits, regex searches, and diagnostic failure reporting (22/22 tests passing at 100% under AddressSanitizer and UBSan).

---

## [v5.2.0] — 2026-09-04
### 🚀 Added
- **Persistent HTTP Keep-Alive Connection Pool (`model_adapter.c`, `model_adapter.h`):**
  - Added persistent `void *curl_handle` in `ModelGateway`.
  - Replaced per-turn `curl_easy_init()` / `curl_easy_cleanup()` with `curl_easy_reset()` and `CURLOPT_TCP_KEEPALIVE`.
  - Eliminates redundant TCP 3-way handshakes, TLS 1.3 key negotiations, and 45 CA-bundle disk reads (~150–250ms saved per roundtrip).
- **Forced Text Synthesis Engine on Budget Depletion (`belya_agent.c`, `belya_agent.h`):**
  - Implemented `belya_agent_step_forced_text(agent, instruction)`.
  - Nullifies tool schemas (`tools_schema = NULL`) when step count reaches `max_steps <= 1` or upon interrupt, mathematically forcing the model to synthesize a complete conversational answer rather than attempting another tool dispatch.
- **Context Pruning & Tool Truncation (`belya_agent.c`):**
  - Added automatic truncation at 2,500 bytes for oversized tool outputs in `belya_agent_add_tool_result()`.
  - Appends diagnostic notice `[... Output truncated to 2500 bytes of N total bytes ...]`, preserving prompt cache tightness and ensuring sub-200ms Time-To-First-Token (TTFT).
- **Telegram Progressive Buffer & Hot-Reload (`telegram_adapter.c`):**
  - Replaced content-discard bug with `DynString accum_content` to capture intermediate reasoning and thoughts produced alongside tool dispatches.
  - Implemented `/restart` slash command allowing authorized Telegram operators to cleanly exit the process, triggering systemd `Restart=always` to reload updated binaries within 2 seconds without SSH access.
  - Completely eliminated the fallback delivery blocker (previously dropping queries into `"✅ Action completed."`).
- **Test Suite Expansion (`test_suite.c`):**
  - Added Test 21: `test_forced_synthesis_and_keepalive` (21/21 tests passing at 100%).

---

## [v5.1.0] — 2026-09-04
### 🎨 Added
- **Interactive Terminal Screen Column Redraw (`linenoise.c`):**
  - Implemented `linenoisePromptLen()` to parse visible screen columns and strip ANSI color escape sequences (`\033[...m`).
  - Fixed prompt length miscalculation where ANSI escapes previously counted as raw character width, causing cursor drift, ghost spaces, and misaligned backspace deletes.

---

## [v5.0.0] — 2026-09-04
### 🌐 Added
- **OpenRouter Cloud Gateway & Provider Routing (`model_adapter.c`, `belya_harness.c`):**
  - Injected `HTTP-Referer` and `X-Title: Belya Agent` headers for OpenRouter API requests.
  - Added live provider switching via `/openrouter <model>`, `/ollama <model>`, and `/key <api_key>`.
  - Auto-routing when model name contains provider slashes (e.g. `anthropic/claude-3.7-sonnet`, `deepseek/deepseek-v4-flash`).
- **Host Execution Mandate & Model Self-Awareness (`main.c`, `belya_agent.c`):**
  - Injected explicit cross-platform POSIX/bash execution directives into the system prompt.
  - Dynamic runtime architecture awareness (`Active Model: %s (Provider: %s)`).
- **DeepSeek-R1 Direct Scavenger Bypass (`model_adapter.c`):**
  - Bypasses raw `tools` JSON array for DeepSeek-R1 to prevent Ollama schema rejection while extracting tool calls directly from `<think>` tags.

---

## [v4.0.0] — 2026-09-03
### 🏆 Added
- **Multi-Arena Benchmark Suite (`benchmark_runner.c`):**
  - 30 end-to-end benchmark scenarios across 5 Arenas:
    - Arena 1: Function Calling & Tool Scavenger (BFCL) (10/10)
    - Arena 2: Polyglot Code Editing & Patching (Aider) (6/6)
    - Arena 3: Scoped Memory & Long Dialogue Retention (Gomaa) (5/5)
    - Arena 4: Autonomous Issue Solving (SWE-bench) (4/4)
    - Arena 5: Resource Footprint & Latency ("The C-Factor") (5/5)
  - 100% pass rate in 327–750 ms total duration.
- **Git State Checkpoints & Rollback:**
  - Automated per-turn commit snapshots (`belya_agent_create_checkpoint`, `/checkpoint`, `/rollback`).
- **OpenAI Fine-Tuning Trajectory Exporter:**
  - `/export [session_id] [file]` exporting complete conversation turns and tool calls into standard OpenAI JSONL format.

---

## [v3.0.0] — 2026-09-02
### 🧠 Added
- **Gomaa Memory Paradigm (`belya_agent.c`):**
  - Wing/Room domain isolation (`wing/room: topic`) backed by SQLite FTS5.
  - Salience scoring with recency boosts on access.
  - FTS5 syntax sanitization preventing query operator crashes (`:`, `/`, `*`).
  - Persistent chronological timeline logging (`agent_timeline`).
  - 50-turn needle-in-a-haystack recall test with 100% precision.

---

## [v2.0.0] — 2026-09-01
### 🛠 Added
- **Dynamic Self-Tooling Engine (`belya_harness.c`):**
  - `define_tool` runtime allowing the agent to synthesize custom executable scripts on the fly.
  - Multi-channel parameter contract: `$PARAM_<KEY>`, `$ARG_<KEY>`, positional `$1`/`$2`, `stdin`, `$TOOL_ARGS_JSON`.
- **Pre-Flight Compiler Watchdog (`belya_harness.c`):**
  - Syntax verification before writing/patching C/C++ files (`gcc -fsyntax-only`).
  - Automatic rollback on syntax failure.

---

## [v1.0.0] — 2026-08-31
### ⚡ Initial Release
- Pure C99 autonomous ReAct reasoning engine (<200KB binary, zero dependencies).
- Native 17-tool execution harness (POSIX bash, file operations, web retrieval, git).
- Model Gateway supporting OpenAI, Ollama, and vLLM JSON endpoints.
- SQLite-backed conversation persistence and session management.
- 24/7 Telegram Bot daemon with zero-trust Chat ID authorization.
