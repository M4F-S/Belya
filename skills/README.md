# Belya Skills Architecture

This directory houses Belya's **core autonomous software engineering skills**.

---

## 1. Directory Scopes: `skills/` vs `company/skills/`

| Directory | Scope & Purpose | Target Engine |
| :--- | :--- | :--- |
| **`skills/`** (This directory) | **Core Autonomous SE Skills**: Fundamental engineering, debugging, code review, safety guardrails, benchmarking, and host/VPS operations used directly by Belya in autonomous execution. | Core Agent (`belya_agent_load_disk_skills`) & `.well-known/agent-skills/` |
| **`company/skills/`** | **Enterprise Departmental Role Skills**: 21 SDLC role-specialized skillsets (e.g., Backend Systems, Frontend Engineering, QA Test Automation, DevOps Cloud Ops, Product Management, Security Governance, Customer Success) used in the simulated enterprise engineering organization. | Multi-Agent Company Simulation (`belya_company.c`) |

---

## 2. Core Skills Inventory (`skills/`)

- **`arena-evolution/`**: Autonomous benchmarking against standard SWE tasks and automated test suite evolution.
- **`benchmark-profiler/`**: Performance profiling, latency analysis, and throughput measurement.
- **`c99-safety/`**: Strict ISO C99 memory safety, bounds checking, deterministic cleanup, and zero memory leaks under AddressSanitizer (ASan).
- **`codebase-onboarding/`**: Deep codebase exploration, symbol dependency mapping, and architecture synthesis.
- **`git-workflow/`**: Git hygiene, atomic commits, pull request orchestration, and release tagging.
- **`interactive-diff-review/`**: Semantic diff analysis, change impact radius calculation, and regression prevention.
- **`safe-refactor-guard/`**: Verification-gated code refactoring with test-driven rollback guards.
- **`vps-deploy/`**: Remote Linux daemon deployment, systemd service management, hot backup generation, and health auditing.
- **`web-research/`**: Autonomous technical documentation lookups and external web knowledge retrieval.

---

## 3. Format & Discovery Specification

Each skill directory contains a `SKILL.md` specification with YAML frontmatter:

```markdown
---
name: skill-name
description: Clear, single-sentence summary of the capability.
triggers: comma, separated, keyword, activation, phrases
---

# Instructions
Step-by-step procedures, tool call patterns, and behavioral constraints.
```

Belya dynamically ingests these definitions at startup via `belya_agent_load_disk_skills()`, mirrors new skills to disk, and matches prompt intent to inject the most relevant instructions directly into execution context.
