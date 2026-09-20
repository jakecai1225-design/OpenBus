# -*- coding: utf-8 -*-
"""Stdlib HTTP bridge for assistant-ui — no fastapi (MSYS2-friendly)."""

from __future__ import annotations

import json
import os
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Optional
from urllib.parse import urlparse

_HERE = Path(__file__).resolve().parent
_PLUGIN = _HERE.parent
_WEBUI_DIST = _PLUGIN / "webui" / "dist"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def _json_bytes(obj) -> bytes:
    return json.dumps(obj, ensure_ascii=False).encode("utf-8")


def _ai_sdk_line(prefix: str, payload) -> bytes:
    body = json.dumps(payload, ensure_ascii=False)
    return ("%s:%s\n" % (prefix, body)).encode("utf-8")


def _make_handler(_bridge: "BridgeServer"):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            return

        def _cors(self):
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header(
                "Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")

        def _read_json(self) -> dict:
            n = int(self.headers.get("Content-Length") or 0)
            if n <= 0:
                return {}
            raw = self.rfile.read(n)
            try:
                data = json.loads(raw.decode("utf-8"))
            except json.JSONDecodeError:
                return {}
            return data if isinstance(data, dict) else {}

        def _send(self, code: int, body: bytes,
                  content_type: str = "application/json"):
            self.send_response(code)
            self._cors()
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_OPTIONS(self):
            self.send_response(204)
            self._cors()
            self.end_headers()

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/api/health":
                from agent.agents_runtime import get_runtime
                return self._send(200, _json_bytes({
                    "ok": True,
                    "session": get_runtime().store.session_id,
                }))
            if path == "/api/settings":
                from agent.agents_runtime import get_runtime
                return self._send(200, _json_bytes(get_runtime().settings()))
            if path == "/api/snapshot":
                try:
                    from _shared import activity_snapshot
                    payload = {
                        "text": activity_snapshot.format_for_prompt(),
                        "data": activity_snapshot.get(),
                    }
                except Exception as e:
                    payload = {"text": "", "error": str(e)}
                return self._send(200, _json_bytes(payload))
            if path == "/api/attachments":
                items = []
                try:
                    from _shared import ai_attach
                    for att in ai_attach.get_inbox().items():
                        items.append({
                            "id": att.id,
                            "kind": att.kind,
                            "title": att.title,
                            "preview": att.preview,
                            "uri": att.uri,
                        })
                except Exception:
                    pass
                return self._send(200, _json_bytes({"items": items}))
            if path == "/api/approve/pending":
                from agent.agents_runtime import get_runtime
                return self._send(200, _json_bytes({
                    "pending": get_runtime().approval.pending(),
                }))
            return self._serve_static(path)

        def do_PUT(self):
            path = urlparse(self.path).path
            if path == "/api/settings":
                from agent.session_store import load_settings, save_settings
                from agent.agents_runtime import get_runtime
                body = self._read_json()
                current = load_settings()
                current.update(body)
                path_saved = save_settings(current)
                get_runtime().reload_settings()
                return self._send(200, _json_bytes({
                    "ok": True,
                    "path": path_saved,
                    "settings": get_runtime().settings(),
                }))
            return self._send(404, _json_bytes({"error": "not found"}))

        def do_DELETE(self):
            path = urlparse(self.path).path
            if path.startswith("/api/attachments/"):
                att_id = path.rsplit("/", 1)[-1]
                ok = False
                try:
                    from _shared import ai_attach
                    ok = ai_attach.get_inbox().remove(att_id)
                except Exception:
                    pass
                return self._send(200, _json_bytes({"ok": ok}))
            return self._send(404, _json_bytes({"error": "not found"}))

        def do_POST(self):
            path = urlparse(self.path).path
            if path == "/api/attachments/clear":
                try:
                    from _shared import ai_attach
                    ai_attach.get_inbox().clear()
                except Exception:
                    pass
                return self._send(200, _json_bytes({"ok": True}))
            if path == "/api/approve":
                from agent.agents_runtime import get_runtime
                body = self._read_json()
                ok = get_runtime().approval.resolve(bool(body.get("approved")))
                return self._send(200, _json_bytes({
                    "ok": ok, "approved": bool(body.get("approved")),
                }))
            if path == "/api/chat":
                return self._chat(self._read_json())
            return self._send(404, _json_bytes({"error": "not found"}))

        def _extract_user_text(self, body: dict) -> str:
            messages = body.get("messages") or []
            user_text = ""
            if isinstance(messages, list) and messages:
                last = messages[-1]
                if isinstance(last, dict):
                    parts = last.get("parts") or []
                    texts = []
                    for p in parts:
                        if isinstance(p, dict) and p.get("type") == "text":
                            texts.append(p.get("text") or "")
                    if texts:
                        user_text = "".join(texts)
                    else:
                        content = last.get("content")
                        if isinstance(content, str):
                            user_text = content
            if not user_text.strip():
                user_text = (
                    body.get("input") or body.get("prompt") or "").strip()
            return user_text

        def _chat(self, body: dict):
            user_text = self._extract_user_text(body)
            if not user_text:
                return self._send(400, _json_bytes({"error": "empty message"}))

            from agent.agents_runtime import get_runtime
            runtime = get_runtime()

            self.send_response(200)
            self._cors()
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("X-Vercel-AI-Data-Stream", "v1")
            self.end_headers()

            try:
                for event in runtime.run_streamed_sync(user_text):
                    et = event.get("type")
                    if et == "text-delta":
                        self.wfile.write(
                            _ai_sdk_line("0", event.get("delta") or ""))
                    elif et == "tool-input-available":
                        self.wfile.write(_ai_sdk_line("9", {
                            "toolCallId": event.get("toolCallId"),
                            "toolName": event.get("toolName"),
                            "args": event.get("input") or {},
                        }))
                    elif et == "tool-output-available":
                        self.wfile.write(_ai_sdk_line("a", {
                            "toolCallId": event.get("toolCallId"),
                            "result": event.get("output"),
                        }))
                    elif et == "error":
                        self.wfile.write(_ai_sdk_line(
                            "3", event.get("errorText") or "error"))
                    elif et == "finish":
                        self.wfile.write(_ai_sdk_line("d", {
                            "finishReason": event.get("finishReason") or "stop",
                        }))
                    self.wfile.flush()
            except Exception as e:
                self.wfile.write(_ai_sdk_line(
                    "3", "%s: %s" % (type(e).__name__, e)))
                self.wfile.write(
                    _ai_sdk_line("d", {"finishReason": "error"}))
                self.wfile.flush()
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()

        def _serve_static(self, path: str):
            if not _WEBUI_DIST.is_dir():
                return self._send(503, _json_bytes({
                    "ok": False,
                    "error": "webui/dist not built",
                    "hint": (
                        "cd plugins/ai-agent/webui && "
                        "npm install && npm run build"),
                }))
            rel = path.lstrip("/") or "index.html"
            candidate = (_WEBUI_DIST / rel).resolve()
            try:
                candidate.relative_to(_WEBUI_DIST.resolve())
            except ValueError:
                return self._send(404, _json_bytes({"error": "not found"}))
            if not candidate.is_file():
                candidate = _WEBUI_DIST / "index.html"
            if not candidate.is_file():
                return self._send(404, _json_bytes({"error": "index missing"}))
            data = candidate.read_bytes()
            ctype = "text/html; charset=utf-8"
            if candidate.suffix == ".js":
                ctype = "application/javascript"
            elif candidate.suffix == ".css":
                ctype = "text/css"
            elif candidate.suffix == ".svg":
                ctype = "image/svg+xml"
            return self._send(200, data, ctype)

    return Handler


