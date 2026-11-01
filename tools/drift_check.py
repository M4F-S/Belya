#!/usr/bin/env python3
"""drift_check.py — make project drift a failing command instead of a later argument.

Model: docs/PRD.md holds numbered requirements. Each requirement names the test(s)
that prove it and the source file(s) that implement it. docs/PRD.lock pins the
PRD's hash so any change is deliberate and logged in docs/PRD-CHANGELOG.md.

Exit codes: 0 clean, 1 drift found, 2 bad input.

Usage:
  python3 tools/drift_check.py            # human report
  python3 tools/drift_check.py --json     # machine report
  python3 tools/drift_check.py --orphans  # also flag source files no requirement names
  python3 tools/drift_check.py --update-lock  # re-pin PRD hash (requires today's changelog entry)
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import sys
from pathlib import Path

PRD = Path("docs/PRD.md")
LOCK = Path("docs/PRD.lock")
CHANGELOG = Path("docs/PRD-CHANGELOG.md")
REQ_RE = re.compile(r"^#{2,4}\s+(R-\d{3,4})\s*(?:\[(P\d)\])?\s*(.+?)\s*$")
FIELD_RE = re.compile(r"^\s*[-*]\s*(test|files|status|owner)\s*:\s*(.+?)\s*$", re.I)
REQ_TAG_RE = re.compile(r"REQ[-: ]?(R-\d{3,4})")


def parse_prd(path: Path) -> tuple[dict[str, dict], list[str]]:
    reqs: dict[str, dict] = {}
    errors: list[str] = []
    cur: dict | None = None
    text = path.read_text(encoding="utf-8", errors="replace")
    # example blocks inside HTML comments must not count as real requirements
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    for raw in text.splitlines():
        m = REQ_RE.match(raw.strip())
        if m:
            rid, prio, rtxt = m.group(1), m.group(2) or "P2", m.group(3)
            if rid in reqs:
                errors.append(f"{rid}: defined twice")
            cur = {"id": rid, "prio": prio, "text": rtxt, "test": [], "files": [], "status": "planned"}
            reqs[rid] = cur
            continue
        if cur is None:
            continue
        f = FIELD_RE.match(raw)
        if f:
            key, val = f.group(1).lower(), f.group(2).strip()
            if key == "test":
                cur["test"].extend([v.strip() for v in val.split(",") if v.strip()])
            elif key == "files":
                cur["files"].extend([v.strip() for v in val.split(",") if v.strip()])
            elif key == "status":
                cur["status"] = val.lower()
    return reqs, errors


def prd_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def changelog_has_today(path: Path) -> bool:
    if not path.exists():
        return False
    today = dt.date.today().isoformat()
    return today in path.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--orphans", action="store_true")
    ap.add_argument("--update-lock", action="store_true")
    args = ap.parse_args()

    if not PRD.exists():
        print(f"drift: {PRD} not found — no PRD means no definition of done", file=sys.stderr)
        return 2

    digest = prd_hash(PRD)
    locked = LOCK.read_text().strip().split()[0] if LOCK.exists() else None

    if args.update_lock:
        if not changelog_has_today(CHANGELOG):
            print(f"drift: refusing to re-pin — add a dated entry to {CHANGELOG} first", file=sys.stderr)
            return 1
        LOCK.write_text(f"{digest}  {dt.date.today().isoformat()}\n")
        print(f"drift: PRD locked at {digest[:12]}")
        return 0

    reqs, errors = parse_prd(PRD)
    findings: list[dict] = []

    if locked is None:
        findings.append({"kind": "no-lock", "detail": "docs/PRD.lock missing; run --update-lock"})
    elif locked != digest:
        findings.append({"kind": "prd-changed", "detail": "PRD differs from lock; log the change and re-lock"})

    referenced_tests: set[str] = set()
    for rid, r in reqs.items():
        if r["status"] == "retired":
            continue
        if not r["test"]:
            findings.append({"kind": "untested-requirement", "req": rid, "detail": r["text"][:90]})
            continue
        for t in r["test"]:
            path_part, _, sym = t.partition("::")
            p = Path(path_part)
            if not p.exists():
                findings.append({"kind": "missing-test-file", "req": rid, "detail": path_part})
                continue
            referenced_tests.add(str(p))
            if sym:
                body = p.read_text(encoding="utf-8", errors="replace")
                if sym not in body:
                    findings.append({"kind": "missing-test-symbol", "req": rid, "detail": t})

    # tests that cite a requirement which does not exist
    for p in Path("tests").rglob("*") if Path("tests").is_dir() else []:
        if p.suffix not in {".c", ".h", ".py", ".sh"}:
            continue
        text = p.read_text(encoding="utf-8", errors="replace")
        for tag in set(REQ_TAG_RE.findall(text)):
            if tag not in reqs:
                findings.append({"kind": "unknown-requirement", "detail": f"{p}:{tag}"})

    if args.orphans:
        claimed = {f.lstrip("./") for r in reqs.values() for f in r["files"]}
        for base in ("src", "adapters"):
            root = Path(base)
            if not root.is_dir():
                continue
            for p in root.rglob("*"):
                if p.is_file() and p.suffix in {".c", ".h"}:
                    rel = str(p).lstrip("./")
                    if rel not in claimed and p.name not in {Path(c).name for c in claimed}:
                        findings.append({"kind": "orphan-source", "detail": rel})

    if not reqs:
        findings.append({"kind": "empty-prd", "detail": "no R-### requirements parsed"})

    by_kind: dict[str, int] = {}
    for f in findings:
        by_kind[f["kind"]] = by_kind.get(f["kind"], 0) + 1

    if args.json:
        print(json.dumps({"requirements": len(reqs), "findings": findings, "counts": by_kind}, indent=2))
    else:
        print(f"PRD requirements: {len(reqs)}   findings: {len(findings)}")
        if findings:
            for f in findings:
                loc = f.get("req", "")
                print(f"  DRIFT {f['kind']:<24} {loc:<7} {f['detail']}")
        else:
            print("  clean")

    return 0 if not findings else 1


if __name__ == "__main__":
    sys.exit(main())
