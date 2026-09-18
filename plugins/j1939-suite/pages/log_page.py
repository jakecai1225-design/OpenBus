# -*- coding: utf-8 -*-
"""Log workspace for J1939 Suite."""

from __future__ import annotations

from PyQt6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QVBoxLayout, QWidget

from _shared import plugin_shell


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.addWidget(plugin_shell.empty_state_label(
        "Shared activity log is at the bottom of the window.\n"
        "Use Pause / Export CSV / Clear on the log strip.\n"
        "Ctrl+1 Analyzer · Ctrl+2 Log"))
    row = QHBoxLayout()
    clear_btn = QPushButton("Clear log")
    export_btn = QPushButton("Export log CSV")
    row.addWidget(clear_btn)
    row.addWidget(export_btn)
    row.addStretch()
    layout.addLayout(row)
    layout.addStretch()
    clear_btn.clicked.connect(lambda: parent.clear_log() if hasattr(parent, "clear_log") else None)
    export_btn.clicked.connect(lambda: parent._export_log() if hasattr(parent, "_export_log") else None)
    return root
