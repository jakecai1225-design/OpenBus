# -*- coding: utf-8 -*-
"""Log workspace — mirror / focus for the shared activity log."""

from __future__ import annotations

from PyQt6.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget

from _shared import plugin_shell
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(_ui.PAD_X * 2, 16, _ui.PAD_X * 2, 16)
    layout.setSpacing(12)

    layout.addWidget(plugin_shell.empty_state_label(
        "The shared activity log is at the bottom of the window.\n"
        "All workspaces append here. Use Pause / Export CSV / Clear on the log strip."))

    tip = QLabel(
        "Tip: Ctrl+1…6 switches workspaces. Node-ID and EDS path apply suite-wide.")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    layout.addWidget(tip)

    btn_row = QHBoxLayout()
    btn_row.setSpacing(_ui.GAP)
    clear_btn = _ui.ghost_btn("Clear log", "Clear the shared OUTPUT panel", "clear")
    export_btn = _ui.ghost_btn(
        "Export log CSV", "Save OUTPUT as a CSV file", "export")
    btn_row.addWidget(clear_btn)
    btn_row.addWidget(export_btn)
    btn_row.addStretch()
    layout.addLayout(btn_row)
    layout.addStretch()

    def _clear():
        if hasattr(parent, "clear_log"):
            parent.clear_log()
            plugin_shell.set_status(parent, "Log cleared", 2000)

    def _export():
        if hasattr(parent, "_export_log"):
            parent._export_log()
        elif hasattr(parent, "export_log"):
            parent.export_log()

    clear_btn.clicked.connect(_clear)
    export_btn.clicked.connect(_export)
    return root
