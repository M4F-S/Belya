#!/usr/bin/env python3
"""tests/test_retry.py - 429 handling: obey Retry-After, but never unboundedly."""
import http.server, json, os, socketserver, subprocess, sys, threading, time

state = {}


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", "0")))
        state["requests"] += 1
        if state["requests"] == 1:
            state["first"] = time.time()
            body = json.dumps({"error": {"message": "rate limited"}}).encode()
            self.send_response(429)
            self.send_header("Retry-After", str(state["asked"]))
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        state["second"] = time.time()
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


def run_case(name, asked, cap, lo, hi):
    binpath = os.environ.get("BELYA_BIN") or os.path.abspath("./build/belya")
    if not os.path.exists(binpath):
        print(f"FAILED: binary not found: {binpath}")
        return 1
    state.clear()
    state.update({"requests": 0, "asked": asked})
    with socketserver.TCPServer(("127.0.0.1", 0), Handler) as srv:
        port = srv.server_address[1]
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        env = dict(os.environ)
        env.update({
            "MODEL_ENDPOINT": f"http://127.0.0.1:{port}/v1/chat/completions",
            "MODEL_API_KEY": "test-key", "MODEL_NAME": "test-model",
            "MODEL_MAX_RETRIES": "3", "BELYA_SANDBOX": "0", "BELYA_STREAM_ECHO": "1",
        })
        if cap:
            env["BELYA_RETRY_AFTER_CAP"] = str(cap)
        proc = subprocess.run([binpath, "--headless", "say hi"],
                              capture_output=True, text=True, env=env, timeout=120)
        srv.shutdown()

    waited = (state["second"] - state["first"]) if state.get("second") else 0.0
    fails = 0
    def ck(n, ok, d=""):
        nonlocal fails
        print(("  [ok] " if ok else "  [FAIL] ") + n + ("" if ok else f" <- {d}"))
        if not ok:
            fails += 1

    ck(f"{name}: retry happened", state["requests"] >= 2, str(state["requests"]))
    ck(f"{name}: waited >= {lo}s", waited >= lo, f"{waited:.2f}s")
    ck(f"{name}: waited < {hi}s", waited < hi, f"{waited:.2f}s")
    ck(f"{name}: clean exit", proc.returncode == 0, str(proc.returncode))
    ck(f"{name}: answer delivered", "waited-out" in (proc.stdout or ""), (proc.stdout or "")[-80:])
    return fails


def main():
    fails = 0
    print("[Test] 429 Retry-After")
    # provider says 2s -> we wait about that, not our own guess
    fails += run_case("honours header", asked=2, cap=None, lo=1.8, hi=7)
    # provider says 9999s -> must be clamped to the cap, not obeyed
    fails += run_case("clamps hostile", asked=9999, cap=2, lo=1.8, hi=7)
    print(f"\n  retry-after: {fails} failures")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
