# Belya-Evolve: Metamorphic Research Fork & Autonomous Evolution Sandbox

> **NOTICE & SAFETY DIRECTIVE**: This subproject contains the experimental architecture for **autonomous binary self-mutation**, **live hot-reloading**, and **Darwinian self-play**. To preserve the determinism and stability of Core Belya in production environments, **Belya-Evolve must always run in an isolated sandbox** (e.g. ephemeral container, bubblewrap jail, or dedicated VM).

---

## 1. Why Belya-Evolve Is Forked from Core Belya

| Feature | Core Belya (`main`) | Belya-Evolve (`experimental`) |
| :--- | :--- | :--- |
| **Operational Role** | Production software engineering harness | Metamorphic autonomous research sandbox |
| **Self-Modification** | **Strictly Forbidden** (Git audit required) | **Autonomous** (Agent edits its own C source code) |
| **Hot-Reloading** | Manual restart via systemd/CLI | Autonomous (`execve` / hot binary swap) |
| **Execution Environment** | Host OS (macOS / Linux VPS) | Isolated Sandbox (`chroot` / Docker / `/tmp` jail) |
| **Integrity Guard** | 100% deterministic, zero regression | Speculative mutations evaluated by fitness function |

---

## 2. Safety Virtualization Architecture

Self-mutating code is vulnerable to two critical failure modes:
1. **Daemon Bricking**: A bad patch causes a compiler error or segmentation fault, terminating the process irreversibly.
2. **Security & Prompt Injection**: An untrusted prompt or webpage convinces the agent to rewrite its authentication or shell tools.

To mitigate this, Belya-Evolve enforces **The 4-Stage Virtualization Pipeline**:

```
 ┌─────────────────────────────────────────────────────────────┐
 │                      SANDBOX BOUNDARY                       │
 │  BELYA_EVOLVE_SANDBOX=1                                     │
 │                                                             │
 │  1. PROPOSE MUTATION                                        │
 │     Agent writes patch to ephemeral working copy            │
 │                         │                                   │
 │                         ▼                                   │
 │  2. COMPILER & ASAN PRE-FLIGHT                              │
 │     gcc -Wall -Wextra -Werror -fsanitize=address,undefined  │
 │     If compilation fails -> AUTO-REVERT & PENALIZE          │
 │                         │                                   │
 │                         ▼                                   │
 │  3. 27/27 TEST BATTERY VALIDATION                           │
 │     Execute full integration test suite                     │
 │     If any test fails or ASan leaks -> DISCARD MUTATION     │
 │                         │                                   │
 │                         ▼                                   │
 │  4. FITNESS EVALUATION & CONDITIONAL SWAP                   │
 │     Compare binary size, RSS memory, and turn speed         │
 │     Only promote if Fitness(candidate) > Fitness(baseline)  │
 └─────────────────────────────────────────────────────────────┘
```

---

## 3. Darwinian Self-Play (Ouroboros Loop)

The Darwinian self-play engine iteratively evolves Belya's own source code:
1. **Benchmark Challenge Generation**: The harness selects a problem from the universal benchmark battery (Exercism / SWE-bench / synthetic tasks).
2. **Profiling**: The harness profiles execution bottlenecks (e.g., token estimation speed, JSON serialization efficiency, SQLite query latency).
3. **Targeted Refactor**: The agent targets a specific C function (e.g., in `minijson.c` or `belya_agent.c`) to optimize throughput.
4. **Genetic Retention**: If the mutated binary passes all 27 integration tests with 0 leaks and outperforms the parent binary, the mutation is committed to a specialized branch (`evolve/generation-N`).

---

## 4. Running the Sandbox

To execute an autonomous evolution cycle, use the runner script:

```bash
export BELYA_EVOLVE_SANDBOX=1
python3 belya-evolve/evolve_sandbox.py
```

Never execute un-sandboxed self-recompilation on production systems.
