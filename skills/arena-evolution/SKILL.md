---
name: arena-evolution
description: Autonomous 4-phase audit, mutation, benchmark, and reflection protocol for self-evolution.
triggers: [evolution, almaz, arena, benchmark, mutate, audit]
category: evolution
---

# Autonomous Arena Evolution Protocol

1. 4-Phase Evolution Cycle:
   - Phase 1: Codebase Audit & Metacognitive Review (identify bottlenecks, memory leaks, algorithmic inefficiencies).
   - Phase 2: Hypothesis & Controlled Mutation (apply focused diff to isolated sandbox branch).
   - Phase 3: Arena Benchmark Execution (Exercism/Aider universal test suite + RSS/speed measurement).
   - Phase 4: Reflection & Promotion (if candidate surpasses champion across all metrics, promote to production).
2. Safety Sandbox:
   - Always isolate experimental mutations in `belya-evolve/` or `evolve/almaz`.
   - Never mutate production branches directly without 100% benchmark and test verification.
