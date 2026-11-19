#!/bin/bash
# Belya deploy provenance (audit remediation Phase 2.4).
#
# Metadata only: no slots, no promotion logic, no restart. The only write is
# .deploy_meta in the deploy directory.
#
# Why this exists: a SHA in a sidecar file describes what someone intended to
# deploy. The compiled-in SHA (belya --version) describes what is running. This
# file records both — ACTIVE_SHA vs RUNNING_SHA from /proc/<pid>/exe, and
# VERSION_MATCH comparing the binary's own claim with the git tree — so a
# disagreement is visible instead of implied.
#
# Usage: tools/deploy_meta.sh [deploy-dir]   (default: repo root)
set -u

CDIR="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
OUT="$CDIR/.deploy_meta"
STAMP=$(date -u +%Y%m%d-%H%M%S)

sha() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }

PREV_SEQ=0
PREV_ACTIVE=""
PREV_VERSION=""
if [ -f "$OUT" ]; then
  PREV_SEQ=$(sed -n 's/^DEPLOY_SEQ=//p' "$OUT" 2>/dev/null | head -1)
  PREV_ACTIVE=$(sed -n 's/^ACTIVE_SHA=//p' "$OUT" 2>/dev/null | head -1)
  PREV_VERSION=$(sed -n 's/^BINARY_VERSION=//p' "$OUT" 2>/dev/null | head -1)
  case "$PREV_SEQ" in ''|*[!0-9]*) PREV_SEQ=0 ;; esac
fi

ACTIVE=$(sha "$CDIR/belya")
LKG=$(sha "$CDIR/belya.lkg")

# The unit is the authority on which process is serving.
PID=$(systemctl show belya -p MainPID --value 2>/dev/null)
case "$PID" in ''|*[!0-9]*) PID="" ;; esac
RUNNING=""
if [ -n "$PID" ]; then
  RUNNING=$(sha "/proc/$PID/exe")
  [ -n "$RUNNING" ] || PID=""
fi

MATCH="no"
if [ -n "$RUNNING" ] && [ "$RUNNING" = "$ACTIVE" ]; then MATCH="yes"; fi

# A version probe must never fall into the REPL: bounded time, stdin closed.
VERSION=$(cd "$CDIR" && timeout 5 ./belya --version </dev/null 2>/dev/null | head -1)
[ -n "${VERSION:-}" ] || VERSION="unsupported"

HEADFULL=$(git -C "$CDIR" rev-parse HEAD 2>/dev/null || echo none)
SHORT=$(git -C "$CDIR" rev-parse --short HEAD 2>/dev/null || echo none)

# A no-op deploy must not inflate the counter.
if [ -n "$PREV_ACTIVE" ] && [ "$PREV_ACTIVE" = "$ACTIVE" ]; then
  SEQ=$PREV_SEQ
else
  SEQ=$(( PREV_SEQ + 1 ))
fi

# Does the binary's own claim agree with the tree it is supposed to come from?
VMATCH="unknown"
if [ "$VERSION" != "unsupported" ] && [ "$SHORT" != "none" ]; then
  if printf '%s\n' "$VERSION" | grep -q "belya $SHORT "; then
    VMATCH="yes"
  else
    VMATCH="no"
  fi
fi

TMP="$OUT.new"
{
  echo "# Belya deploy provenance — recorded $STAMP UTC"
  echo "RECORDED=$STAMP"
  echo "GIT_HEAD=$HEADFULL"
  echo "GIT_HEAD_SHORT=$SHORT"
  echo "DEPLOY_SEQ=$SEQ"
  echo "ACTIVE_SHA=${ACTIVE:-missing}"
  echo "RUNNING_SHA=${RUNNING:-unknown}"
  echo "RUNNING_PID=${PID:-none}"
  echo "MATCH=$MATCH"
  echo "LKG_SHA=${LKG:-missing}"
  echo "BINARY_VERSION=$VERSION"
  echo "VERSION_MATCH=$VMATCH"
  echo "PREV_BINARY_VERSION=${PREV_VERSION:-none}"
} > "$TMP" 2>/dev/null && chmod 0644 "$TMP" && mv -f "$TMP" "$OUT"
cat "$OUT"
