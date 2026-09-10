# Changelog — Belya Autonomous AI Software Engineer (Pure C99)

All notable changes, architectural milestones, and release notes for Belya and Belya Harness are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [v6.5.0] — 2026-09-10
### 🚀 Added & Enhanced
- **Belya Agency Sovereign Multi-Agent Architecture:**
  - **Declarative Manifest Protocol (`agents/*.md`):** Uses `minifrontmatter.c` to parse role definitions into native C structs, indexing them into SQLite FTS5 on startup.
  - **Least-Privilege Tool Bounding:** Restricts tool schemas per subagent role (`Architect` = read-only, `Builder` = surgical code mutator, `Reviewer` = diff auditor, `Tester` = restricted sandbox bash).
  - **Chief-of-Staff Triage Layer:** Evaluates incoming queries, providing instant answers for simple queries or dispatching targeted subagent pipelines for complex engineering tasks.
  - **Per-Subagent Git Rollback Guard:** Snapshots repository `HEAD` SHA prior to mutative execution; automatically executes `git reset --hard` and `git clean -fd` if a subagent encounters errors, loops, or trips circuit breakers.
  - **`dispatch_agent` Native Tool:** Built-in tool allowing orchestrator agents to delegate bounded tasks to specialized subagents with isolated message buffers.
  - **Multi-Modal Controls:** Full agency support via `--agency` / `-a` (CLI), `/agency` (REPL), and Telegram bot daemons.
- **Almaz Sovereign Evolution Supervisor (v2.0):**
  - Real empirical microbenchmarking with high-resolution timing and Linux `/proc/{pid}/status` (`VmHWM`/`VmRSS`) peak memory tracking.
  - Multi-mutation pool (whitespace inlining, DynString preallocation, YAML frontmatter trimming) with least-recently-tested round-robin selection.
  - Real AST/regex scanner verifying zero-tolerance memory allocation checks (`malloc != NULL`).
  - Unified diff generation (`patches/mutation_*.patch`) and automated Git versioning with semantic tags (`evo-v<timestamp>`) on `evolve/almaz`.
- **Systemd Security Sandboxing & Daemon Hardening:**
  - Applied `PrivateTmp=true`, `ProtectSystem=strict`, `ProtectHome=true`, and strict `ReadWritePaths` to both `belya.service` and `almaz.service` on production VPS.
- **Expanded Test Suite (33/33 Tests, 100% Pass):**
  - Added Test 33: Track B Belya Agency Multi-Agent Architecture & Rollback Guard. Verified 0 memory leaks under AddressSanitizer.

---

## [v6.4.0] — 2026-09-10
### 🚀 Added & Enhanced
- **C99 Workspace Path Jailing (`is_path_jailed()` in `belya_harness.c`):**
  - Rigidly enforces directory containment and rejects traversal attacks (`../`).
  - Differentiates strictly between file writes (confined to workspace root) and reads (whitelisting system inspection paths `/tmp/`, `/proc/`, `/etc/os-release`).
- **C99 Lightweight Frontmatter Parser (`minifrontmatter.c` / `minifrontmatter.h`):**
  - Zero-dependency parser for YAML `---` frontmatter blocks extracting scalars and bracket/bullet list sequences into native C structs.
- **File-First Skills System (`skills/*/SKILL.md`):**
  - Automatic scanning and indexing of disk-based markdown skills on startup into SQLite FTS5.
  - Multi-trigger parsing and automatic disk mirroring for newly synthesized skills.
- **Composable Modular Rule Packs (`rules/*/*.md`):**
  - Project-context aware rule injection dynamically tailoring system instructions for C, Python, security, and common workflows.
- **Systematic `TROUBLESHOOTING.md` Pattern Resolver:**
  - Automated detection and remediation injection for known compiler and runtime error traces.
- **Expanded Test Suite (32/32 tests, 100% pass):**
  - Verified under AddressSanitizer and UndefinedBehaviorSanitizer with 0 memory leaks.

---

## [v6.3.0] — 2026-09-06
### 🚀 Added & Enhanced
- **DeepSeek DSML Tool Scavenger Engine (`model_adapter.c`):**
  - Implemented `scavenge_dsml_tool_calls()` supporting DeepSeek-v4-flash's native DSML tool invocation syntax (`<｜DSML｜invoke name="...">...<｜DSML｜parameter name="...">...</｜DSML｜invoke>`).
  - Added XML/tag parameter extraction into normalized JSON tool arguments with validation against `is_known_tool()`.
  - Added fallback to `delta.reasoning` and `res.reasoning_content` when content stream is empty.
- **10-Challenge Universal Coding Benchmark Battery (`tools/universal_benchmark.py`):**
  - Expanded standard Exercism C coding test battery from 5 to 10 canonical challenges: `binary_search`, `queen_attack`, `roman_numerals`, `circular_buffer`, `word_count`, `collatz_conjecture`, `hamming`, `armstrong_numbers`, `allergies`, and `linked_list`.
  - Added CLI options: `--list` (show battery), `--challenge <name>` (run single benchmark), and `--all` (run full suite).
  - Achieved 100% Pass@1 across all executed challenges under AddressSanitizer and UndefinedBehaviorSanitizer with zero memory leaks and zero compiler warnings.
- **Belya-Evolve Autonomous Metamorphic Research Sandbox (`belya-evolve/`):**
  - Implemented isolated sandbox runner (`evolve_sandbox.py`) with safety virtualization guard (`BELYA_EVOLVE_SANDBOX=1`).
  - Implemented 100,000-iteration JSON parse microbenchmark.
  - Verified autonomous AST/source-level mutation in `minijson.c` (inlined whitespace comparison), achieving +9.06% latency reduction and +9.97% Darwinian fitness gain with zero memory leaks.
- **1-Line Universal Installer & Release Packaging (`install.sh`, `dist/`):**
  - Created `install.sh` with OS/architecture auto-detection (macOS Apple Silicon & Linux x86_64), runtime dependency checks, pre-compiled archive extraction, and fallback to native compilation.
  - Packaged pre-compiled release tarballs with SHA-256 checksums (`dist/SHA256SUMS`).

---

## [v6.2.0] — 2026-09-06
### 🚀 Added
- **Internal Self-Telemetry & Proprioception Banner (`belya_agent.c`):**
  - Dynamic Zone 3 ephemeral proprioception injection computing live PID, process uptime, and resident memory (RSS via `getrusage`).
- **Metacognitive Circuit Breaker & Consecutive Failure Threshold:**
  - Tracks consecutive failed tool executions; automatically halts loops or forces re-planning when failure threshold is exceeded.
- **Autonomous Verification Guard:**
  - Enforces mandatory verification steps before concluding software authoring or mutation missions.
- **Test Suite Expansion to 27 Tests (`test_suite.c`):**
  - Added Test 26 (Self-Telemetry & Proprioception) and Test 27 (Circuit Breaker & Verification Guard).

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
