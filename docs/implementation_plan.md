# Implementation Plan: Belya Speed Optimization & Telegram Delivery Overhaul

Elevate Belya above Claude Code, Codex CLI, and Hermes by fixing the Telegram delivery blocker, optimizing response latency via persistent HTTP keep-alive and adaptive step budgeting, and establishing deterministic synthesis loops.

---

## 1. Problem Diagnostics & Root Cause Analysis

Based on forensic log auditing across **Toy** (Hermes in Docker), **Old Hermes**, and **Belya** (VPS session logs in SQLite and systemd):

1. **The Core Delivery Blocker (`telegram_adapter.c`):**
   - **Content Discard Bug:** When the LLM outputs text alongside tool calls (`resp.has_tool_call == true`), `resp.content` was completely dropped.
   - **Max-Steps Blind Spot:** When `max_steps` (30) expired, Belya did not suppress tools on the final attempt. The model attempted another tool call, leaving `final_response_text` NULL.
   - **The "Action Completed" Fallback:** If `final_response_text` was NULL, Belya sent `"✅ Action completed."` without delivering any of the research findings gathered.
   - **Self-Restart Inability:** Belya diagnosed this bug autonomously on the VPS and patched the file, but could not restart its own systemd daemon without killing its own process.

2. **The Latency Bottleneck (Why replies feel slow):**
   - **Uncapped Tool Loops:** Belya executed up to 30 sequential turns for a single user query. At ~2 seconds per API roundtrip, 20 steps took 40+ seconds.
   - **No HTTP Connection Reuse (CURL):** `model_adapter.c` called `curl_easy_init()` on every single step. This forced a new TCP handshake, TLS 1.3 key negotiation, and parsing of 182KB CA certificates (45 `read()` syscalls) on *every single tool step*.
   - **Unbounded Context Inflation:** Long tool outputs (e.g. multi-KB grep/curl dumps) stayed raw in the message history, bloating the input prompt to 15,000+ tokens and tripling Time-To-First-Token (TTFT).

---

## 2. User Review Required

> [!IMPORTANT]
> **Adaptive Step Budgeting:**
> For Telegram Q&A and research queries, we propose setting the tool limit to **6–8 steps** (instead of 30), with an automatic **Synthesis Phase** triggered at the final step. For explicit multi-file coding missions, the budget remains up to 20 steps. This ensures answers arrive in 5–12 seconds rather than 45–60 seconds.

> [!IMPORTANT]
> **Forced Final Synthesis Turn:**
> When the tool budget runs out, Belya will inject a mandatory user message:
> *"You have reached the tool execution limit. Synthesize your complete, detailed answer and findings to the user now without calling any tools."*
> Furthermore, `tools` will be explicitly disabled (`tools = NULL`) during this call so the model cannot loop or stall.

---

## 3. Proposed Changes

### Component 1: Telegram Adapter (`telegram_adapter.c`)

#### [MODIFY] [telegram_adapter.c](file:///Users/mohamedfathy/Desktop/Privacy%20C/C%20Harness%20and%20Agent/telegram_adapter.c)
- **Progressive Content Accumulation:** Accumulate all assistant text generated during intermediate steps into a dynamic buffer (`dyn_str_append`) so no thought, analysis, or explanation is ever lost.
- **Forced Synthesis on Tool Exhaustion:** When `max_steps <= 1`, disable tool schemas and append a synthesis instruction prompting the model for its complete findings.
- **Typing Indicator Heartbeat:** Send periodic Telegram `typing` actions every 3.5 seconds during tool execution so the user sees continuous activity.
- **Eliminate Generic Fallback:** Never fall back to `"✅ Action completed."` if the user asked an informational question. If the model produced no text, dump the summarized tool observations.
- **Autonomous `/restart` command:** Provide a secure slash command `/restart` allowing authorized users to trigger a clean systemd daemon reload.

---

### Component 2: High-Speed Model Adapter & Connection Keep-Alive (`model_adapter.c`)

#### [MODIFY] [model_adapter.c](file:///Users/mohamedfathy/Desktop/Privacy%20C/C%20Harness%20and%20Agent/model_adapter.c)
- **Persistent CURL Easy Handle (`HTTP Keep-Alive`):** Store a persistent `CURL *` inside `ModelGateway`. Reuse the connection across turns to maintain hot TCP/TLS sessions, shaving ~150-250ms off every single tool call.
- **Fast Timeout & Retry Logic:** Configure sensible per-attempt timeouts (e.g., 30s) and exponential backoff to prevent hanging on unresponsive provider endpoints.
- **Support Explicit Tool Suppression:** Add a flag or parameter to `model_gateway_chat_complete` allowing the harness to pass `tools = NULL` on demand (forcing pure textual synthesis).

---

### Component 3: Agent Reasoning & Context Pruning (`belya_agent.c`)

#### [MODIFY] [belya_agent.c](file:///Users/mohamedfathy/Desktop/Privacy%20C/C%20Harness%20and%20Agent/belya_agent.c)
- **Tool Output Compaction:** Truncate raw tool observation strings stored in `messages` to 2,000 characters if they exceed reasonable bounds, preventing context explosion during research turns.
- **Adaptive Step Budgeting:** Add `belya_agent_step_forced_text()` to execute a step where tool calling is disabled and conversational output is guaranteed.
- **Parallel Tool Execution:** Ensure that when models dispatch multiple tool calls in a single turn, all independent tool calls execute immediately in sequence before calling the model again.

---

### Component 4: Deployment & Host Synchronization

#### [MODIFY] VPS Deployment (`/opt/belya`)
- Pull the completed changes into `/opt/belya`.
- Compile cleanly with `gcc -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L`.
- Perform a clean `systemctl restart belya` to replace the 19-hour-old process.
- Run live test queries on Telegram to verify speed and output delivery.

---

## 4. Verification Plan

### Automated Test Suite
- Run `make clean && make test`:
  - Verify all 20 existing stress tests pass (100%).
  - Add test case `test_forced_synthesis_on_step_limit` verifying that when step limits are reached, the agent cleanly transitions to text generation.
  - Add test case `test_curl_connection_reuse` validating that the persistent connection pool handles consecutive queries without leaks.
- Run `make benchmark`:
  - Verify all 30/30 benchmark tasks pass in under 500ms.
  - Verify memory footprint remains < 10 MB RSS.

### Live Telegram Verification (VPS)
1. Send message: `"Are you online?"` -> Expect instant direct greeting within 1.5 seconds.
2. Send message: `"If we compare Belya to claude code and Codex CLI and hermes agent, how do they compare specially on benchmarks?"` -> Expect:
   - Dynamic typing indicator / status.
   - 3–5 high-density research steps.
   - Comprehensive multi-paragraph benchmark comparison delivered to Telegram within 8–15 seconds (no more silent `"Action completed"`).
