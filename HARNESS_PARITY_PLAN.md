# Harness Parity Plan — Belya vs Claude Code vs Hermes

**Status:** PROPOSED — awaiting approval. No code has been written for this plan.
**Date:** 2026-11-13
**Owner:** Mohamed (operator) · implementation via Belya/Toy agents
**Baseline binary:** `f4fd145f412579d9f9df213acbb53c4fbb7b429c16aae5efd1b5cad29cb98b01` (v7.4.0, 48 tools, 54/54 gate)

---

## 1. Goal

Close the **harness** gap between Belya and Claude Code / Hermes Agent, without changing the model, and then run a controlled A/B of the same harness on Claude vs the current DeepSeek model.

The gap is not the codebase size or the language. It is five missing mechanisms, in order of leverage:

1. Persistent, forkable subagents (today: `spawn_subagent` dies at the turn boundary)
2. A memory pipeline (today: one flat `agent_memory` table + FTS)
3. Model-aware context accounting (today: 5-tier compaction with a fixed 80% threshold)
4. Config-driven dynamic tool mounting (today: MCP servers connect on demand, 64-tool cap)
5. Image input (today: no image path anywhere)

Then, and only then, wire Claude through the Anthropic protocol support that already exists and measure both models with the same benchmark.

## 2. Non-goals

- Copying Claude Code source. It is proprietary; the mirrors are leaks. Only **designs** transfer.
- Matching Claude's model quality. Same brain, better body — the model ceiling is Phase 6's subject, not a build target.
- Rewriting Belya in TypeScript/Python. C99 stays; that is the product.
- A GUI, a plugin marketplace, or an IDE extension.
- Any behaviour change to the Telegram channel contract (plain prose, ~150 words, no code dumps).

## 3. Principles

1. **One phase = one shippable PR.** Each phase passes CI, host `make verify`, deploy, sha check, rollback note. No phase waits on a later one except Phase 0.
2. **Additive, never replacement.** New subsystems live beside the old path under a config flag; measure, then migrate. (See `.hermes` discipline: a regression must be attributable.)
3. **The gate grows, never shrinks.** 54/54 stays green; every phase adds tests and the summary line is quoted in the PR.
4. **Schema migrations are additive.** `ALTER TABLE ... ADD COLUMN` and new tables only; back up the DB before each deploy.
5. **Measure before optimising.** Phase 0 exists so every later claim ("faster", "smarter", "cheaper") has a number behind it.
6. **Design, not code.** From Claude Code we take mechanism shapes; from Hermes (MIT) we may adapt code with attribution.

## 4. Where Belya actually stands (verified, 2026-11-13)

| Area | Reality | Gap |
|---|---|---|
| Tools | 48 registered, hard cap 64 in `belya_harness_register_tool` | Only 16 slots for MCP tools; collisions unhandled |
| MCP | `mcp_connect` at runtime; `belya_harness_connect_mcp`, stdio + HTTP | No startup config, no health check, no reconnect |
| Subagents | `spawn_subagent` (belya_harness.c:2143), worker whitelist, in-process | No persistence, no fork/resume, no own memory |
| Memory | `agent_memory` + FTS5 + `agent_timeline` | No extraction, consolidation, contradiction handling, decay |
| Compaction | 5 tiers, `compaction_percent` 80, `compaction_keep` 10 | Fixed threshold; no per-model window; no pinned-fact guarantee |
| Tokens | `prompt_tokens`/`completion_tokens` captured from API | Not used to drive policy; no cost accounting |
| Claude | Anthropic headers + `anthropic-beta: prompt-caching-2024-07-31` + claude model detection in `model_adapter.c` | No model config block, no tool-schema mapping, never used in anger |
| Vision | None (`model_adapter.c` has no image content part; Telegram has no photo handler) | Whole capability missing |
| Skills | 9 SKILL.md, RFC 8615 discovery | No curator, no per-skill usage telemetry |
| Hooks | `belya_run_lifecycle_hook` exists | Not documented or surfaced as a product feature |
| Tests | 19 test functions → 54 assertion groups, 100% green | No benchmark, no regression metric beyond pass/fail |

