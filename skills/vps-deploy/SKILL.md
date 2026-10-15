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
| Active artifact | `belya` (chattr +i) | `almaz` (chattr +i) — the slot the watchdog `execv`s, resolved from `.deploy_state` |
| Provenance | — | `.deploy_meta` (regenerate with `/usr/local/libexec/almaz/deploy_meta.sh`) |

## Build vs deploy — they are different things now

The build writes **`build/<name>`**. The deployed artifact at the repo root is *not*
build output: it carries the immutable flag and is what systemd runs.

- ✅ `make` / `make clean` — build and clean **without** touching the daemon
- ✅ `make deploy` — publish `build/<name>` to the deployed path: it clears the
  immutable flag, installs atomically, restores the flag, and restarts the service
  **only if the content actually changed**
- ✅ `make release` — the whole ritual: `clean → build → test → deploy`
- ❌ `make clean && make && systemctl restart <svc>` — the old instruction. With the
  immutable flag this fails at `rm`/link with `Operation not permitted`, and a plain
  `make` no longer produces `./<name>` at all.

## Deploy checklist

1. Re-anchor before editing anything: `git rev-parse HEAD` (the tree moves under you).
2. `make release` (or `make deploy` if you already built).
3. Verify, in this order:
   - `sha256sum build/<name> ./<name>` — identical after deploy
   - `sha256sum /proc/$(systemctl show <svc> -p MainPID --value)/exe` — the *running*
     image, which must match the artifact (a rebuild without restart shows up here)
   - `systemctl is-active <svc>`; Almaz only: `./almaz --selfest`
   - Almaz only: `/usr/local/libexec/almaz/deploy_meta.sh` then confirm every declared
     slot hash equals the file on disk
4. Never hand-edit `.deploy_meta` — regenerate it with the generator.

## Hard rules

- **Never** `chattr -i` and leave it cleared: unlock → change → relock, and put the
  relock in a `trap … EXIT` if you are scripting it.
- **Never** delete `almaz-watchdog`: it is a deployed artifact; its absence is a
  203/EXEC restart loop.
- **The two repos are forks, not twins** (different test counts, different harness
  sizes). Never blind-copy a change between them — apply and test per repo.
- **Pushing from this shell does not work**: the unit runs with `ProtectHome=yes`, so
  `/root/.ssh` (and the git host alias in it) is invisible inside the sandbox. Commit
  locally, report the commit hash, and let the operator push — or use the credential
  mechanism if it has been added to the unit.
- Rollback target must be **content-distinct**: `promote.sh` refuses to run when
  ACTIVE/LKG/STAGING are byte-identical.
