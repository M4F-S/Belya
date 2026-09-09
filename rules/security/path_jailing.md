---
category: security
description: Strict filesystem path jailing, directory traversal rejection, and write boundary containment.
triggers: [security, path, jailing, sandbox, permission]
---

# Workspace Path Jailing Protocol
1. Workspace Boundaries: All file creation and editing must be strictly confined to the workspace root.
2. Traversal Defense: Any path containing `..` must be immediately rejected with `Path traversal denied`.
3. Whitelisted Reads: Reads outside the workspace root are restricted strictly to whitelisted system inspection paths (`/tmp/`, `/proc/`, `/etc/os-release`).
