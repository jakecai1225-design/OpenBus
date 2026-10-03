# -*- coding: utf-8 -*-
"""J1939 Suite — Analyzer | Log."""

from __future__ import annotations

import os
import sys
from typing import Optional

import sin
from _shared import state_store

PLUGIN_ID = "j1939-suite"

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
    for attr in ("start_page", "j1939_suite_page", "page"):
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
        msg = "J1939 Suite failed to import: %s" % e
        sin.output.append(msg)
        raise RuntimeError(msg) from e

    start = _resolve_start_page(context)
    _shell = AppShell(context, start_page=start)

    def on_open(*_args):
        page = _resolve_start_page(context)
        if page:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        _open_suite(page)

    context.register_command("j1939Suite.open", on_open, "J1939 Suite")

    _shell.show()
    sin.output.append("J1939 Suite ready (Live / Transport / Diagnostics / Network)")


def deactivate():
    global _shell, _context
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
    sin.output.append("J1939 Suite deactivated")


def goto_page(page: str):
    state_store.save_state(PLUGIN_ID, {"start_page": page}, "goto.json")
    try:
        sin.commands.execute("j1939Suite.open")
    except Exception:
        _open_suite(page)
