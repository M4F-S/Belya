---
name: devops-cloud-operations
description: Container builds, systemd daemon lifecycle, remote execution, and continuous deployment workflows.
triggers: [devops, cloud, docker, container, ci/cd, deployment, systemd, infrastructure]
category: operations
---

# DevOps & Cloud Operations Protocol

1. **Immutable Containerization:**
   - Multi-stage Docker builds: separate build dependencies from runtime scratch/alpine containers.
   - Run all containers as non-root unprivileged users.
   - Pin specific base image digests; avoid unbounded `latest` tags.

2. **Daemon & Process Management:**
   - Manage long-running daemons in persistent background PTY sessions (`manage_task`).
   - Configure systemd service units with sandbox hardening (`ProtectSystem=strict`, `ProtectHome=true`, `PrivateTmp=true`).
   - Monitor process exit codes and maintain automated watchdog restart policies.

3. **Remote Deployment & Cron:**
   - Execute remote piped commands over SSH/Docker without host shell quoting pitfalls (`exec_backend`).
   - Configure automated maintenance jobs via Belya cron engine (`cron_create`, `cron_tick`).
