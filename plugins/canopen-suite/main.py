# -*- coding: utf-8 -*-
"""CANopen Suite — domain suite for CiA 301/402 network, OD, EDS, SDO.

Workbench: activity bar + Side Bar + closable editor tabs + OUTPUT + status.
"""

from __future__ import annotations

import os
import sys
from typing import Optional

import sin
from _shared import state_store

PLUGIN_ID = "canopen-suite"

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

_shell = None
_context = None


def _resolve_start_page(context) -> Optional[str]:
    goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
    page = goto.get("start_page")
    if page:
        return str(page)
    for attr in ("start_page", "canopen_suite_page", "page"):
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
    _shell.showMaximized()
    _shell.raise_()
    _shell.activateWindow()


def activate(context):
    global _shell, _context
    _context = context

    try:
        from app_shell import AppShell
    except ImportError as e:
        msg = "CANopen Suite failed to import: %s" % e
        sin.output.append(msg)
        raise RuntimeError(msg) from e

    start = _resolve_start_page(context)
    _shell = AppShell(context, start_page=start)

    def on_open(*_args):
        page = _resolve_start_page(context)
        if page:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        _open_suite(page)

    context.register_command("canopenSuite.open", on_open, "CANopen Suite")

    _register_ai_caps()
    _shell.showMaximized()
    sin.output.append(
        "CANopen Suite ready (Project · EDS · Device · Network)")


def _register_ai_caps():
    """Read-only Capability Bus entries for the AI Agent (soft-fail)."""
    try:
        from _shared.capability_bus import Capability, register as cap_register
    except Exception:
        return

    def od_get(args: dict):
        session = getattr(_shell, "session", None) if _shell else None
        if session is None:
            return {"ok": False, "error": "suite not ready"}
        index = int(args.get("index") or 0)
        sub = int(args.get("subindex") or 0)
        for e in session.od_entries:
            if e.index == index and e.subindex == sub:
                return {
                    "ok": True,
                    "index": index,
                    "subindex": sub,
                    "name": e.name,
                    "data_type": getattr(e, "data_type", ""),
                    "access": getattr(e, "access_type", ""),
                    "default": getattr(e, "default_value", ""),
                }
        return {"ok": False, "error": "object not found", "index": index, "subindex": sub}

    def eds_summary(_args: dict):
        session = getattr(_shell, "session", None) if _shell else None
        if session is None:
            return {"ok": False, "error": "suite not ready"}
        entries = session.draft_entries or session.od_entries
        return {
            "ok": True,
            "path": session.eds_path or "",
            "object_count": len(entries),
            "node_id": session.node_id,
            "file_info": dict(session.eds_file_info or {}),
            "device_info": dict(session.eds_device_info or {}),
        }

    try:
        cap_register(PLUGIN_ID, [
            Capability(
                id="canopen.od.get",
                provider=PLUGIN_ID,
                title="CANopen OD get",
                description="Look up one object in the applied OD by index/subindex",
                parameters={
                    "type": "object",
                    "properties": {
                        "index": {"type": "integer"},
                        "subindex": {"type": "integer"},
                    },
                    "required": ["index"],
                },
                permission="read",
                handler=od_get,
                tags=["canopen", "od"],
            ),
            Capability(
                id="canopen.eds.summary",
                provider=PLUGIN_ID,
                title="CANopen EDS summary",
                description="Summarize the loaded EDS / draft object dictionary",
                parameters={"type": "object", "properties": {}},
                permission="read",
                handler=eds_summary,
                tags=["canopen", "eds"],
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
    sin.output.append("CANopen Suite deactivated")


def goto_page(page: str):
    """Helper for deprecated stubs: open suite on a workspace."""
    state_store.save_state(PLUGIN_ID, {"start_page": page}, "goto.json")
    try:
        sin.commands.execute("canopenSuite.open")
    except Exception:
        _open_suite(page)
