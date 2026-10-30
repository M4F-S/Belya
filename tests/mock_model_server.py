#!/usr/bin/env python3
"""Scripted OpenAI-compatible model server for agent-loop tests.

Serves /v1/chat/completions from a JSON script, one entry per request:
    {"status": 200, "body": {...}}          # normal reply
    {"status": 429, "body": {"error": ...}} # rate limited
If the script is exhausted, the last entry repeats.

Usage: mock_model_server.py --port 18081 --script tests/loop_script.json
"""
import argparse
import json
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


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):  # noqa: A002 - stdlib signature
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        entry = next_entry()
        payload = json.dumps(entry["body"]).encode()
        self.send_response(entry.get("status", 200))
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


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
