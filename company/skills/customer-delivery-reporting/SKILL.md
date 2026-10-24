---
name: customer-delivery-reporting
description: Customer-facing milestone reporting, SQLite task board aggregation, blocker detection, and project transparency.
triggers: [reporting, delivery, customer-report, progress, milestones, status, blocker]
category: governance
---

# Customer Delivery & Executive Reporting Protocol

1. **Objective Data Aggregation:**
   - Query the SQLite Task Board (`task_list`, `task_get`) to obtain exact task completion percentages and status.
   - Inspect git commit logs (`git_status`, `git_diff`) and agent timeline events for tangible deliverables.
   - Verify that reported accomplishments are grounded in verified test passes and working code.

2. **Customer-Facing Digest Structure:**
   - **Executive Summary:** High-level narrative of current sprint accomplishments and readiness.
   - **Milestone Progress Table:** Feature name, owner role, completion percentage, and status (Completed, In Progress, Blocked).
   - **Deliverables & Artifacts:** Direct links to PRDs, API specs, schemas, and deployed microservices.
   - **Security & Quality Health:** Verification test pass rate, sanitizer status, and vulnerability audit verdicts.
   - **Blockers & Mitigations:** Transparent breakdown of any active risks and mitigation timelines.
   - **Upcoming Milestones:** Explicit deliverables scheduled for the next delivery cycle.

3. **Bi-Directional Customer Communication:**
   - Present information in clear, jargon-free business language suitable for executive stakeholders.
   - Solicit client feedback and sign-off on key milestones (`ask_user`).
