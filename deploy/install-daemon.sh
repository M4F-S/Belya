#!/bin/bash
# deploy/install-daemon.sh - make the running daemon match this repo.
#
# Idempotent. Creates the unprivileged account, hands it ownership of the data
# directory, installs the systemd drop-in that drops privileges, and restarts.
# Run as root on the host; safe to re-run after a binary swap.
#
#   ./deploy/install-daemon.sh            # apply
#   ./deploy/install-daemon.sh --check    # report drift, change nothing
set -euo pipefail

SERVICE="${SERVICE:-belya}"
USER_NAME="${USER_NAME:-belya}"
DATA_DIR="${DATA_DIR:-/opt/belya}"
UNIT_DIR="/etc/systemd/system/${SERVICE}.service.d"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DROPIN_SRC="${HERE}/systemd/20-user.conf"
DROPIN_DST="${UNIT_DIR}/20-user.conf"

check() {
    local drift=0
    if ! id "$USER_NAME" >/dev/null 2>&1; then
        echo "DRIFT: user ${USER_NAME} does not exist"; drift=1
    fi
    if ! diff -q "$DROPIN_SRC" "$DROPIN_DST" >/dev/null 2>&1; then
        echo "DRIFT: ${DROPIN_DST} differs from repo"; drift=1
    fi
    if [ "$(systemctl show "$SERVICE" -p User --value)" != "$USER_NAME" ]; then
        echo "DRIFT: unit not running as ${USER_NAME}"; drift=1
    fi
    [ "$drift" -eq 0 ] && echo "in sync"
    return "$drift"
}

if [ "${1:-}" = "--check" ]; then
    check
    exit $?
fi

[ "$(id -u)" -eq 0 ] || { echo "must run as root" >&2; exit 1; }

id "$USER_NAME" >/dev/null 2>&1 || \
    useradd --system --create-home --home-dir "/home/${USER_NAME}" --shell /bin/bash "$USER_NAME"

# The daemon writes its sqlite database, turbo marker and backups here.
chown -R "${USER_NAME}:${USER_NAME}" "$DATA_DIR"

install -d -m 755 "$UNIT_DIR"
install -m 644 "$DROPIN_SRC" "$DROPIN_DST"

systemctl daemon-reload
systemctl restart "$SERVICE"
for _ in $(seq 1 15); do
    [ "$(systemctl is-active "$SERVICE")" = active ] && break
    sleep 1
done

state=$(systemctl is-active "$SERVICE")
uid=$(ps -o uid= -p "$(systemctl show "$SERVICE" -p MainPID --value)" 2>/dev/null | tr -d ' ')
echo "service=${state} uid=${uid}"
[ "$state" = active ] || { echo "FAILED: ${SERVICE} is ${state}" >&2; exit 1; }
[ "$uid" != "0" ] || { echo "FAILED: still running as root" >&2; exit 1; }
echo "ok: ${SERVICE} runs as ${USER_NAME}"
