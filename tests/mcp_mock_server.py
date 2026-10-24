#!/usr/bin/env python3
"""Minimal, spec-correct MCP 2026-07-28 mock server for Belya HTTP transport tests.

Rules enforced (see modelcontextprotocol.io/specification/2026-07-28):
  - MCP-Protocol-Version header must be present and match _meta.protocolVersion
  - Mcp-Method header must match the JSON-RPC method
  - Mcp-Name header is required for tools/call
  - _meta must be inside params, never at the JSON-RPC root
Responses use text/event-stream, as a real Streamable HTTP server may.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from socketserver import ThreadingMixIn

PROTO = "2026-07-28"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def _json_error(self, status, code, message):
        body = json.dumps({"jsonrpc": "2.0", "id": None,
                           "error": {"code": code, "message": message}}).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def _sse(self, payload):
        data = json.dumps(payload)
        body = f"event: message\ndata: {data}\n\n".encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/health":
            body = b"ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path == "/mcp/auth-test":
            if not self.headers.get("Authorization"):
                self._json_error(401, -32001, "Unauthorized: token required")
                return
        if self.path == "/mcp/forbidden-test":
            self._json_error(403, -32003, "Forbidden: policy denied")
            return
        if not self.path.startswith("/mcp"):
            self.send_error(404)
            return

        if self.headers.get("MCP-Protocol-Version") != PROTO:
            self._json_error(400, -32020, "HeaderMismatch: MCP-Protocol-Version")
            return

        method_hdr = self.headers.get("Mcp-Method")
        if not method_hdr:
            self._json_error(400, -32020, "HeaderMismatch: missing Mcp-Method")
            return

        try:
            n = int(self.headers.get("Content-Length", 0))
            payload = json.loads(self.rfile.read(n).decode("utf-8"))
        except Exception as exc:
            self._json_error(400, -32020, f"HeaderMismatch: bad JSON: {exc}")
            return

        if "_meta" in payload:
            self._json_error(400, -32020, "HeaderMismatch: _meta at root")
            return
        if payload.get("method") != method_hdr:
            self._json_error(400, -32020, "HeaderMismatch: Mcp-Method != body method")
            return

        params = payload.get("params") or {}
        meta = params.get("_meta") or {}
        if meta.get("io.modelcontextprotocol/protocolVersion") != PROTO:
            self._json_error(400, -32020, "HeaderMismatch: missing protocolVersion in params._meta")
            return
        if "io.modelcontextprotocol/clientCapabilities" not in meta:
            self._json_error(400, -32020, "HeaderMismatch: missing clientCapabilities")
            return

        rid = payload.get("id", 1)

        if method_hdr == "tools/list":
            self._sse({"jsonrpc": "2.0", "id": rid, "result": {"tools": [
                {"name": "echo", "description": "Echo text back",
                 "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}}},
                {"name": "add", "description": "Add two numbers",
                 "inputSchema": {"type": "object", "properties": {"a": {"type": "number"}, "b": {"type": "number"}}}},
            ]}})
            return

        if method_hdr == "tools/call":
            name = params.get("name")
            if not self.headers.get("Mcp-Name") or self.headers.get("Mcp-Name") != name:
                self._json_error(400, -32020, "HeaderMismatch: Mcp-Name")
                return
            args = params.get("arguments") or {}
            if name == "echo":
                text = str(args.get("text", ""))
            elif name == "add":
                text = str(float(args.get("a", 0)) + float(args.get("b", 0)))
            else:
                self._json_error(404, -32601, f"Method not found: {name}")
                return
            self._sse({"jsonrpc": "2.0", "id": rid,
                       "result": {"content": [{"type": "text", "text": text}]}})
            return

        self._json_error(404, -32601, f"Method not found: {method_hdr}")


class Server(ThreadingMixIn, HTTPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8791
    srv = Server(("127.0.0.1", port), Handler)
    print(f"[mcp_mock] http://127.0.0.1:{port}/mcp", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
