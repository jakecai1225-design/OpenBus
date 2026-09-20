# -*- coding: utf-8 -*-
"""AUTOSAR Suite — COM, system extract, CanNm, E2E, SecOC."""

from __future__ import annotations

import os
import sys
from typing import Optional

import sin
from _shared import state_store

PLUGIN_ID = "autosar-suite"

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
    for attr in ("start_page", "autosar_suite_page", "page"):
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
    except ImportError as exc:
        msg = "AUTOSAR Suite failed to import: %s" % exc
        sin.output.append(msg)
        raise RuntimeError(msg) from exc

    start = _resolve_start_page(context)
    _shell = AppShell(context, start_page=start)

    def on_open(*_args):
        page = _resolve_start_page(context)
        if page:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        _open_suite(page)

    context.register_command("autosarSuite.open", on_open, "AUTOSAR Suite")
    _shell.show()
    _shell.raise_()
    _shell.activateWindow()
    sin.output.append("AUTOSAR Suite ready (COM / System / NM / E2E / SecOC)")


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
    sin.output.append("AUTOSAR Suite deactivated")


def goto_page(page: str):
    state_store.save_state(PLUGIN_ID, {"start_page": page}, "goto.json")
    try:
        sin.commands.execute("autosarSuite.open")
    except Exception:
        _open_suite(page)
