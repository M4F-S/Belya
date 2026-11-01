#!/usr/bin/env python3
"""Self-check for tests/mock_model_server.py.

The mock is what every loop/CLI test talks to, so a bug in it surfaces as a
confusing product failure. This pins both wire formats and the script order of
tests/loop_script.json:

  1 text reply  2 tool call  (replayed as SSE)  3 HTTP 429  4 text  5 repeat

Run: make test-mock
"""
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

from mock_model_server import free_port

HERE = __file__.rsplit("/", 1)[0]
FAILS = []


def check(name, ok, detail=""):
    print(("  [ok] " if ok else "  [FAIL] ") + name + ("" if ok else f"  <- {detail}"))
    if not ok:
        FAILS.append(name)


def post(port, stream):
    """Returns (status, body). HTTP errors are part of the contract here."""
    body = json.dumps({"stream": stream, "messages": [{"role": "user", "content": "x"}]}).encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/v1/chat/completions",
        data=body,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def main():
    port = free_port()
    proc = subprocess.Popen(
        [sys.executable, f"{HERE}/mock_model_server.py", "--port", str(port),
         "--script", f"{HERE}/loop_script.json"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        for _ in range(40):
            try:
                socket.create_connection(("127.0.0.1", port), 0.2).close()
                break
            except OSError:
                time.sleep(0.05)

        status, raw = post(port, False)
        msg = json.loads(raw)["choices"][0]["message"]
        check("1 json: text reply decoded", msg.get("content") == "hello from mock", str(msg)[:80])

        status, sse = post(port, True)
        frames = [l[6:] for l in sse.splitlines() if l.startswith("data: ") and "[DONE]" not in l]
        check("2 sse: emits frames", len(frames) >= 2, sse[:120])
        check("2 sse: terminates with [DONE]", sse.rstrip().endswith("data: [DONE]"), sse[-40:])
        check("2 sse: frames parse as json", all(json.loads(f) for f in frames), "unparsable frame")
        check("2 sse: streams tool_calls", any('"tool_calls"' in f for f in frames), sse[:120])
        check("2 sse: keeps arguments", "echo hi" in sse, sse[:120])

        status, raw = post(port, False)
        check("3 429 is surfaced as HTTP 429", status == 429, f"status={status}")

        status, raw = post(port, False)
        check("4 recovery reply served", json.loads(raw)["choices"][0]["message"]["content"] == "after retry")
        status, raw = post(port, False)
        check("5 exhausted script repeats the last entry",
              json.loads(raw)["choices"][0]["message"]["content"] == "after retry")
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    print("\nmock server self-check: %s (%d failure%s)"
          % ("PASSED" if not FAILS else "FAILED", len(FAILS), "" if len(FAILS) == 1 else "s"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
