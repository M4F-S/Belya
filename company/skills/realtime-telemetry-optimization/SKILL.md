---
name: realtime-telemetry-optimization
description: Guidelines for optimizing telemetry ingestion throughput
triggers: [telemetry_opt]
---

1. Use SQLite WAL batching
2. Bound JSON payload buffers
3. Pre-aggregate metrics in memory
