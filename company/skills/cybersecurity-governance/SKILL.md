---
name: cybersecurity-governance
description: Vulnerability sweeping, threat modeling, path traversal prevention, secret scanning, and security invariants.
triggers: [security, audit, vulnerability, injection, sqli, sanitization, secret, threat]
category: security
---

# Cybersecurity & Threat Governance Protocol

1. **Automated Variant Sweeping:**
   - Execute Mythos variant audits (`audit_security_variants`) across all source trees prior to release.
   - Detect dangerous C functions (`sprintf`, `strcpy`, `strcat`, `system` with untrusted args) and enforce bounded safe alternatives.
   - Detect SQL concatenation patterns and verify that parameterized bindings are utilized everywhere.

2. **Filesystem & Network Jailing:**
   - Verify that all filesystem paths are jailed within the designated workspace root (`is_path_jailed`).
   - Block path traversal sequences (`../`, null byte injection, symlink escapes).
   - Prevent Server-Side Request Forgery (SSRF) by verifying IP addresses against private network boundaries.

3. **Secret Hygiene:**
   - Scan repositories and git diffs for API keys, private tokens, certificates, and credentials.
   - Enforce environment variable injection for secrets; ensure `.env` is ignored by Git.
