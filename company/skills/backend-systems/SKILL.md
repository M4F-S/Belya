---
name: backend-systems
description: High-throughput API architectures, microservices engineering, connection pooling, and transactional database integrity.
triggers: [backend, api, rest, database, schema, sqlite, postgresql, server, microservice]
category: engineering
---

# Backend Systems & API Architecture Protocol

1. **Deterministic Contracts & Data Validation:**
   - Define strict request/response schemas with unambiguous field types.
   - Enforce rigorous validation at the network boundary; reject malformed payloads immediately with HTTP 400 Bad Request.
   - Standardize error responses: include a machine-readable error code, human message, and request trace ID.

2. **Storage & Transactional Invariants:**
   - Use parameterized prepared statements (`sqlite3_prepare_v2`, parameterized SQL) without exception.
   - Explicitly define transactional boundaries (`BEGIN IMMEDIATE`, `COMMIT`, `ROLLBACK`) to prevent write locks or race conditions.
   - Ensure every query utilizes an index; verify performance using query plan analysis.

3. **Concurrency & Resource Management:**
   - Bound thread/process concurrency to avoid memory exhaustion under spike loads.
   - Enforce deterministic cleanup: free request context buffers, close file descriptors, and release database locks immediately upon response dispatch.
