---
name: benchmark-profiler
description: Rigorous latency, resident memory (RSS), and performance profiling for systems software.
triggers: [benchmark-profile, profile-latency, measure-rss, benchmark-run, profile-speed]
category: performance
---

# Benchmark & Performance Profiling Protocol

## Objective
Measure execution latency, cold-start startup overhead, and peak resident set size (RSS) with mathematical rigor. Generate reproducible benchmark scorecards comparing baseline vs optimized implementations.

## Execution Steps

1. **Test Environment Preparation**:
   - Ensure the system is in a stable, non-throttled state.
   - Compile code with standard production optimization flags (`-O3` or `-O2`) and stripped debug symbols where appropriate.
   - For memory leak audits, use `-fsanitize=address,undefined` separately (note: sanitizers add ~2-3x memory overhead; profile production builds without sanitizers for true RSS).

2. **Metrics Collection**:
   - **Cold Start Latency**: Measure startup to exit time across 10 iterations (`/usr/bin/time` or clock_gettime `CLOCK_MONOTONIC`).
   - **Peak RSS**:
     - On Linux: Parse `/proc/[pid]/status` (`VmHWM` / `VmRSS`) or `/proc/self/statm`.
     - On macOS: Parse `getrusage(RUSAGE_CHILDREN, ...)` `ru_maxrss` (in bytes on macOS, KB on Linux).
   - **Throughput / QPS**: Measure operations handled per second under sustained load.

3. **Multi-Run Statistical Rigor**:
   - Discard the first run if disk caches need warming (or report cold vs warm separately).
   - Compute mean, median, standard deviation, and p99 latency across at least 10 trials.

4. **Standardized Scorecard Output**:
   - Report results in a Markdown table:
     - Target / Version
     - Binary Size
     - Cold Start (ms)
     - Active RSS (MB)
     - Test Suite Runtime (s)
     - Pass Rate (%)
