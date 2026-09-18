# -*- coding: utf-8 -*-
"""Protocol Hub — NM | ISO-TP | ISOBUS | NMEA2000 | GBT27930 | XCP | Log."""

from __future__ import annotations

import os
import sys
from typing import Optional

import sin
from _shared import state_store

PLUGIN_ID = "protocol-hub"

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
    for attr in ("start_page", "protocol_hub_page", "page"):
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
        msg = "Protocol Hub failed to import: %s" % e
        sin.output.append(msg)
        raise RuntimeError(msg) from e

    start = _resolve_start_page(context)
    _shell = AppShell(context, start_page=start)

    def on_open(*_args):
        page = _resolve_start_page(context)
        if page:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        _open_suite(page)

    context.register_command("protocolHub.open", on_open, "Protocol Hub")

    _shell.show()
    sin.output.append(
        "Protocol Hub ready (NM / ISO-TP / ISOBUS / NMEA2000 / GBT27930 / XCP / Log)")


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
    sin.output.append("Protocol Hub deactivated")


def goto_page(page: str):
    """Helper for deprecated stubs: open suite on a workspace."""
    state_store.save_state(PLUGIN_ID, {"start_page": page}, "goto.json")
    try:
        sin.commands.execute("protocolHub.open")
    except Exception:
        _open_suite(page)
