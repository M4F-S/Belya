# Harness Parity Plan — Belya vs Claude Code vs Hermes

**Version:** 2.0 — merged programme (full capability set)
**Status:** PROPOSED — awaiting approval. No code written for this plan.
**Date:** 2026-11-13
**Owner:** Mohamed (operator) · implementation via Belya/Toy agents
**Baseline binary:** `f4fd145f412579d9f9df213acbb53c4fbb7b429c16aae5efd1b5cad29cb98b01` (v7.4.0, 48 tools, 54/54 gate)

> v1 was a completeness-first plan (~37 d). v2 keeps **every feature from v1** and adds the
> sequencing and cost controls learned from reviewing it: two slices per heavy phase, a cheap
> vision path on the existing key, a measured concurrency ramp, and an optional capped Claude
> probe. Nothing is removed.

---

## 1. Goal

Close the harness gap between Belya and Claude Code / Hermes Agent without changing the model, then run a controlled A/B of the same harness on Claude vs DeepSeek.

Full capability set:

1. Persistent, parallel, forkable subagents that survive a restart
2. A full memory pipeline (capture → extract → consolidate → snapshot → decay → scored retrieval)
3. Model-aware context accounting and compaction with pinned-fact guarantees
4. Config-driven dynamic tool mounting (MCP), no practical tool cap, health and trust controls
5. Image input end-to-end (Telegram, CLI, provider), cheap by default
6. Claude wired through the existing Anthropic support, with cost accounting and a benchmark A/B

## 2. Non-goals

- Copying Claude Code source (proprietary; the mirrors are leaks). Designs only.
- Matching Claude's model quality. The model ceiling is Phase 6's subject.
- Rewriting Belya in TypeScript/Python. C99 stays.
- GUI, plugin marketplace, IDE extension.
- Any change to the Telegram channel contract (plain prose, ~150 words, no code dumps).

## 3. Principles

1. **One PR per slice, each independently shippable.** Heavy phases (1, 2) ship in two slices: a usable core first, then the durability/depth layer.
2. **Additive, never replacement.** New subsystems live beside the old path behind a config flag; measure, then migrate.
3. **The gate grows, never shrinks.** 54/54 stays green; every slice adds tests and quotes the summary line.
4. **Schema migrations are additive.** `ALTER TABLE ... ADD COLUMN` and new tables only; DB backup before every deploy.
5. **Measure before optimising.** Phase 0 exists so every later claim has a number.
6. **Design, not code.** Claude Code: mechanisms. Hermes (MIT): patterns, and code with attribution.

## 4. Decisions locked (from the 2026-11-13 review)

| # | Decision | Effect on plan |
|---|---|---|
| 1 | Parallel subagents, pool designed for 4 | Ship `max_parallel=2`, raise to 4 after Phase 0 metrics; hard budget per child |
| 2 | No model switch until the end; no wasted credits | Claude spend limited to an **optional, capped ($5) probe** after Phase 0; full A/B only in Phase 6 |
| 3 | Cheap VLM | `qwen3.8-flash` — already on the opencode zen key, described as a vision-language model; `deepseek-v4.1-flash` claims visual understanding and is tried first |
| 4 | Deploy cadence | One deploy per slice, except 2a+3 shipped together (shared pinned-fact path) |

## 5. Where Belya stands (verified 2026-11-13)

- 48 registered tools, **hard cap 64** in `belya_harness_register_tool` → 16 slots for MCP.
- `spawn_subagent` (belya_harness.c:2143) is in-process, whitelisted, and dies at the turn boundary.
- Memory: `agent_memory` + FTS5 + `agent_timeline`; no extraction, consolidation, decay, or contradiction handling.
- Compaction: 5 tiers (micro → truncate tools → middle-out → FTS5 evict → emergency), fixed 80% threshold, keep 10.
- MCP: `mcp_connect` on demand (stdio + HTTP), no startup config, no health, no reconnect.
- Anthropic: `x-api-key`, `anthropic-version: 2023-06-01`, `anthropic-beta: prompt-caching-2024-07-31`, Claude model detection — never exercised.
- Vision: none — `model_adapter.c` has no image part; Telegram has no photo handler.
- Tests: 19 test functions → 54 assertion groups, 100% green.
- Reference: Hermes in this fleet = 177 skills, 24 plugins, 273 tool modules, 18 toolsets, MIT.

## 6. Programme at a glance

