# -*- coding: utf-8 -*-
"""Deprecated stub — opens UDS Suite Diagnose workspace."""

from __future__ import annotations

import sin
from _shared import state_store

PLUGIN_ID = "uds-diagnostic"
SUITE_PAGE = "diagnose"


def _open_suite():
    state_store.save_state("uds-suite", {"start_page": SUITE_PAGE}, "goto.json")
    try:
        sin.commands.execute("udsSuite.open")
        sin.output.append(
            "uds-diagnostic is deprecated — opened UDS Suite (%s)" % SUITE_PAGE)
        return True
    except Exception as e:
        sin.output.append("Could not open UDS Suite: %s" % e)
        return False


def activate(context):
    from PyQt6.QtWidgets import QMessageBox

    ok = _open_suite()
    try:
        QMessageBox.information(
            None,
            "Moved to UDS Suite",
            "UDS Diagnostic has moved into UDS Suite.\n"
            "Opening the Diagnose workspace.\n\n"
            "Use menu command: UDS Suite",
        )
    except Exception:
        pass

    def on_open():
        _open_suite()

    context.register_command(
        "udsDiagnostic.open", on_open, "Protocol: UDS Diagnostic")
    if not ok:
        sin.output.append(
            "Install/enable uds-suite plugin, then run UDS Suite")


def deactivate():
    sin.output.append("uds-diagnostic stub deactivated")
