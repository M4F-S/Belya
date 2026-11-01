#!/usr/bin/env python3
"""Minimal fake Telegram Bot API for adapter tests.

Records every request body (JSON, one per line) to a capture file and replies
{"ok": true, "result": {...}} so the adapter's success paths run for real.

Usage: fake_telegram.py --port N --capture FILE
"""
import argparse
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CAPTURE: str = ""
LOCK = threading.Lock()
MSG_ID = [100]


def record(method, body):
    with LOCK:
        with open(CAPTURE, "a", encoding="utf-8") as fh:
            fh.write(json.dumps({"method": method, "body": body}) + "\n")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):  # noqa: A002
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)
        try:
            body = json.loads(raw or b"{}")
        except ValueError:
            body = {"_raw": raw.decode("utf-8", "replace")}

        method = self.path.rsplit("/", 1)[-1]
        record(method, body)

        with LOCK:
            MSG_ID[0] += 1
            msg_id = MSG_ID[0]

        if method == "sendMessage":
            result = {"message_id": msg_id, "text": body.get("text", "")}
        elif method == "editMessageText":
            result = {"message_id": body.get("message_id", 0), "text": body.get("text", "")}
        else:
            result = True

        payload = json.dumps({"ok": True, "result": result}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--capture", required=True)
    args = ap.parse_args()
    global CAPTURE
    CAPTURE = args.capture
    open(CAPTURE, "w", encoding="utf-8").close()
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
