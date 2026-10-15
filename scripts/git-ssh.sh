#!/bin/sh
# git-ssh.sh — the SSH transport git uses from inside the service sandbox.
#
# The unit runs with ProtectHome=yes, so /root/.ssh (and the `Host github.com-qiyyas`
# alias inside it) is invisible to the daemon. systemd instead reads the deploy key
# outside the sandbox and injects it via LoadCredential=, which lands in
# $CREDENTIALS_DIRECTORY and stays readable here. Git is pointed at this script with
# GIT_SSH_COMMAND (see the unit drop-in).
#
# The repo's remote keeps the `github.com-qiyyas` alias; HostName= below resolves it to
# the real host, so the operator's own git/ssh setup is left completely untouched.
set -eu

d="${CREDENTIALS_DIRECTORY:-}"
if [ -z "$d" ] || [ ! -r "$d/git_ssh_key" ]; then
    echo "git-ssh.sh: no deploy key credential available." >&2
    echo "  This script only works inside the service unit (LoadCredential=git_ssh_key)." >&2
    echo "  From an interactive shell, push the normal way instead." >&2
    exit 1
fi

# PrivateTmp=yes gives the unit its own /tmp, so the known_hosts file below is private
# to the service and does not leak into the host's.
exec ssh -i "$d/git_ssh_key" \
    -o IdentitiesOnly=yes \
    -o HostName=github.com \
    -o UserKnownHostsFile=/tmp/git_known_hosts \
    -o StrictHostKeyChecking=accept-new \
    "$@"
