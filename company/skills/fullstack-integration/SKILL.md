---
name: fullstack-integration
description: End-to-end integration patterns, client-server contract synchronization, worktree isolation, and full-stack debugging.
triggers: [fullstack, integration, end-to-end, worktree, contract, client-server]
category: engineering
---

# Full-Stack Integration Protocol

1. **Contract Synchronization:**
   - Maintain single source of truth for API contracts between frontend client and backend services.
   - Synchronize TypeScript/JSON schemas with backend models to prevent deserialization bugs.
   - Test round-trip serialization and deserialization for every data field.

2. **Worktree Sandboxing:**
   - Execute complex multi-file architectural changes in ephemeral Git worktrees (`enter_worktree`).
   - Run integration checks within the worktree sandbox before merging back to main branch (`leave_worktree`).

3. **End-to-End Tracing:**
   - Verify that client error states faithfully propagate backend validation alerts.
   - Ensure latency budgets are maintained end-to-end (<100ms API roundtrip).
