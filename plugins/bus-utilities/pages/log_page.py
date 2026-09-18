# -*- coding: utf-8 -*-
"""Log workspace for Bus Utilities."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    layout.addWidget(plugin_shell.empty_state_label(
        "The shared activity log is at the bottom of the window.\n"
        "Gateway hit notes and suite events append here.\n"
        "Use Pause / Export CSV / Clear on the log strip."))

    layout.addWidget(QLabel("Tip: Ctrl+1 Bit Timing · Ctrl+2 Gateway · Ctrl+3 Log"))

    btn_row = QHBoxLayout()
    clear_btn = QPushButton("Clear log")
    export_btn = QPushButton("Export log CSV")
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

    clear_btn.clicked.connect(_clear)
    export_btn.clicked.connect(_export)
    return root
