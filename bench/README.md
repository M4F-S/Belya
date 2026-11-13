# Task benchmark (Phase 0)

Measures the real `belya` binary on user-shaped tasks. Not part of `verify` — it
spends model tokens and needs network.

```sh
make bench                       # fast subset (3 tasks), one run
make bench BENCH=all             # all tasks, one run
python3 bench/run_bench.py --tasks all --runs 3 --out bench/baseline.json
```

## What is measured

Per task and run:

- **ok** — every expectation passed (check script, required tool, answer length)
- **wall_ms** — end-to-end process time
- **steps** — model turns (from the binary's `--stats-json`)
- **tool_calls** — tool executions requested (telemetry) and names (parsed from stdout)
- **prompt_tokens / completion_tokens / cached_tokens** — from the provider usage block
- **over_tool_cap** — whether the run exceeded the task's efficiency budget (informational)

`--stats-json <path>` is the only product hook: `main.c` writes one JSON object
after a headless turn so the driver never has to scrape numbers out of prose.

## Layout

```
bench/
  run_bench.py          driver
  tasks/*.json          task definitions
  fixtures/<id>/        files copied into a fresh workspace per run
  .work/                scratch workspaces (gitignored)
  baseline-*.json       recorded baselines (committed)
```

## Task schema

```json
{
  "id": "fix-bug",
  "category": "code-fix",
  "fixture": "fix-bug",
  "fast": true,
  "prompt": "…",
  "timeout_s": 240,
  "expect": {"cmd": "sh check.sh", "contains": "…", "min_chars": 120},
  "require_tools": ["write_file", "bash"],
  "max_tool_calls": 25
}
```

- `expect.cmd` runs in the workspace after the agent finishes; exit 0 = pass.
- `require_tools` passes if **any** listed tool was used.
- `max_tool_calls` is recorded, not scored — a correct answer reached in many
  calls is still correct.
- `seed` is a list of prompts run first in the same workspace, so a recall task
  exercises the real memory path instead of a hand-written database.
- `setup_check.sql` runs against `belya_memory.sqlite` after seeding; if it
  returns nothing the run is failed as a *setup* failure, not a recall failure.

## Adding a task

1. Add `tasks/<id>.json` and, if needed, a fixture directory.
2. Make the success check deterministic (`expect.cmd` preferred over substrings).
3. Keep the prompt answerable without network unless the task is about research.
4. Run it once with `--tasks <id>` and confirm it fails on the unmodified fixture
   before trusting a pass.
