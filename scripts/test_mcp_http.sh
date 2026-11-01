#!/usr/bin/env bash
# Live interop test: Belya HTTP MCP client <-> spec-enforcing mock server.
set -euo pipefail

PORT="${1:-8791}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/test_mcp_http"

if [[ ! -x "$BIN" ]]; then
  echo "build/test_mcp_http missing; run: make test" >&2
  exit 1
fi

python3 "$ROOT/tests/mcp_mock_server.py" "$PORT" &
MOCK_PID=$!
trap 'kill "$MOCK_PID" 2>/dev/null || true' EXIT

for _ in $(seq 1 50); do
  if curl -fsS "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then break; fi
  sleep 0.1
done

"$BIN" --url "http://127.0.0.1:$PORT/mcp"

# Optional: an official-SDK server (stateful 2025-11-25) to exercise negotiation.
if [[ -n "${MCP_SDK_URL:-}" ]]; then
  echo "[mcp] official SDK endpoint: $MCP_SDK_URL"
  "$BIN" --url "$MCP_SDK_URL"
fi

# Negative paths: 401 and 403 must be reported honestly.
set +e
"$BIN" --url "http://127.0.0.1:$PORT/mcp/auth-test" >/tmp/belya_mcp_auth.log 2>&1
AUTH_RC=$?
"$BIN" --url "http://127.0.0.1:$PORT/mcp/forbidden-test" >/tmp/belya_mcp_forbidden.log 2>&1
FORBID_RC=$?
set -e

if [[ $AUTH_RC -eq 0 ]]; then
  echo "[FAIL] auth-test unexpectedly succeeded" >&2; exit 1
fi
if [[ $FORBID_RC -eq 0 ]]; then
  echo "[FAIL] forbidden-test unexpectedly succeeded" >&2; exit 1
fi
grep -q "Unauthorized" /tmp/belya_mcp_auth.log || { echo "[FAIL] 401 not reported" >&2; exit 1; }
grep -q "Forbidden" /tmp/belya_mcp_forbidden.log || { echo "[FAIL] 403 not reported" >&2; exit 1; }

echo "[PASS] live MCP HTTP interop incl. 401/403 mapping"
