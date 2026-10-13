---
name: codebase-onboarding
description: Rapid autonomous repository reconnaissance, architecture mapping, and codebase onboarding.
triggers: [onboard, onboarding, repo-map, explore-repo, map-architecture]
category: exploration
---

# Codebase Onboarding & Architecture Reconnaissance Protocol

## Objective
Quickly explore, index, and understand an unfamiliar codebase with zero assumptions. Produce a clear, actionable architectural map without overwhelming the context window.

## Execution Steps

1. **Topology Discovery**:
   - Run `list_dir` on the project root to inspect the directory layout.
   - Identify configuration manifests: `Makefile`, `CMakeLists.txt`, `Cargo.toml`, `package.json`, `go.mod`, `pyproject.toml`.
   - Scan for repository guidelines: `README.md`, `CONTRIBUTING.md`, `ARCHITECTURE.md`, `AGENTS.md`.

2. **Entry-Point Resolution**:
   - Locate the primary application entry points (`main.c`, `main.go`, `index.ts`, `app.py`).
   - Use `search_files` with `pattern="int main("` or relevant entry signature.
   - Read the entry point to trace initialization, configuration loading, and the main event loop.

3. **Symbol & Call-Graph Indexing**:
   - Use `goto_definition` to inspect foundational structs, types, interfaces, and core abstractions.
   - Use `find_callers` on core dispatch functions to map subsystem dependency chains.

4. **Test & Build Discovery**:
   - Identify build commands and test suites (`make test`, `cargo test`, `npm test`, `pytest`).
   - Run a dry-run or quick test execution via `bash` to verify the baseline build passes before making modifications.

5. **Architecture Brief Synthesis**:
   - Document:
     - **Stack & Toolchain**: Languages, compilers, dependencies, runtime requirements.
     - **Core Data Flows**: How input flows into processing pipelines and outputs.
     - **State & Storage**: File formats, SQLite databases, memory lifecycles.
     - **Key Invariants**: Memory ownership, concurrency models, security boundaries.
