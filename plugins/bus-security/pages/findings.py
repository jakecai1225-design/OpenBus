# -*- coding: utf-8 -*-
"""Findings workspace — alerts noted by IDS (and other pages)."""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QHeaderView,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, vscode_theme


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "Findings",
        "IDS alerts land here. Export is CSV. Stop All on the toolbar aborts TX.")
    row = QHBoxLayout()
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    row.addWidget(clear_btn)
    row.addWidget(export_btn)
    row.addStretch(1)
    body.addLayout(row)
    layout.addWidget(card)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Time", "Level", "Kind", "Detail"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    def _refresh():
        tree.clear()
        for ts, level, kind, text in session.findings[-400:]:
            tree.addTopLevelItem(QTreeWidgetItem([
                time.strftime("%H:%M:%S", time.localtime(ts)),
                level, kind, text,
            ]))
        tree.scrollToBottom()

    def _export():
        rows = []
        for ts, level, kind, text in session.findings:
            rows.append([
                time.strftime("%H:%M:%S", time.localtime(ts)),
                level, kind, text,
            ])
        path = plugin_shell.export_csv(
            parent, ["Time", "Level", "Kind", "Detail"], rows, "findings.csv")
        if path:
            log_fn("FIND", "Exported %s" % path)

    clear_btn.clicked.connect(lambda: (session.clear_findings(), _refresh()))
    export_btn.clicked.connect(_export)
    session.on_finding(_refresh)
    timer = QTimer(root)
    timer.timeout.connect(_refresh)
    timer.start(500)
    return root
