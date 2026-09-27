# -*- coding: utf-8 -*-
"""Start / stop the Node Mastra (agent-ts) sidecar when available."""

from __future__ import annotations

import os
import shutil
import socket
import subprocess
import time
import urllib.request
from pathlib import Path
from typing import Optional

_PLUGIN = Path(__file__).resolve().parent
_AGENT_TS = _PLUGIN / "agent-ts"


def find_node() -> Optional[str]:
    env = os.environ.get("OPENBUS_NODE") or os.environ.get("NODE_BINARY")
    if env and os.path.isfile(env):
        return env
    found = shutil.which("node")
    if found:
        return found
    candidates = [
        r"D:\tools\nodejs\node.exe",
        r"C:\Program Files\nodejs\node.exe",
        os.path.expandvars(r"%LOCALAPPDATA%\Programs\node\node.exe"),
    ]
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return None


def sidecar_entry() -> Optional[Path]:
    for name in ("dist/server.js", "dist/server.mjs", "src/server.ts", "server.mjs"):
        p = _AGENT_TS / name
        if p.is_file():
            return p
    return None


class NodeSidecar:
    def __init__(self):
        self.port: Optional[int] = None
        self._proc: Optional[subprocess.Popen] = None

    @property
    def base_url(self) -> str:
        if not self.port:
            raise RuntimeError("sidecar not started")
        return "http://127.0.0.1:%d" % self.port

    def available(self) -> bool:
        return find_node() is not None and sidecar_entry() is not None

    def start(self, tool_bridge_url: str, port: int | None = None) -> Optional[str]:
        """Start Node agent. Returns base URL or None if unavailable."""
        if self._proc and self._proc.poll() is None:
            return self.base_url
        node = find_node()
        entry = sidecar_entry()
        if not node or not entry:
            return None

        self.port = int(port or os.environ.get("OPENBUS_AI_AGENT_PORT") or 0)
        if not self.port:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.bind(("127.0.0.1", 0))
                self.port = int(s.getsockname()[1])

        env = os.environ.copy()
        env["OPENBUS_TOOL_BRIDGE"] = tool_bridge_url.rstrip("/")
        env["PORT"] = str(self.port)
        env["HOST"] = "127.0.0.1"

        args = [node]
        if entry.suffix == ".ts":
            # Prefer compiled dist; if only ts, try npx tsx
            tsx = shutil.which("tsx")
            if tsx:
                args = [node, tsx, str(entry)]
            else:
                return None
        else:
            args.append(str(entry))

        try:
            self._proc = subprocess.Popen(
                args,
                cwd=str(_AGENT_TS),
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except OSError:
            self._proc = None
            self.port = None
            return None

        # Wait for health
        deadline = time.time() + 8.0
        while time.time() < deadline:
            if self._proc.poll() is not None:
                self.port = None
                return None
            try:
                with urllib.request.urlopen(
                    self.base_url + "/api/health", timeout=0.5
                ) as resp:
                    if resp.status == 200:
                        return self.base_url
            except Exception:
                time.sleep(0.2)
        return self.base_url

    def stop(self) -> None:
        if self._proc is not None and self._proc.poll() is None:
            try:
                self._proc.terminate()
                self._proc.wait(timeout=3)
            except Exception:
                try:
                    self._proc.kill()
                except Exception:
                    pass
        self._proc = None
        self.port = None


_SIDECAR: Optional[NodeSidecar] = None


def get_sidecar() -> NodeSidecar:
    global _SIDECAR
    if _SIDECAR is None:
        _SIDECAR = NodeSidecar()
    return _SIDECAR
