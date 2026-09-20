# -*- coding: utf-8 -*-
"""AI Agent — Windows A+B workbench (browser + Edge embed); Tauri later."""

from __future__ import annotations

import os
import sys
import traceback

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
        try:
            alive = False
            if _window is not None:
                try:
                    _ = _window.windowTitle()
                    alive = True
                except RuntimeError:
                    _window = None
            if not alive:
                _window = ChatWindow()
            _window.show()
            _window.raise_()
            _window.activateWindow()
        except Exception as e:
            sin.output.append(
                "AI Agent open failed: %s\n%s" % (e, traceback.format_exc()))
            raise

    context.register_command("aiAgent.open", on_open, "AI Agent")
    on_open()
    sin.output.append(
        "AI Agent ready (Edge embed B / browser A; Orchestrator or Agents SDK)")


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
