---
name: data-engineering
description: High-throughput data ingestion, schema migration, WAL indexing, and analytical notebook pipelines.
triggers: [data, pipeline, etl, schema, migration, ingestion, parquet, notebook]
category: data
---

# Data Engineering Protocol

1. **Schema Evolution & Migration:**
   - Author idempotent migration scripts: apply changes in strictly ordered, versioned steps.
   - Enforce WAL mode (`PRAGMA journal_mode = WAL`) and memory limits (`PRAGMA cache_size`, `PRAGMA mmap_size`).
   - Create covering indexes for all frequent filter and join predicates.

2. **Ingestion & Transformation Invariants:**
   - Process streams and batches in chunks; avoid loading unbounded datasets into memory.
   - Validate incoming records against schema types; route malformed rows to dead-letter storage.
   - Design data models to support idempotency: repeated runs should yield identical state.

3. **Notebook & Analytical Tooling:**
   - Use `edit_notebook` to prototype data transformations, schema checks, and analytical pipelines.
   - Keep notebook cells deterministic and reproducible from top to bottom.
