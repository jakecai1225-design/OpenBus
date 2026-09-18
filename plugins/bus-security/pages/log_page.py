# -*- coding: utf-8 -*-
"""Log workspace — mirror / focus for the shared activity log."""

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
        "Fuzzer / IDS / Stress / E2E append here. Use Pause / Export / Clear "
        "on the log strip. Stop All on the rate strip aborts TX."))

    layout.addWidget(QLabel(
        "Tip: Ctrl+1…5 switches workspaces. Rate defaults apply suite-wide."))

    btn_row = QHBoxLayout()
    clear_btn = QPushButton("Clear log")
    export_btn = QPushButton("Export log CSV")
    stop_btn = QPushButton("Stop All")
    btn_row.addWidget(clear_btn)
    btn_row.addWidget(export_btn)
    btn_row.addWidget(stop_btn)
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

    def _stop():
        session.stop_all()
        plugin_shell.set_status(parent, "Stop All", 3000)

    clear_btn.clicked.connect(_clear)
    export_btn.clicked.connect(_export)
    stop_btn.clicked.connect(_stop)
    return root
