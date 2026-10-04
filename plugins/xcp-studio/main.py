# -*- coding: utf-8 -*-
"""XCP Studio — online XCP measurement & calibration."""

from __future__ import annotations

import os
import sys
from typing import Optional

import sin
from _shared import state_store

PLUGIN_ID = "xcp-studio"
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
    for attr in ("start_page", "xcp_studio_page", "page"):
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
        msg = "XCP Studio failed to import: %s" % e
        sin.output.append(msg)
        raise RuntimeError(msg) from e

    start = _resolve_start_page(context)
    _shell = AppShell(context, start_page=start)

    def on_open(*_args):
        page = _resolve_start_page(context)
        if page:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        _open_suite(page)

    context.register_command("xcpStudio.open", on_open, "XCP Studio")
    _shell.showMaximized()
    sin.output.append("XCP Studio ready (Setup / Measure / Calibrate / Record)")


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
    sin.output.append("XCP Studio deactivated")


def goto_page(page: str):
    state_store.save_state(PLUGIN_ID, {"start_page": page}, "goto.json")
    try:
        sin.commands.execute("xcpStudio.open")
    except Exception:
        _open_suite(page)
