# -*- coding: utf-8 -*-
"""UDS Suite — domain suite for ISO 14229 diagnostics.

Workspaces: Setup | Diagnose | Scan | Batch | Security | Profiles
VS Code workbench: activity bar + tabs + OUTPUT panel.
"""

from __future__ import annotations

import os
import sys
from typing import Optional

import sin
from _shared import state_store

PLUGIN_ID = "uds-suite"

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

_shell = None
_context = None


def _resolve_start_page(context) -> Optional[str]:
    """Optional start page from state goto.json or context attrs."""
    goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
    page = goto.get("start_page")
    if page:
        return str(page)
    for attr in ("start_page", "uds_suite_page", "page"):
        if hasattr(context, attr):
            val = getattr(context, attr)
            if val:
                return str(val)
    return None


def _open_suite(start_page: str | None = None):
    global _shell
    if _shell is None:
        return
    if start_page:
        _shell.goto_page(start_page)
    _shell.show()
    _shell.raise_()
    _shell.activateWindow()



def _reload_live_modules():
    from _shared import suite_chrome
    suite_chrome.reload_live_modules()


def activate(context):
    global _shell, _context
    _context = context

    try:
        _reload_live_modules()
        from app_shell import AppShell
    except ImportError as e:
        msg = "UDS Suite failed to import: %s" % e
        sin.output.append(msg)
        raise RuntimeError(msg) from e

    start = _resolve_start_page(context)
    _shell = AppShell(context, start_page=start)

    def on_open(*_args):
        # Re-check goto / optional page each open
        page = _resolve_start_page(context)
        if page:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        _open_suite(page)

    context.register_command("udsSuite.open", on_open, "UDS Suite")

    _register_ai_caps()
    _shell.show()
    _shell.raise_()
    _shell.activateWindow()
    sin.output.append(
        "UDS Suite ready (Setup / Diagnose / Scan / Batch / Security / Profiles)")


def _register_ai_caps():
    """Read-only Capability Bus entries for the AI Agent (soft-fail)."""
    try:
        from _shared.capability_bus import Capability, register as cap_register
    except Exception:
        return

    def session_info(_args: dict):
        session = getattr(_shell, "session", None) if _shell else None
        if session is None:
            return {"ok": False, "error": "suite not ready"}
        return {
            "ok": True,
            "tx_id": getattr(session, "tx_id", None),
            "rx_id": getattr(session, "rx_id", None),
            "func_id": getattr(session, "func_id", None),
            "session_name": getattr(session, "session_name", "unknown"),
            "tester_present": bool(getattr(session, "tester_present", False)),
            "functional": bool(getattr(session, "functional", False)),
        }

    try:
        cap_register(PLUGIN_ID, [
            Capability(
                id="uds.session.info",
                provider=PLUGIN_ID,
                title="UDS session info",
                description="Read current UDS addressing and diagnostic session name",
                parameters={"type": "object", "properties": {}},
                permission="read",
                handler=session_info,
                tags=["uds", "session"],
            ),
        ])
    except Exception:
        pass


def deactivate():
    global _shell, _context
    try:
        from _shared import capability_bus
        capability_bus.unregister(PLUGIN_ID)
    except Exception:
        pass
    if _shell is not None:
        _shell._sin_suppress_close_notify = True
        try:
            _shell.shutdown()
        except Exception:
            pass
        try:
            _shell.close()
        except Exception:
            pass
        _shell = None
    _context = None
    sin.output.append("UDS Suite deactivated")


def goto_page(page: str):
    """Helper for deprecated stubs: open suite on a workspace."""
    state_store.save_state(PLUGIN_ID, {"start_page": page}, "goto.json")
    try:
        sin.commands.execute("udsSuite.open")
    except Exception:
        _open_suite(page)