| Phase | Ships | Slices | Effort |
|---|---|---|---|
| 0 | Benchmark + baseline (+ optional Claude probe) | 1 | 2–3 d |
| 1 | Persistent parallel subagents | 1a core, 1b durability | 9–10 d |
| 2 | Memory pipeline | 2a capture/dedupe, 2b consolidation/decay | 7–8 d |
| 3 | Model-aware context policy | 1 (with 2a) | 3–4 d |
| 4 | Dynamic MCP tool mounting | 4a config/table, 4b health/reload | 4–5 d |
| 5 | Vision end-to-end | 1 | 3 d |
| 6 | Claude wiring + A/B | 1 | 3 d |
| | | **9 PRs / 8 deploys** | **~35 d + 15% ≈ 7–8 weeks** |

---

## Phase 0 — Benchmark and baseline (2–3 days)

**Why:** every later claim needs a number; without it “faster/smarter/cheaper” is opinion.

- T0.1 12 tasks in `bench/tasks/*.json`: 3 research, 3 single-file fixes, 3 multi-file refactors, 2 memory-recall (facts seeded 3 sessions earlier), 1 long-context (200 messages). Plus a **4-task fast subset** for iteration.
- T0.2 Extend `benchmark_runner.c`: success (script-checked), tool calls, prompt/completion tokens, wall time, peak RSS, per-task cost.
- T0.3 Run 3× on the current binary → `bench/baseline-2026-11-13.json`; report median and spread.
- T0.4 `make bench` (not in `verify`; costs tokens).
- T0.5 **Optional Claude probe** (needs approval, hard cap $5): 3 tasks through the *current* harness on Claude, compared with baseline. Purpose: learn whether model or harness dominates before spending weeks. If declined, skip — Phase 6 still measures it.

**Acceptance:** baseline committed; 3-run variance < 15%; `make bench` reproducible on the host.

## Phase 1 — Persistent parallel subagents (9–10 days, 2 slices)

**Why:** the biggest architectural gap. Today a child cannot run alongside another, cannot be inspected, and cannot survive a redeploy.

### 1a — Core: parallel, isolated, inspectable (5–6 d)

**Schema (additive)**
```sql
CREATE TABLE subagents (
  id TEXT PRIMARY KEY, parent_session TEXT NOT NULL, role TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'running',        -- running|paused|done|failed|cancelled
  task TEXT NOT NULL, summary TEXT, workspace TEXT,
  model TEXT, budget_tokens INTEGER DEFAULT 0, spent_tokens INTEGER DEFAULT 0,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP, updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE subagent_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT, subagent_id TEXT NOT NULL,
  idx INTEGER NOT NULL, role TEXT NOT NULL, content TEXT NOT NULL,
  tool_calls_json TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

- T1.1 Schema + idempotent migration in `belya_agent.c`.
- T1.2 `belya_subagent_fork(parent, role, task, budget)` — role manifest + a **summary** of parent context (never the full transcript) + workspace (reuse worktree code).
- T1.3 `belya_subagent_step(id)` — one bounded turn on the existing whitelist harness; persists every message.
- T1.4 Parent tools: `spawn_subagent(wait=false)` returns a handle; `check_subagent(id)`, `collect_subagent(id)`, `cancel_subagent(id)`.
- T1.5 Concurrency pool: `max_parallel` (ship 2, cap 4), pthread workers, SQLite WAL + `busy_timeout`, one harness per child.
- T1.6 Budgets: stop at `budget_tokens`; mark `failed` with reason; parent sees spend.

**Tests:** two children run concurrently without interleaved corruption; budget exhaustion; parent receives summary; 50 forks without fd/memory growth; SQLite busy handling.
**Acceptance:** 2 concurrent children complete; gate ≥ 58/58.

### 1b — Durability (3–4 d)

- T1.7 `belya_subagent_resume(id)` — reload state from SQLite after process restart; status transitions.
- T1.8 Orphan handling: on boot, `running` rows → `paused`; resume or cancel via tool.
- T1.9 Artifacts table + collection (paths, not bytes); GC of `done`/`failed` older than 7 days.
- T1.10 `/agents` command: list children, status, spend.

**Tests:** fork → `systemctl restart` → resume → deliver; orphan sweep; GC leaves no orphan messages.
**Acceptance:** a child survives a real service restart and delivers.

## Phase 2 — Memory pipeline (7–8 days, 2 slices)

### 2a — Capture and dedupe (4 d)

```sql
CREATE TABLE memory_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT, turn_idx INTEGER,
  kind TEXT NOT NULL,              -- user_fact|decision|preference|error|artifact
  raw TEXT NOT NULL, created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
