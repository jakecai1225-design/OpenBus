# -*- coding: utf-8 -*-
"""AI Agent — read-only tool loop over Trace, DBC, and workspace."""

from __future__ import annotations

import os
import sys

import sin

PLUGIN_ID = "ai-agent"

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

_window = None


def activate(context):
    global _window
    try:
        from chat_window import ChatWindow
    except ImportError as e:
        msg = "AI Agent failed to import: %s" % e
        sin.output.append(msg)
        raise RuntimeError(msg) from e

    def on_open(*_args):
        global _window
        if _window is None:
            _window = ChatWindow()
        _window.show()
        _window.raise_()
        _window.activateWindow()

    context.register_command("aiAgent.open", on_open, "AI Agent")
    on_open()
    sin.output.append("AI Agent ready (read-only Trace / DBC tools)")


def deactivate():
    global _window
    if _window is not None:
        try:
            _window.shutdown()
        except Exception:
            pass
        try:
            _window.close()
        except Exception:
            pass
        _window = None
    sin.output.append("AI Agent deactivated")
