---
name: agent-wellbeing-evolution
description: Agent cognitive health monitoring, context compactor optimization, loop prevention, and continuous tool/skill synthesis.
triggers: [wellbeing, evolution, almaz, health, context, compactor, loop, skill-curation, mcp-synthesis]
category: governance
---

# Agent Well-Being & Capability Evolution Protocol (Almaz Core)

1. **Cognitive Health & Memory Defense:**
   - **Context Window Health:** Track token accumulation across subagent turns; ensure 5-tier compaction and ROMA recursive context firewalls prevent buffer overflow.
   - **Loop Breaker & Circuit Health:** Monitor the harness loop detector; if an agent repeats tool calls or encounters consecutive errors, intervene before the turn limit is exhausted.
   - **Resource Proprioception:** Monitor process VmRSS footprint and memory allocations; enforce the strict <25 MB arena memory budget.

2. **Continuous Procedural Skill Curation:**
   - When recurring engineering challenges or domain-specific needs are detected, author modular `SKILL.md` files directly in `skills/<name>/SKILL.md` using `save_skill`.
   - Maintain the progressive disclosure manifest: ensure high-salience skills are surfaced while keeping prompt overhead minimal.

3. **Dynamic Tool Synthesis & MCP Expansion:**
   - If an agent requires specialized capabilities (e.g. custom parsing, domain APIs), synthesize a dynamic tool via `define_tool` with a safe, tested backing script.
   - Synthesize repo-level MCP servers (`synthesize_mcp_server`) to dynamically bridge external tools without modifying the core C99 harness.
