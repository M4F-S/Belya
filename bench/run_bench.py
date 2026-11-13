#!/usr/bin/env python3
"""Belya task benchmark driver.

Runs the real `belya` binary headless (`-p`) against task fixtures in a fresh
workspace per run, collects the binary's own telemetry (`--stats-json`) plus
tool calls parsed from stdout, evaluates each task's success condition, and
writes a JSON report.

Task file schema (bench/tasks/<id>.json):
  id            unique name
  category      free-form grouping
  prompt        the request sent to the agent
  fixture       directory under bench/fixtures (optional)
  timeout_s     hard wall-clock limit (default 240)
  expect.cmd    shell command run in the workspace; exit 0 = pass (optional)
  expect.contains   substring that must appear in the agent's stdout (optional)
  expect.min_chars  minimum length of the agent's final answer text (optional)
  require_tools     list of tool names; at least one must have been called (optional)

Usage:
  python3 bench/run_bench.py --bin ./build/belya --tasks fast
  python3 bench/run_bench.py --tasks all --runs 3 --out bench/baseline.json
"""
import argparse
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TASKS_DIR = ROOT / "bench" / "tasks"
FIXTURES_DIR = ROOT / "bench" / "fixtures"
WORK_DIR = ROOT / "bench" / ".work"

TOOL_RE = re.compile(r"\[Tool Call Request\]:\s*([A-Za-z0-9_.-]+)")
ANSWER_AFTER = "[Belya]\x1b[0m"  # ansi; we strip codes instead
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def load_env_file(path):
    env = {}
    p = Path(path)
    if not p.is_file():
        return env
    for line in p.read_text(errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        v = v.strip().strip('"').strip("'")
        env[k.strip()] = v
    return env


def load_tasks(selection):
    files = sorted(TASKS_DIR.glob("*.json"))
    tasks = []
    for f in files:
        t = json.loads(f.read_text())
        t.setdefault("id", f.stem)
        tasks.append(t)
    if selection and selection != "all":
        if selection == "fast":
            wanted = {t["id"] for t in tasks if t.get("fast")}
        else:
            wanted = {s.strip() for s in selection.split(",") if s.strip()}
        tasks = [t for t in tasks if t["id"] in wanted]
    return tasks


def prepare_workspace(task, run_idx):
    ws = WORK_DIR / f"{task['id']}-{run_idx}"
    if ws.exists():
        shutil.rmtree(ws)
    ws.mkdir(parents=True)
    fixture = FIXTURES_DIR / task["fixture"] if task.get("fixture") else None
    if fixture and fixture.is_dir():
        for item in fixture.iterdir():
            dst = ws / item.name
            if item.is_dir():
                shutil.copytree(item, dst)
            else:
                shutil.copy2(item, dst)
    git_env = os.environ.copy()
    git_env.update({
        "GIT_AUTHOR_NAME": "bench", "GIT_AUTHOR_EMAIL": "bench@local",
        "GIT_COMMITTER_NAME": "bench", "GIT_COMMITTER_EMAIL": "bench@local",
    })
    subprocess.run(["git", "init", "-q"], cwd=ws, env=git_env, check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["git", "add", "-A"], cwd=ws, env=git_env, check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["git", "commit", "-qm", "init"], cwd=ws, env=git_env, check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return ws


def final_answer(stdout):
    clean = ANSI_RE.sub("", stdout)
    idx = clean.rfind("[Belya]")
    if idx >= 0:
        return clean[idx + len("[Belya]"):].strip()
    return clean.strip()


def run_one(binary, task, run_idx, base_env):
    ws = prepare_workspace(task, run_idx)
    stats_path = ws / ".bench_stats.json"
    cmd = [str(binary), "-p", task["prompt"], "--stats-json", str(stats_path)]
    env = base_env.copy()
    env.setdefault("BELYA_HEADLESS", "1")
    env.setdefault("GIT_AUTHOR_NAME", "bench")
    env.setdefault("GIT_AUTHOR_EMAIL", "bench@local")
    env.setdefault("GIT_COMMITTER_NAME", "bench")
    env.setdefault("GIT_COMMITTER_EMAIL", "bench@local")

    # Optional seed turns: run real turns first (e.g. "remember X") so the
    # measured turn exercises recall instead of a hand-written fixture.
    seed_failures = []
    for i, seed_prompt in enumerate(task.get("seed") or []):
        try:
            subprocess.run([str(binary), "-p", seed_prompt], cwd=ws, env=env,
                           capture_output=True, text=True,
                           timeout=task.get("timeout_s", 240))
        except subprocess.TimeoutExpired:
            seed_failures.append(f"seed {i} timed out")
    setup = task.get("setup_check") or {}
    if setup.get("sql"):
        try:
            con = sqlite3.connect(ws / "belya_memory.sqlite")
            seeded = bool(con.execute(setup["sql"]).fetchone()[0])
            con.close()
            if not seeded:
                seed_failures.append("seed verification found no matching memory")
        except Exception as e:  # noqa: BLE001 - report, don't crash the run
            seed_failures.append(f"seed verification error: {e}")

    started = time.time()
    timed_out = False
    try:
        proc = subprocess.run(cmd, cwd=ws, env=env, capture_output=True, text=True,
                              timeout=task.get("timeout_s", 240))
        rc, stdout, stderr = proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as e:
        timed_out = True
        rc = -1
        stdout = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or "")
        stderr = e.stderr.decode() if isinstance(e.stderr, bytes) else (e.stderr or "")
    wall_ms = int((time.time() - started) * 1000)

    stats = {}
    if stats_path.is_file():
        try:
            stats = json.loads(stats_path.read_text())
        except json.JSONDecodeError:
            stats = {}

    tools = TOOL_RE.findall(ANSI_RE.sub("", stdout))
    answer = (stats.get("answer") or "").strip() or final_answer(stdout)
    reasons = []

    if timed_out:
        reasons.append("timeout")
    if rc != 0:
        reasons.append(f"exit={rc}")

    exp = task.get("expect", {})
    reasons.extend(seed_failures)
    if exp.get("cmd"):
        try:
            chk = subprocess.run(["sh", "-c", exp["cmd"]], cwd=ws, capture_output=True,
                                 text=True, timeout=exp.get("timeout_s", 90))
            if chk.returncode != 0:
                reasons.append("check failed: " + (chk.stdout + chk.stderr).strip()[:200])
        except subprocess.TimeoutExpired:
            reasons.append("check timed out")
    if exp.get("contains") and exp["contains"] not in (answer + "\n" + stdout):
        reasons.append(f"missing output: {exp['contains']!r}")
    if exp.get("min_chars") and len(answer) < int(exp["min_chars"]):
        reasons.append(f"answer too short ({len(answer)} < {exp['min_chars']})")
    req = task.get("require_tools") or []
    if req and not any(t in tools for t in req):
        reasons.append("required tool not used: " + ",".join(req))
    # Tool-call count is a recorded efficiency metric, not a pass/fail gate:
    # a correct result reached in 34 calls is still a correct result.
    over_cap = bool(task.get("max_tool_calls")) and len(tools) > int(task["max_tool_calls"])

    return {
        "task": task["id"],
        "category": task.get("category", ""),
        "run": run_idx,
        "ok": not reasons,
        "reasons": reasons,
        "wall_ms": wall_ms,
        "exit_code": rc,
        "steps": stats.get("steps"),
        "tool_calls": stats.get("tool_calls", len(tools)),
        "over_tool_cap": over_cap,
        "seed_failures": seed_failures,
        "tools": tools,
        "prompt_tokens": stats.get("prompt_tokens"),
        "completion_tokens": stats.get("completion_tokens"),
        "cached_tokens": stats.get("cached_tokens"),
        "answer_chars": len(answer),
        "answer_excerpt": answer[:400],
        "workspace": str(ws),
    }


def git_meta():
    try:
        sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                             capture_output=True, text=True).stdout.strip()
    except Exception:
        sha = ""
    return {"git": sha, "when": datetime.now(timezone.utc).isoformat()}


