---
name: vps-deploy
description: Protocols for deploying updates to Belya and Almaz daemons on remote VPS.
triggers: [vps, deploy, systemd, restart, vps-deploy]
category: devops
---

# VPS Deployment Protocol

1. Remote Target:
   - Host: `root@187.124.2.26`
   - Belya Champion: `/opt/belya` managed by `belya.service` (branch `main`)
   - Almaz Challenger: `/opt/almaz` managed by `almaz.service` (branch `evolve/almaz`)
2. Safe Deployment Steps:
   - Ensure local changes are committed and pushed to `origin main`.
   - Update Belya:
     `cd /opt/belya && git pull origin main && make clean && make && systemctl restart belya.service`
   - Update Almaz:
     `cd /opt/almaz && git merge main && make clean && make && systemctl restart almaz.service`
3. Verification:
   - Run `systemctl is-active belya.service almaz.service`
   - Inspect journal logs: `journalctl -u belya.service -n 20 --no-pager`
