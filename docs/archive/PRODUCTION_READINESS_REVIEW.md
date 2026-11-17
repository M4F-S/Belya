# Belya & Almaz Production Readiness & Skeptical Review Directive

**Date:** 2026-09-12  
**Target Goal:** Full Deep Skeptical Strict Review, Comprehensive Test & Benchmark Execution, Production Hardening & Release Preparation.

---

## 1. Executive Context & System Architecture

- **GitHub Repository:** `M4F-S/Belya`
  - `main` branch: Commit `a8ddddf` (clean, verified with AddressSanitizer).
  - `evolve/almaz` branch: Commit `cf13a2e` (merged with latest fixes).
- **Core Technology:** Pure C99, zero dependencies outside libc, libcurl, and libsqlite3.
- **Active VPS (`187.124.2.26`):**
  - **Core Belya (Champion):** `/opt/belya` (symlink `/opt/charness`), systemd unit `belya.service`, handle `@AgentCthe_bot`.
  - **Almaz (Challenger):** `/opt/almaz`, systemd unit `almaz.service`, handle `@AlmaztheBot`.
  - **Model Backend:** OpenRouter `deepseek/deepseek-v4-flash`.

---

## 2. Mandatory Scope of Review for the Next Session

### Phase 1: Skeptical Codebase & Architecture Audit
1. **Memory Safety & Leaks:**
   - Run `clang -fsanitize=address,undefined` across all source modules: `linenoise.c`, `minijson.c`, `minifrontmatter.c`, `mcp_client.c`, `model_adapter.c`, `belya_agent.c`, `belya_harness.c`, `telegram_adapter.c`.
   - Verify that all `malloc`, `calloc`, `strdup`, and `dyn_str_new` calls have corresponding `NULL` checks and deterministic free paths (especially in failure and early-return paths).
2. **Bounds & String Safety:**
   - Audit all `snprintf`, `memcpy`, and buffer copies. Guarantee zero truncated string warnings (`-Wstringop-truncation`) and zero buffer overflows.
3. **Sandbox & Security Boundaries:**
   - Audit `is_path_jailed()` for any symlink traversal, race condition, or escaping workspace root.
   - Verify restricted bash capabilities (`bash_restricted`) for tester subagents and untrusted executions.
4. **Metacognitive Circuit Breaker & Git Rollback Guard:**
   - Audit `belya_harness_record_tool_observation()` and ensure rollback triggers (`git reset --hard && git clean -fd`) execute reliably without leaving orphaned files or uncommitted mutations.

### Phase 2: Live Verification & Testing
1. **Super Strict Unit Test Suite (Host & VPS):**
   - Execute `make clean && make test`.
   - All 33 tests must pass with 100% success rate under AddressSanitizer and LeakSanitizer.
2. **Comprehensive 30-Scenario Arena Benchmark (Host & VPS):**
   - Execute `make benchmark`.
   - Verify 100.0% pass rate across all 5 Arenas:
     - Arena 1: Function Calling & Scavenger (BFCL) (10/10)
     - Arena 2: Polyglot Editing & Patching (Aider) (6/6)
     - Arena 3: Scoped Memory & Retention (Gomaa) (5/5)
     - Arena 4: Autonomous Issue Solving (SWE-bench) (4/4)
     - Arena 5: Harness Resource Footprint & Latency ("C-Factor") (5/5)
   - Verify Resident Set Size (RSS) is `< 15 MB` and zero memory leak delta.

### Phase 3: Production VPS Agent Verification
1. **Daemon Health & Resource Footprint:**
   - Check `systemctl status belya.service almaz.service`.
   - Verify process memory (`< 10 MB`), zero runaway CPU consumption, and clean systemd logs (`journalctl`).
2. **Telegram Polling & Response Verifications:**
   - Verify that long-polling remains resilient against network disconnects and Telegram API rate limits.
   - Verify active session auto-resume (`telegram_active` in `belya_memory.sqlite`).

### Phase 4: Release Preparation & Packaging
1. **Documentation & Changelog:**
   - Verify `README.md`, `CHANGELOG.md`, and manifest specs in `agents/*.md`.
   - Document the newly added features: compound `cd` persistence, session auto-resume, non-streaming reasoning fallbacks, and Makefile clean safety.
2. **Git Release Tagging:**
   - Create a clean signed or annotated Git tag (e.g. `v6.6.0` or `v7.0.0`).
   - Push tags to `origin` on GitHub.

---

## 3. Verification Commands for the Next Session

```bash
# 1. Local Strict Verification
make clean
make test
make benchmark

# 2. VPS Remote Verification
ssh root@187.124.2.26 "cd /opt/belya && make clean && make test && make benchmark"
ssh root@187.124.2.26 "cd /opt/almaz && make clean && make test && make benchmark"
ssh root@187.124.2.26 "systemctl status belya.service almaz.service --no-pager"
```