def print_summary(results):
    print("\n%-22s %-6s %-10s %-8s %-7s %-8s" % ("TASK", "OK", "WALL(ms)", "STEPS", "TOOLS", "TOKENS"))
    print("-" * 70)
    for r in results:
        tokens = (r["prompt_tokens"] or 0) + (r["completion_tokens"] or 0)
        print("%-22s %-6s %-10d %-8s %-7d %-8d" % (
            r["task"], "pass" if r["ok"] else "FAIL", r["wall_ms"],
            str(r["steps"]), r["tool_calls"], tokens))
        if r["reasons"]:
            print("    ! " + "; ".join(r["reasons"])[:200])
    passed = sum(1 for r in results if r["ok"])
    print("-" * 70)
    print(f"{passed}/{len(results)} passed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="./build/belya")
    ap.add_argument("--tasks", default="fast", help="fast|all|id,id,...")
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--out", default="bench/latest.json")
    ap.add_argument("--env-file", default=".env")
    args = ap.parse_args()

    binary = Path(args.bin).resolve()
    if not binary.is_file():
        sys.exit(f"binary not found: {binary}")

    tasks = load_tasks(args.tasks)
    if not tasks:
        sys.exit("no tasks selected")

    base_env = os.environ.copy()
    base_env.update(load_env_file(ROOT / args.env_file))

    results = []
    for run_idx in range(1, args.runs + 1):
        for task in tasks:
            print(f"[bench] run {run_idx} {task['id']} ...", flush=True)
            results.append(run_one(binary, task, run_idx, base_env))

    report = {"meta": {**git_meta(), "binary": str(binary), "model": base_env.get("MODEL_NAME", ""),
                       "tasks": [t["id"] for t in tasks], "runs": args.runs},
              "results": results}

    out = ROOT / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n")
    print_summary(results)
    print(f"\nreport: {out}")


if __name__ == "__main__":
    main()
