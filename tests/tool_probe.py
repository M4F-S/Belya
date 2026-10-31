#!/usr/bin/env python3
"""Exercise every registered Belya tool against the DEPLOYED binary.

For each tool: script a mock model that asks for that one tool call, run
/opt/belya/belya in CLI mode, then read the tool observation out of the second
model request (the one that carries the conversation back to the model).

Writes /tmp/tool_report.json and prints a compact summary.
"""
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BIN = os.path.abspath(os.environ.get("BELYA_BIN", "./build/belya"))
WS = "/tmp/probe_ws"
RESULTS = []

ARGS = {
    "bash": {"command": "echo probe_ok"},
    "read_file": {"path": "sample.txt"},
    "write_file": {"path": "probe_out.txt", "content": "written by probe"},
    "edit_file": {"path": "sample.txt", "old_text": "alpha", "new_text": "beta"},
    "apply_patch": {"patch": "--- a/sample.txt\n+++ b/sample.txt\n-alpha line one\n+gamma line one\n"},
    "list_dir": {"path": "."},
    "search_files": {"pattern": "alpha"},
    "git_status": {},
    "git_diff": {},
    "save_memory": {"topic": "probe_topic", "content": "probe memory entry"},
    "recall_memory": {"query": "probe"},
    "fetch_url": {"url": "http://127.0.0.1:9/"},
    "recall_skill": {"query": "probe"},
    "recall_conversation": {"query": "probe"},
    "task_create": {"title": "probe task", "description": "probe"},
    "task_list": {},
    "cron_list": {},
    "enter_plan_mode": {},
    "exit_plan_mode": {},
    "ask_user": {"question": "probe?"},
    "define_tool": {"name": "probe_tool", "description": "noop", "script_body": "echo probe"},
    "web_search": {"query": "probe"},
    "web_fetch": {"url": "http://127.0.0.1:9/"},
}

CAPTURED = []
LOCK = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n)
        try:
            body = json.loads(raw)
        except ValueError:
            body = {}
        with LOCK:
            CAPTURED.append(body)
        # Belya always streams: emit proper OpenAI delta frames.
        if len(CAPTURED) % 2 == 1:
            args = json.dumps(ARGS.get(CURRENT_TOOL, {}))
            frames = [{"choices": [{"delta": {"tool_calls": [{"index": 0, "id": "call_1",
                       "type": "function", "function": {"name": CURRENT_TOOL, "arguments": args}}]}}]},
                      {"choices": [{"delta": {}, "finish_reason": "tool_calls"}],
                       "usage": {"prompt_tokens": 5, "completion_tokens": 5}}]
        else:
            frames = [{"choices": [{"delta": {"content": "done"}}]},
                      {"choices": [{"delta": {}, "finish_reason": "stop"}],
                       "usage": {"prompt_tokens": 5, "completion_tokens": 2}}]
        data = "".join("data: " + json.dumps(f) + "\n\n" for f in frames) + "data: [DONE]\n\n"
        out = data.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)


def start_server():
    srv = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, srv.server_address[1]


def tool_observation():
    """Last role=tool message across all captured requests."""
    obs = None
    for body in CAPTURED:
        for m in body.get("messages", []):
            if m.get("role") == "tool":
                c = m.get("content")
                if isinstance(c, list):
                    c = " ".join(x.get("text", "") for x in c if isinstance(x, dict))
                obs = c
    return obs


def classify(tool, obs, stdout, rc):
    if rc != 0 and obs is None:
        return "CRASH", f"exit={rc}"
    if obs is None:
        return "NO_OBSERVATION", "tool never returned a result to the model"
    s = str(obs).strip()
    if not s:
        return "EMPTY", ""
    low = s.lower()
    if s == "Success":
        return "SUSPECT_PLACEHOLDER", "bare 'Success' - known NULL->Success pattern"
    if low.startswith("error") or low.startswith("unknown tool") or "not permitted" in low:
        return "ERROR", s[:150]
    if "missing" in low and "argument" in low:
        return "NEEDS_ARGS", s[:150]
    return "OK", s[:150].replace("\n", " | ")


def main():
    global CURRENT_TOOL
    shutil.rmtree(WS, ignore_errors=True)
    os.makedirs(WS, exist_ok=True)
    with open(os.path.join(WS, "sample.txt"), "w") as fh:
        fh.write("alpha line one\nalpha line two\n")
    os.makedirs(os.path.join(WS, "subdir"), exist_ok=True)
    shutil.copy(BIN, os.path.join(WS, "belya")) if False else None

    tools = sys.argv[2:]
    srv, port = start_server()
    env = dict(os.environ)
    env.update({
        "MODEL_ENDPOINT": f"http://127.0.0.1:{port}/v1/chat/completions",
        "MODEL_API_KEY": "probe",
        "MODEL_NAME": "mock",
        "MODEL_TIMEOUT": "10",
        "BELYA_PERMISSION_MODE": "permissive",
        "BELYA_STREAM_ECHO": "0",
        "GATEWAY_PREFLIGHT_PROBE": "0",
    })

    for tool in tools:
        CURRENT_TOOL = tool
        CAPTURED.clear()
        t0 = time.time()
        try:
            proc = subprocess.run([BIN, "--no-probe", "--headless", "probe request"], cwd=WS, env=env,
                                  capture_output=True, text=True, timeout=25)
            rc, out = proc.returncode, proc.stdout + proc.stderr
        except subprocess.TimeoutExpired as e:
            rc, out = -9, (e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, bytes) else ""
        if os.environ.get("SHOW_OUT"):
            print('    STDOUT>>>', out[-600:].replace(chr(10), ' | '))
        obs = tool_observation()
        reqs = len(CAPTURED)
        status, detail = classify(tool, obs, out, rc)
        RESULTS.append({"tool": tool, "status": status, "detail": detail,
                        "ms": int((time.time() - t0) * 1000), "obs_len": len(str(obs or "")), "requests": reqs})
        print(f"  {status:<17} {tool:<20} reqs={reqs} {RESULTS[-1]['ms']:>5}ms  {detail[:70]}")

    srv.shutdown()
    with open("/tmp/tool_report.json", "w") as fh:
        json.dump(RESULTS, fh, indent=1)

    from collections import Counter
    c = Counter(r["status"] for r in RESULTS)
    print("\nSUMMARY:", ", ".join(f"{k}={v}" for k, v in sorted(c.items())))
    print("total:", len(RESULTS))


if __name__ == "__main__":
    CURRENT_TOOL = ""
    main()
