---
name: vps-deploy
description: Protocols for deploying updates to Belya and Almaz daemons on remote VPS.
triggers: [vps, deploy, systemd, restart, vps-deploy]
category: devops
---

# VPS Deployment Protocol (verified 2026-10-15)

## Ground truth

| | Belya | Almaz |
|---|---|---|
| Path | `/opt/charness`, also reachable as `/opt/belya` (symlink) | `/opt/almaz` |
| Branch | `main` (== `origin/main`) | `main` (== `origin/main`) |
| Unit | `belya.service` | `almaz.service` (supervised by `almaz-watchdog`) |
| Active artifact | `belya` (chattr +i) | `almaz` (chattr +i) — the slot the watchdog `execv`s, from `.deploy_state` |
| Provenance | — | `.deploy_meta` (regenerate with `/usr/local/libexec/almaz/deploy_meta.sh`) |

## Build vs deploy — they are different things

The build writes **`build/<name>`**. The deployed artifact at the repo root is *not* build
output: it carries the immutable flag and is what systemd runs.

- ✅ `make` / `make clean` — build and clean **without** touching the daemon
- ✅ `make deploy` — publish `build/<name>` to the deployed path: clear the immutable
  flag, install atomically, restore the flag, restart **only if the content changed**
- ✅ `make release` — the whole ritual: `clean → build → test → deploy`
- ❌ `make clean && make && systemctl restart <svc>` — the old instruction. The immutable
  flag makes `rm`/link fail with `Operation not permitted`, and a plain `make` no longer
  produces `./<name>` at all.

**Publishing is an OPERATOR action.** The service runs with
`CapabilityBoundingSet=~CAP_LINUX_IMMUTABLE`, so from inside the unit `make deploy` cannot
clear the flag and now refuses with an explicit message instead of failing halfway. Run it
as root outside the service. (That capability drop is deliberate: it is what makes the
tamper lock real, rather than something the agent can remove itself.)

## Deploy checklist

1. Re-anchor before editing anything: `git rev-parse HEAD` (the tree moves under you).
2. `make release` (or `make deploy` if you already built) — run it as the operator.
3. Verify, in this order:
   - `sha256sum build/<name> ./<name>` — identical after deploy
   - `sha256sum /proc/$(systemctl show <svc> -p MainPID --value)/exe` — the *running*
     image, which must match (a rebuild without restart shows up here)
   - `systemctl is-active <svc>`; Almaz only: `./almaz --selfest`
   - Almaz only: run `deploy_meta.sh`, then confirm every declared slot hash equals the file
4. Never hand-edit `.deploy_meta` — regenerate it with the generator.

## Pushing to GitHub

Push **works** from inside the service (fixed 2026-10-15). `ProtectHome=yes` hides
`/root/.ssh`, so the unit instead injects the deploy key with
`LoadCredential=git_ssh_key` and sets `GIT_SSH_COMMAND=<repo>/scripts/git-ssh.sh`; the
wrapper resolves the `github.com-qiyyas` alias to the real host. So:

- commit, tag and push from your own shell as usual
- if a push fails, say so plainly — never claim a push you did not perform
- the operator's own ssh setup is untouched; this is only the sandbox path

## Hard rules

- **Never** `chattr -i` and leave it cleared. Inside the service you cannot do it at all
  any more; as the operator, unlock → change → relock, with the relock in a
  `trap … EXIT` when scripting it.
- **Never** delete `almaz-watchdog`: a deployed artifact whose absence is a 203/EXEC loop.
- **The two repos are forks, not twins** (different test counts, different harness sizes).
  Never blind-copy a change between them — apply and test per repo.
- Rollback targets must be **content-distinct**: `promote.sh` refuses to run when
  ACTIVE/LKG/STAGING are byte-identical.
- **A sandbox copy is not production-equivalent**: `cp -a` does not carry `chattr` flags,
  and `nsenter` does not adopt the target's environment or capabilities. Never test a
  deploy against the production path — override `DEPLOY_BIN` to a throwaway file.