Reference points: Hermes in this fleet runs **177 skills, 24 plugins, 273 tool modules** across 18 toolsets and is MIT-licensed. Claude Code ships ~43 tools with proprietary source.

## 5. Program phases

Estimates assume one focused engineer-agent working with the existing gate and review loop.

### Phase 0 — Benchmark and baseline (2–3 days)

**Why:** every later phase claims an improvement; without a baseline those claims are opinions.

**Work**
- T0.1 Define 12 tasks in `bench/tasks/*.json`: 3 research (web_search + synthesis), 3 single-file code fixes, 3 multi-file refactors, 2 memory-recall (facts seeded 3 sessions earlier), 1 long-context (200-message session).
- T0.2 Extend `benchmark_runner.c` to record per task: success (script-checked), tool calls, prompt/completion tokens, wall time, peak RSS.
- T0.3 Run 3× on the current binary; write `bench/baseline-2026-11-13.json`.
- T0.4 Add `make bench` (not part of `verify`; network + cost).

**Acceptance:** baseline JSON committed; variance across 3 runs < 15%; `make bench` reproducible on the host.

### Phase 1 — Persistent subagents (8–10 days)

**Why:** this is the single biggest architectural difference. Claude Code can fan out, keep child context, and resume; Belya loses everything when the turn ends.

**Schema (additive, `belya_memory.sqlite`)**
```sql
CREATE TABLE subagents (
  id TEXT PRIMARY KEY, parent_session TEXT NOT NULL, role TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'running',   -- running|paused|done|failed
  task TEXT NOT NULL, summary TEXT, workspace TEXT,
  model TEXT, budget_tokens INTEGER DEFAULT 0, spent_tokens INTEGER DEFAULT 0,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP, updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE subagent_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT, subagent_id TEXT NOT NULL,
  idx INTEGER NOT NULL, role TEXT NOT NULL, content TEXT NOT NULL,
  tool_calls_json TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE subagent_artifacts (
  id INTEGER PRIMARY KEY AUTOINCREMENT, subagent_id TEXT NOT NULL,
  path TEXT NOT NULL, kind TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

**Work**
- T1.1 Schema + migration in `belya_agent.c` (`belya_agent_init_schema`), idempotent.
- T1.2 `belya_subagent_fork(parent, role, task, budget)` — clones the role manifest (`agent_manifests`), a *summary* of parent context (never the full transcript), and a workspace (reuse worktree code).
- T1.3 `belya_subagent_step(id)` — one bounded turn using the existing whitelist harness; persists every message.
- T1.4 `belya_subagent_resume(id)` — reload from SQLite after process restart; status transitions.
- T1.5 Parent integration: `spawn_subagent` gains `wait=false` and returns a handle; new `check_subagent(id)` and `collect_subagent(id)` tools; summary + artifact paths returned to the parent.
- T1.6 Budgets: stop at `budget_tokens`, mark `failed` with reason; parent is told.
- T1.7 GC: `done`/`failed` rows older than 7 days archived; no orphan messages.

**Files:** `belya_agent.c/.h`, `belya_harness.c` (spawn/collect), `test_suite.c`.
**Tests:** fork→step→resume in a **new process**; budget exhaustion; parent receives summary; artifact recorded; 200 subagents without fd/memory growth.
**Acceptance:** a subagent survives `systemctl restart belya`, resumes, and delivers; gate ≥ 60/60.

### Phase 2 — Memory pipeline (6–8 days)

**Why:** Belya stores facts but never extracts, merges, or forgets. Hermes runs a curator; Claude Code runs extraction + consolidation stages.

**Schema**
```sql
CREATE TABLE memory_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT, turn_idx INTEGER,
  kind TEXT NOT NULL,            -- user_fact|decision|preference|error|artifact
  raw TEXT NOT NULL, created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
