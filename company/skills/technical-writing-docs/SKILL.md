---
name: technical-writing-docs
description: Developer documentation, API reference generation, Mermaid diagramming, operator runbooks, and changelogs.
triggers: [documentation, docs, api, mermaid, runbook, changelog, guide, reference]
category: documentation
---

# Technical Writing & Documentation Protocol

1. **Exhaustive API Documentation:**
   - Document every endpoint: HTTP method, path, authentication, query parameters, request body, and response payload.
   - Provide concrete curl and code snippets for both success (200/201) and error (400/401/404/500) cases.
   - Enforce consistency with actual codebase types and schemas.

2. **Visual Architecture Topologies:**
   - Use Mermaid flowcharts, sequence diagrams, and class diagrams to visualize complex data flows.
   - Keep diagrams crisp, labeled, and syntactically valid.

3. **Operator Runbooks & Changelogs:**
   - Author step-by-step operator setup, configuration, and troubleshooting procedures.
   - Maintain CHANGELOG.md adhering strictly to Keep a Changelog standards (Added, Changed, Deprecated, Removed, Fixed, Security).
