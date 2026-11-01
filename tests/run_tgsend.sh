#!/bin/sh
# Runs the Telegram send-path test against a fake Bot API on a local port.
set -e

PORT="${PORT:-18091}"
BIN="${BIN:-build/test_telegram_send}"
CAP="$(mktemp)"

python3 tests/fake_telegram.py --port "$PORT" --capture "$CAP" &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; rm -f "$CAP"' EXIT INT TERM

i=0
while [ "$i" -lt 40 ]; do
    if python3 -c "import socket,sys; socket.create_connection(('127.0.0.1',$PORT),0.2).close()" 2>/dev/null; then
        break
    fi
    i=$((i + 1))
    sleep 0.05
done

"$BIN" "http://127.0.0.1:$PORT" "$CAP" "12345"