ALTER TABLE agent_memory ADD COLUMN confidence REAL DEFAULT 0.7;
ALTER TABLE agent_memory ADD COLUMN source_event INTEGER;
ALTER TABLE agent_memory ADD COLUMN updated_at DATETIME;
ALTER TABLE agent_memory ADD COLUMN superseded_by INTEGER;
```

- T2.1 `memory_extract(turn)` — after the reply is delivered, never blocking it; rules for preferences, one cheap model call for facts.
- T2.2 Dedupe on write: FTS similarity > threshold → merge, bump confidence, keep newest text.
- T2.3 Retrieval scoring: BM25 × salience × recency, top-k within a token budget (replaces raw `LIMIT`).

**Tests:** 20 seeded facts, recall precision ≥ 85%; duplicate write merges; reply latency unchanged ±10%.

### 2b — Consolidation, snapshots, decay (3–4 d)

- T2.4 `memory_consolidate()` — merge clusters, resolve contradictions (`superseded_by`), decay salience by age × access.
- T2.5 `session_snapshot` pinned row every N turns (decisions, open tasks, names, current goal).
- T2.6 Nightly curator cron (03:20) via `cron_jobs`; logs what it merged/forgot.
- T2.7 `/memory why <topic>` — show the provenance of a memory.

**Tests:** consolidation idempotent (zero duplicates on re-run); contradiction resolution; decay with a fake clock; snapshot used by compaction.

## Phase 3 — Model-aware context policy (3–4 days)

- T3.1 Per-model config: `context_window`, `reserve_output`, `compact_at` (0.75), `keep_recent`.
- T3.2 Real accounting: API `prompt_tokens` + tokenizer estimate for unsent messages; EWMA calibration per model.
- T3.3 Checkpoint (`agent_checkpoints`) before eviction; summary must carry pinned facts, decisions, open tasks, current goal.
- T3.4 Post-compaction assertion: system prompt + pinned facts present (regression test for the PR #36 bug class).
- T3.5 `/context`: window used, tier, next threshold.

**Acceptance:** 200-message session never exceeds the window; pinned facts survive 3 compactions; summary ≤ 8% of window.

## Phase 4 — Dynamic MCP tool mounting (4–5 days, 2 slices)

### 4a — Config + no practical cap (2–3 d)

```json
{ "servers": [
  { "name": "fs", "transport": "stdio", "command": "npx -y @modelcontextprotocol/server-filesystem /opt/charness",
    "env": {}, "allow": ["read_file", "list_dir"], "timeout_ms": 15000 }
]}
```

- T4.1 `.belya/mcp.json` loader; startup connect with timeout; failures log and boot continues.
- T4.2 Dynamic tool table (array growth, no 64 cap) or a separate MCP registry; `server.tool` prefixes; `SEC_MCP` tier.
- T4.3 Per-server allowlist; a server cannot expose `bash` unless allowed; trust model documented.

### 4b — Health and reload (2 d)

- T4.4 Ping on use, one reconnect, clean error text; `mcp_status` tool.
- T4.5 SIGHUP hot reload (add/remove servers without restart).
- T4.6 `/mcp` command: servers, tools, health, last error.

**Tests:** config → restart → tools visible; kill server → clean error, no crash; 100 mounted tools; name collision; disallowed tool refused; hot reload.
**Acceptance:** new server via config edit + restart; zero regressions in the 48 built-ins.

## Phase 5 — Vision end-to-end (3 days)

- T5.1 `model_adapter.c`: content parts `{"type":"image_url","image_url":{"url":"data:image/jpeg;base64,..."}}`; capability flag per model.
- T5.2 Try `deepseek-v4.1-flash` first (claims visual understanding); fall back to `qwen3.8-flash` (explicit VLM, same key) if quality is poor.
- T5.3 Per-turn model override so an image turn can route without changing the global model.
- T5.4 Telegram: `photo` → largest size → `getFile` → download (cap 5 MB) → `uploads/` → attach to the turn.
- T5.5 CLI `--image <path>`.
- T5.6 Hygiene: path + hash in the transcript, never base64; uploads pruned after 7 days; downscale to 1568 px.

**Acceptance:** screenshot question answered correctly; text-only models degrade with a clear message; 5 MB cap enforced; no image bytes in SQLite.

## Phase 6 — Claude wiring and A/B (3 days)

- T6.1 Config block: `provider: anthropic`, endpoint, key env, model, context window, prompt caching.
- T6.2 Tool-schema mapping: Belya tool JSON → Anthropic `tools` / `tool_use` / `tool_result`; verify multi-turn tool loops.
- T6.3 Prompt caching: system + tools; record cache hits/creation tokens.
- T6.4 Cost accounting per turn (prompt, cached, completion) into `agent_timeline`.
- T6.5 A/B: 12 tasks × 3 runs × 2 models, same prompts/tools; report success, tokens, latency, cost, variance.
- T6.6 Decision gate: adopt Claude as default only if success +10% or the cost/latency trade is acceptable; otherwise keep DeepSeek and document why.

**Acceptance:** A/B report committed; model switch is one config change, no rebuild; rollback is one env line.

## 7. Feature coverage map

| Requested feature | Phase | Slice |
|---|---|---|
| Benchmark + baseline | 0 | 0 |
| Parallel subagents | 1 | 1a |
| Subagent persistence/resume | 1 | 1b |
| Auto memory capture | 2 | 2a |
| Dedupe + scored retrieval | 2 | 2a |
| Consolidation, decay, snapshots, curator | 2 | 2b |
| Model-aware context + pinned facts | 3 | with 2a |
| MCP config, no cap, allowlist | 4 | 4a |
| MCP health, reconnect, hot reload | 4 | 4b |
| Vision (Telegram, CLI, provider) | 5 | 5 |
| Claude adapter + A/B | 6 | 6 |

## 8. Definition of done (every slice)

1. PR on a branch; CI green; link in the description.
2. Host `sudo -u belya make verify` → `All Tests Passed Successfully (N/N)`, `verify: all targets passed`, `EXIT=0` — quoted.
3. `make deploy`; sha256 of `build/belya` == `/proc/<MainPID>/exe`; journal errors 0; previous binary saved as `belya.lkg`.
4. DB backup before schema changes (`/opt/backups/belya-db-<epoch>/`) + `PRAGMA integrity_check`.
5. Rollback line in the PR: exact sha + one command.
6. Memory note (Gomaa, wing `belya`, room `deploy`).

## 9. Schedule

| Phase | Effort | Cumulative |
|---|---|---|
| 0 | 2–3 d | 3 d |
| 1a | 5–6 d | 9 d |
| 1b | 3–4 d | 13 d |
| 2a + 3 | 6–7 d | 20 d |
| 2b | 3–4 d | 24 d |
| 4a | 2–3 d | 27 d |
| 4b | 2 d | 29 d |
| 5 | 3 d | 32 d |
| 6 | 3 d | **~35 d + 15% buffer ≈ 7–8 weeks** |

## 10. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Schema migration corrupts the DB | Additive only; backup + integrity check; old binary retained |
| Parallel children burn tokens | Hard budget per child; max 2 → 4; parent sees spend |
| SQLite write contention | WAL + busy_timeout; one connection per worker |
| Compaction loses critical facts | Pinned-fact assertion; checkpoint before evict |
| MCP server is untrusted | Allowlist, no bash/network by default, 15 s timeout, `SEC_MCP` |
| Vision inflates context | 5 MB cap, one image per turn, downscale to 1568 px |
| A/B confounded by prompts | Same prompt/tools/tasks; 3 runs; report variance |
| Scope creep | Non-goals list; additions need a new plan |

## 11. Open questions

1. Claude probe: approve the capped $5 measurement after Phase 0, or strictly nothing until Phase 6?
2. Concurrency ramp: raise 2 → 4 after Phase 0, or after Phase 1b?
3. Vision model: default to `deepseek-v4.1-flash` and only route to `qwen3.8-flash` if quality fails — agreed?

## 12. What we take from where (design only)

- **Claude Code:** subagent isolation and resumption; session-memory extraction; checkpoint-before-compaction; permission modes; hooks as first-class.
- **Hermes (MIT):** skills as files with progressive disclosure; plugin manifests; provider layer; toolset gating; memory curator.
- **Belya keeps:** C99 single binary, native tool dispatch (no token cost), Landlock sandbox, self-healing A/B deploys, Telegram channel contract.

---

*Per-slice implementation plans (bite-sized tasks, exact code, exact commands) are written immediately before each slice starts, against the tree at that moment.*
