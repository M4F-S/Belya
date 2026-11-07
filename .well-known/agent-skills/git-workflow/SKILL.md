---
name: git-workflow
description: Strict git repository workflow, atomic commits, rollback guards, and semantic version tagging.
triggers: [git, commit, tag, branch, merge, rollback]
category: workflow
---

# Git Workflow & Integrity Protocol

1. Atomic Commits:
   - Commit logically coherent units of work with standard conventional commit prefixes (`feat:`, `fix:`, `refactor:`, `test:`).
2. Verification Gate:
   - Never commit broken builds. Always run `make test` before `git commit`.
3. Tagging:
   - Explicitly tag milestone versions (`git tag -a vX.Y.Z -m "Release vX.Y.Z"`) and push tags (`git push origin --tags`).
4. Instant Rollback:
   - Use checkpointing (`git stash create` / `git rev-parse HEAD`) before risky operations so any failed mutation can be immediately rolled back.