ALTER TABLE agent_memory ADD COLUMN confidence REAL DEFAULT 0.7;
ALTER TABLE agent_memory ADD COLUMN source_event INTEGER;
ALTER TABLE agent_memory ADD COLUMN updated_at DATETIME;
ALTER TABLE agent_memory ADD COLUMN superseded_by INTEGER;
```

**Work**
- T2.1 `memory_extract(turn)` — one cheap model call (or rule-based for preferences), writes `memory_events`; runs *after* the reply is sent, never blocking it.
- T2.2 `memory_consolidate()` — merge near-duplicates (FTS similarity), resolve contradictions (newer wins, old marked `superseded_by`), decay salience by age × access.
- T2.3 Retrieval scoring: BM25 (FTS5) × salience × recency, top-k with a token budget; replaces raw `LIMIT`.
- T2.4 Snapshot: every N turns write a pinned `session_snapshot` memory row (decisions, open tasks, names) used by compaction.
- T2.5 Curator cron: nightly consolidation at 03:20 via `cron_jobs`.

**Tests:** 20 seeded facts, recall precision ≥ 85%; re-running consolidation creates zero duplicates; contradiction test; decay test with fake clock.
**Acceptance:** recall precision target met; reply latency unchanged (±10%); no memory writes during a turn.

### Phase 3 — Model-aware context policy (3–4 days)

**Why:** the 5-tier compactor is good, but it triggers on a fixed percentage and cannot see the model's real window or the value of pinned facts.

**Work**
- T3.1 Per-model config: `context_window`, `reserve_output`, `compact_at` (default 0.75), `keep_recent`.
- T3.2 Real accounting: use captured `prompt_tokens` (already available) + a tokenizer estimate for un-sent messages; EWMA calibration per model.
- T3.3 Compaction writes an `agent_checkpoints` row before evicting; summary must include pinned facts, decisions, open tasks, and the user's current goal.
- T3.4 Post-compaction assertion: system prompt + pinned facts present (regression test for the class of bug fixed in PR #36).
- T3.5 `/context` command: window used, tier, next threshold.

**Acceptance:** 200-message session never exceeds the window; pinned facts survive 3 compactions; summary ≤ 8% of window; gate green.

### Phase 4 — Dynamic tool mounting (4–5 days)

**Why:** adding a capability should not require recompiling, and 16 free slots is a ceiling that will bite.

**Config (`.belya/mcp.json`)**
```json
{ "servers": [
  { "name": "fs", "transport": "stdio", "command": "npx -y @modelcontextprotocol/server-filesystem /opt/charness",
    "env": {}, "allow": ["read_file", "list_dir"], "timeout_ms": 15000 }
]}
```

**Work**
- T4.1 Config loader + startup connect with timeout; failures log and continue (never block boot).
- T4.2 Grow the tool table (dynamic array, no 64 cap) or add a second registered-MCP list; prefix tools `server.tool` to prevent collisions; permission tier `SEC_MCP`.
- T4.3 Health: ping on use, one reconnect attempt, clean error text; `mcp_status` tool.
- T4.4 Allowlist per server (a server cannot expose `bash` unless allowed); document the trust model.
- T4.5 Hot reload on SIGHUP (optional, only if T4.1–4.4 are green).

**Tests:** config → restart → tools visible; kill server → clean error, no crash; 100 mounted tools; name collision; disallowed tool refused.
**Acceptance:** a new MCP server is usable with a config edit + restart; zero regressions in the 48 built-ins.

### Phase 5 — Vision (3–4 days)

**Why:** it cannot read a screenshot, a design mock, or a chart. For an agent that reviews websites and designs, that is a hard ceiling.

**Work**
- T5.1 `model_adapter.c`: message content parts — `{"type":"image_url","image_url":{"url":"data:image/jpeg;base64,..."}}`; capability flag per model.
- T5.2 Telegram: handle `photo` updates (largest size), `getFile` → download (cap 5 MB) → workspace `uploads/` → attach to the turn.
- T5.3 CLI: `--image <path>` for one-shot questions.
- T5.4 Prompt rule: describe what is needed from the image; never dump base64.
- T5.5 Storage hygiene: uploads pruned after 7 days; no images in the SQLite transcript (store path + hash).

**Acceptance:** screenshot question answered correctly on a vision-capable model; text-only models degrade with a clear message; 5 MB cap enforced; gate green.

### Phase 6 — Claude wiring and A/B (2–3 days)

**Why:** the operator's original goal — run the same body with a stronger brain and see the delta.

**Work**
- T6.1 Config block: `provider: anthropic`, `endpoint`, `api_key_env`, `model`, `context_window`, `prompt_caching: true`.
- T6.2 Tool-schema mapping: Belya's tool JSON → Anthropic `tools` format (input_schema), tool_use/tool_result blocks; verify multi-turn tool loops.
- T6.3 Prompt caching: cache system + tool definitions; measure cache hit tokens (already returns `cache_creation_input_tokens`).
- T6.4 Cost accounting per turn (prompt, cached, completion) written to `agent_timeline`.
- T6.5 A/B: 12 bench tasks × 3 runs × 2 models, same prompts/tools; report success, tokens, latency, cost.
- T6.6 Decision gate: adopt Claude as default only if success +10% or cost/latency acceptable; otherwise keep DeepSeek and document why.

**Acceptance:** A/B report committed; switching models is a config change, no rebuild; rollback = one env line.

## 6. Definition of done (every phase)

1. PR on a branch, CI gate green, link in the PR description.
2. Host `sudo -u belya make verify` → `All Tests Passed Successfully (N/N)`, `verify: all targets passed`, `EXIT=0` — quoted in the PR.
3. Deploy: `make deploy`, sha256 of `build/belya` == `/proc/<MainPID>/exe`, journal errors 0, previous binary saved as `belya.lkg`.
4. DB backup before any schema change (`/opt/backups/belya-db-<epoch>/`).
5. Rollback line in the PR: exact sha + one command.
6. Memory note (Gomaa, wing `belya`, room `deploy`) with what changed and how it was proven.

## 7. Schedule

| Phase | Effort | Cumulative |
|---|---|---|
| 0 Baseline | 2–3 d | 3 d |
| 1 Subagents | 8–10 d | 13 d |
| 2 Memory | 6–8 d | 21 d |
| 3 Context | 3–4 d | 25 d |
| 4 Tools/MCP | 4–5 d | 30 d |
| 5 Vision | 3–4 d | 34 d |
| 6 Claude A/B | 2–3 d | **~37 working days (7–8 weeks)** |

Phases 2 and 4 can run in parallel with 1 if two workstreams are acceptable; 3 depends on 2 (pinned facts) and should follow it.

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Schema migration corrupts the session DB | Additive only; backup + `PRAGMA integrity_check` before deploy; keep the old binary |
| Subagent fan-out burns tokens | Hard `budget_tokens` per child; max 4 concurrent; parent sees spend |
| Compaction loses critical facts | Pinned-fact assertion test; checkpoint before evict |
| MCP server is untrusted | Per-server allowlist, no bash/network by default, 15 s timeout, permission tier |
| Vision inflates context/cost | 5 MB cap, one image per turn, downscale to 1568 px before send |
| Claude A/B is confounded by prompts | Same system prompt, same tools, same tasks, 3 runs, report variance |
| Scope creep (plugins, marketplace, GUI) | Non-goals above; any addition needs a new plan |

## 9. Open questions (need a decision before Phase 1)

1. **Concurrency:** 4 parallel subagents acceptable, or start with 1 (sequential) for safety?
2. **Claude key:** use the existing Anthropic key (if any) or a separate budget line for the A/B?
3. **Vision provider:** which model handles images — Claude, or a cheaper vision model (e.g. a small VLM) routed only for image turns?
4. **Deploy cadence:** one deploy per phase (7 deploys) or batch 2–3 phases per deploy?

## 10. What we take from where (design only)

- **Claude Code:** subagent isolation and resumption; session-memory extraction; checkpoint-before-compaction; permission modes; hooks as first-class.
- **Hermes (MIT):** skills as files with progressive disclosure; plugin manifests; provider layer; toolset gating; memory curator.
- **Belya keeps:** C99 single binary, native tool dispatch (no token cost), Landlock sandbox, self-healing A/B deploys, Telegram channel contract.

---

*Per-phase implementation plans (bite-sized tasks with exact code) will be written immediately before each phase starts, so the code reflects the tree at that moment rather than this document's assumptions.*
