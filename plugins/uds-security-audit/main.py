# -*- coding: utf-8 -*-
"""Deprecated stub — opens UDS Suite Security workspace."""

from __future__ import annotations

import sin
from _shared import state_store

SUITE_PAGE = "security"


def _open_suite():
    state_store.save_state("uds-suite", {"start_page": SUITE_PAGE}, "goto.json")
    try:
        sin.commands.execute("udsSuite.open")
        sin.output.append(
            "uds-security-audit is deprecated — opened UDS Suite (%s)" % SUITE_PAGE)
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
            "UDS Security Audit has moved into UDS Suite.\n"
            "Opening the Security workspace.\n\n"
            "Use menu command: UDS Suite",
        )
    except Exception:
        pass

    def on_open():
        _open_suite()

    context.register_command(
        "udsSecurityAudit.open", on_open, "Diagnostic: UDS Security Audit")
    if not ok:
        sin.output.append(
            "Install/enable uds-suite plugin, then run UDS Suite")


def deactivate():
    sin.output.append("uds-security-audit stub deactivated")
