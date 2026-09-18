# -*- coding: utf-8 -*-
"""CANopen Suite — domain suite for CiA 301/402 network, OD, EDS, SDO.

Workspaces: Network | Monitor | Object Dictionary | Profiles | EDS Editor | Log
Shared Node-ID / EDS / activity log; bus TX/RX via sin.frames.
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
    _shell.show()
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

    _shell.show()
    sin.output.append(
        "CANopen Suite ready (Network / Monitor / OD / Profiles / EDS / Log)")


def deactivate():
    global _shell, _context
    if _shell is not None:
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
