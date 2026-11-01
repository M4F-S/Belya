#!/usr/bin/env python3
"""Scripted OpenAI-compatible model server for agent-loop and CLI tests.

Serves /v1/chat/completions from a JSON script, one entry per request:
    {"status": 200, "body": {...chat.completion...}}
    {"status": 429, "body": {"error": {...}}}

The same entry is replayed as SSE deltas when the request sets "stream": true.
The CLI streams by default, so a JSON-only mock makes it report "Empty response
from stream" and test nothing. The last entry repeats once the script runs out.

Usage: mock_model_server.py --port 18081 --script tests/loop_script.json
"""
import argparse
import json
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SCRIPT = []
INDEX = 0
LOCK = threading.Lock()


def next_entry():
    global INDEX
    with LOCK:
        if not SCRIPT:
            return {"status": 500, "body": {"error": {"message": "no script"}}}
        entry = SCRIPT[min(INDEX, len(SCRIPT) - 1)]
        INDEX += 1
        return entry


def sse_for(body):
    """Render a chat.completion object as OpenAI streaming delta frames."""
    choice = (body.get("choices") or [{}])[0]
    msg = choice.get("message", {})
    frames = []
    if msg.get("content"):
        frames.append({"choices": [{"delta": {"content": msg["content"]}}]})
    for i, tc in enumerate(msg.get("tool_calls") or []):
        frames.append({"choices": [{"delta": {"tool_calls": [{
            "index": i, "id": tc.get("id"), "type": "function",
            "function": tc.get("function", {}),
        }]}}]})
    frames.append({"choices": [{"delta": {},
                                "finish_reason": choice.get("finish_reason", "stop")}],
                   "usage": body.get("usage", {})})
    return "".join("data: " + json.dumps(f) + "\n\n" for f in frames) + "data: [DONE]\n\n"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):  # noqa: A002 - stdlib signature
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)
        try:
            req = json.loads(raw or b"{}")
        except ValueError:
            req = {}
        entry = next_entry()
        stream = bool(req.get("stream"))
        payload = (sse_for(entry["body"]) if stream else json.dumps(entry["body"])).encode()
        self.send_response(entry.get("status", 200))
        self.send_header("Content-Type", "text/event-stream" if stream else "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=18081)
    ap.add_argument("--script", default="tests/loop_script.json")
    args = ap.parse_args()
    global SCRIPT
    with open(args.script, encoding="utf-8") as fh:
        SCRIPT = json.load(fh)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