class BridgeServer:
    """Background ThreadingHTTPServer on 127.0.0.1."""

    def __init__(self):
        self.port: Optional[int] = None
        self._httpd: Optional[ThreadingHTTPServer] = None
        self._thread: Optional[threading.Thread] = None

    @property
    def base_url(self) -> str:
        if not self.port:
            raise RuntimeError("bridge not started")
        return "http://127.0.0.1:%d" % self.port

    def start(self, port: int | None = None) -> str:
        if self._thread and self._thread.is_alive():
            return self.base_url

        self.port = int(
            port or os.environ.get("OPENBUS_AI_BRIDGE_PORT") or 0
        ) or _find_free_port()
        handler = _make_handler(self)
        self._httpd = ThreadingHTTPServer(("127.0.0.1", self.port), handler)
        self._thread = threading.Thread(
            target=self._httpd.serve_forever,
            name="ai-agent-bridge",
            daemon=True,
        )
        self._thread.start()
        return self.base_url

    def stop(self) -> None:
        if self._httpd is not None:
            try:
                self._httpd.shutdown()
            except Exception:
                pass
            try:
                self._httpd.server_close()
            except Exception:
                pass
        if self._thread is not None:
            self._thread.join(timeout=3.0)
        self._httpd = None
        self._thread = None
        self.port = None


_BRIDGE: Optional[BridgeServer] = None


def get_bridge() -> BridgeServer:
    global _BRIDGE
    if _BRIDGE is None:
        _BRIDGE = BridgeServer()
    return _BRIDGE
