---
name: web-research
description: Autonomous documentation retrieval, API endpoint exploration, and web research using native REST tools.
triggers: [web-research, api-scrape, fetch-docs, curl-docs, web-docs]
category: research
---

# Web Research & API Documentation Protocol

## Objective
Retrieve external documentation, API specifications, and online resources using native HTTP tools (`fetch_url`) safely and efficiently, without polluting context or triggering SSRF guards.

## Execution Steps

1. **Target Identification & Protocol Compliance**:
   - Verify the target URL starts with `http://` or `https://`.
   - Never query forbidden private/loopback addresses (`localhost`, `127.0.0.1`, `10.0.0.0/8`, `192.168.0.0/16`, cloud metadata services) — the SSRF guard will strictly block and reject them.
   - For public APIs (e.g. GitHub API, PyPI, Crates.io, HackerNews), set standard headers such as `{"Accept": "application/json", "User-Agent": "Belya-Agent/1.0"}`.

2. **Fetching Content**:
   - Use `fetch_url` with `method="GET"` and target URL.
   - For authenticated endpoints or specific API payloads, supply custom headers or JSON body.
   - Inspect the returned HTTP status code. If HTTP >= 400 is returned, inspect the error message and do not assume success.

3. **Content Filtering & Synthesis**:
   - Filter raw HTML/JSON into actionable technical insights.
   - Extract function signatures, endpoints, schemas, parameters, and error codes.
   - Discard boilerplate navigation bars, footers, tracking scripts, and stylesheets.

4. **Actionable Output**:
   - Synthesize findings into clear engineering notes:
     - Exact API routes and request/response examples.
     - Prerequisite headers, authentication mechanisms, and rate limits.
     - Integration code snippets tailored to the current codebase.
