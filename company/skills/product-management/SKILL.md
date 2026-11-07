---
name: product-management
description: Product Requirement Documents (PRDs), atomic story breakdown, task board dependency graphs, and plan-mode discovery.
triggers: [product, prd, roadmap, backlog, story, requirements, plan, task]
category: product
---

# Product Management & Backlog Protocol

1. **Structured PRD Formulation:**
   - Define Problem Statement, Target Audience, and Core Objectives.
   - Specify Functional Requirements with unambiguous input/output specifications.
   - Enumerate Non-Functional Requirements (latency budgets, memory limits, browser support, security invariants).

2. **Atomic Task Board Graphing:**
   - Decompose features into atomic, independent tasks on the SQLite task board (`task_create`).
   - Define explicit parent/child dependencies to govern the engineering pipeline sequence.
   - Set unambiguous Acceptance Criteria for every task before setting state to in_progress.

3. **Plan Mode Sandboxing:**
   - Enter Plan Mode (`enter_plan_mode`) during requirements discovery and architectural mapping.
   - Verify feasibility and resource estimates before authorizing Builder/Engineering dispatch.
