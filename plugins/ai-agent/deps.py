# -*- coding: utf-8 -*-
"""Runtime dependency checks for the AI workbench. Fail loudly — never silent."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List


AGENTS_HINT = (
    "Optional: OpenAI Agents SDK (if wheels exist for your Python):\n"
    "  /ucrt64/bin/python -m pip install --break-system-packages openai-agents\n"
    "Without it, the local Orchestrator tool-loop is used."
)

BRIDGE_HINT = (
    "Bridge uses the Python standard library (no fastapi required).\n"
    "Rebuild the UI with MSYS2 node if needed:\n"
    "  cd plugins/ai-agent/webui && npm install && npm run build"
)

WEBVIEW2_HINT = (
    "Install Microsoft Edge / WebView2 Evergreen Runtime for in-window chat.\n"
    "Optional: pip install pywebview  (true WebView2 via edgechromium).\n"
    "Without them, use Open in browser (path A).\n"
    "  https://developer.microsoft.com/microsoft-edge/webview2/"
)

NODE_HINT = (
    "Optional Node.js >= 20 for Mastra agent-ts sidecar:\n"
    "  cd plugins/ai-agent/agent-ts && npm install && npm run build\n"
    "Without Node, Python Orchestrator remains the agent runtime."
)


@dataclass
class DepStatus:
    ok: bool
    missing: List[str] = field(default_factory=list)
    hints: List[str] = field(default_factory=list)
    detail: str = ""

    def message(self) -> str:
        parts = []
        if self.detail:
            parts.append(self.detail)
        if self.missing:
            parts.append("Missing: " + ", ".join(self.missing))
        for h in self.hints:
            parts.append(h)
        return "\n".join(parts) if parts else "OK"


def check_webview2() -> DepStatus:
    try:
        from webview2_host import webview2_available, find_msedge
        if webview2_available():
            return DepStatus(
                ok=True,
                detail="Edge found: %s" % (find_msedge() or ""),
            )
    except Exception as e:
        return DepStatus(
            ok=False,
            missing=["msedge"],
            hints=[WEBVIEW2_HINT],
            detail="WebView2 probe failed: %s" % e,
        )
    return DepStatus(
        ok=False,
        missing=["msedge"],
        hints=[WEBVIEW2_HINT],
        detail="Microsoft Edge not found",
    )


def check_agents_sdk() -> DepStatus:
    try:
        from agents import Agent, Runner  # noqa: F401
        return DepStatus(ok=True, detail="openai-agents available")
    except ImportError as e:
        return DepStatus(
            ok=False,
            missing=["openai-agents"],
            hints=[AGENTS_HINT],
            detail="openai-agents not installed (%s); using Orchestrator fallback"
            % e,
        )


def check_bridge() -> DepStatus:
    # Stdlib HTTP server — always available
    dist_ok = False
    try:
        from pathlib import Path
        dist = Path(__file__).resolve().parent / "webui" / "dist" / "index.html"
        dist_ok = dist.is_file()
    except Exception:
        dist_ok = False
    if not dist_ok:
        return DepStatus(
            ok=False,
            missing=["webui/dist"],
            hints=[BRIDGE_HINT],
            detail="assistant-ui dist not built",
        )
    return DepStatus(ok=True, detail="Stdlib bridge + webui/dist OK")


def check_all(*, require_agents: bool = False,
              require_webview2: bool = False) -> DepStatus:
    checks = [check_bridge()]
    if require_agents:
        checks.append(check_agents_sdk())
    if require_webview2:
        checks.append(check_webview2())
    missing = []
    hints = []
    details = []
    for c in checks:
        if not c.ok:
            missing.extend(c.missing)
            hints.extend(c.hints)
            if c.detail:
                details.append(c.detail)
    ok = all(c.ok for c in checks)
    seen = set()
    uniq_hints = []
    for h in hints:
        if h not in seen:
            seen.add(h)
            uniq_hints.append(h)
    return DepStatus(
        ok=ok,
        missing=missing,
        hints=uniq_hints,
        detail="; ".join(details) if details else (
            "OK" if ok else "Dependencies incomplete"),
    )
