---
name: data-analytics-bi
description: Business intelligence, telemetry analytics, statistical metrics, and executive data visualizations.
triggers: [analytics, bi, telemetry, metrics, kpi, trends, retention, insights]
category: data
---

# Data Analytics & Business Intelligence Protocol

1. **Analytical Invariants:**
   - Always query read-only replicas or analytics snapshots; never run blocking queries against transactional databases.
   - Standardize KPI definitions across all reports (DAU/MAU, conversion rate, churn, p95/p99 latency).
   - Use window functions and aggregated rollups to minimize query computation time.

2. **Statistical Rigor:**
   - Verify sample sizes and confidence intervals before drawing comparative conclusions.
   - Clearly delineate seasonality, sampling anomalies, and genuine statistical shifts.

3. **Executive Visualization:**
   - Structure findings into concise executive dashboards with clear chart specs.
   - Accompany raw metrics with actionable business recommendations.
