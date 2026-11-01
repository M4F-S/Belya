#!/usr/bin/env python3
"""tests/test_retry.py - a 429 with Retry-After must be waited out, not guessed.

Serves 429 + `Retry-After: 2` once, then a normal SSE completion. Runs the real
binary in headless mode and asserts it (a) still answers and (b) actually waited
the ~2 seconds the server asked for rather than its own 4s floor or nothing.
"""
import http.server, json, os, socketserver, subprocess, sys, threading, time

ASKED = 2
state = {"requests": 0, "first_seen": None, "second_seen": None}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        self.rfile.read(n)
        state["requests"] += 1
        idx = state["requests"]

        if idx == 1:
            state["first_seen"] = time.time()
            self.send_response(429)
            self.send_header("Retry-After", str(ASKED))
            self.send_header("Content-Type", "application/json")
            body = json.dumps({"error": {"message": "rate limited"}}).encode()
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        state["second_seen"] = time.time()
        # Same frame shape the tool probe uses: the client always streams and
        # needs the finish frame with usage, plus a Content-Length.
        frames = [{"choices": [{"delta": {"content": "waited-out"}}]},
                  {"choices": [{"delta": {}, "finish_reason": "stop"}],
                   "usage": {"prompt_tokens": 5, "completion_tokens": 2}}]
        data = "".join("data: " + json.dumps(f) + "\n\n" for f in frames) + "data: [DONE]\n\n"
        out = data.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)


def main():
    belya = os.environ.get("BELYA_BIN") or os.path.abspath("./build/belya")
    if not os.path.exists(belya):
        print(f"FAILED: binary not found: {belya}")
        return 1

    with socketserver.TCPServer(("127.0.0.1", 0), Handler) as srv:
        port = srv.server_address[1]
        threading.Thread(target=srv.serve_forever, daemon=True).start()

        env = dict(os.environ)
        env.update({
            "MODEL_ENDPOINT": f"http://127.0.0.1:{port}/v1/chat/completions",
            "MODEL_API_KEY": "test-key",
            "MODEL_NAME": "test-model",
            "MODEL_MAX_RETRIES": "3",
            "BELYA_SANDBOX": "0",
            # headless suppresses the stream echo unless asked (non-TTY)
            "BELYA_STREAM_ECHO": "1",
        })
        t0 = time.time()
        proc = subprocess.run([belya, "--headless", "say hi"],
                              capture_output=True, text=True, env=env, timeout=120)
        elapsed = time.time() - t0
        srv.shutdown()

    fails = 0
    def ck(name, ok, detail=None):
        nonlocal fails
        print(("  [ok] " if ok else "  [FAIL] ") + name + ("" if ok else f" <- {detail}"))
        if not ok:
            fails += 1

    waited = (state["second_seen"] - state["first_seen"]) if state["second_seen"] else 0
    ck("server saw the retry", state["requests"] >= 2, str(state["requests"]))
    ck("retry happened after the requested delay", waited >= ASKED * 0.9, f"{waited:.2f}s")
    ck("retry did not overshoot wildly", waited < ASKED + 5, f"{waited:.2f}s")
    ck("binary exited cleanly", proc.returncode == 0, str(proc.returncode))
    ck("answer was delivered", "waited-out" in (proc.stdout or ""), (proc.stdout or "")[-120:])

    print(f"\n  retry-after: {fails} failures (waited {waited:.2f}s, exit {proc.returncode})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
