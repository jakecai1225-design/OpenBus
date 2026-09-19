# -*- coding: utf-8 -*-
"""Readiness workspace — Mode 01 PID 01 monitor summary."""

from __future__ import annotations

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import vscode_theme
from core.readiness import decode_pid01
from pages import scanner


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "Monitors",
        "Reads Mode 01 PID 01. Complete means the monitor has finished since codes were cleared.")
    row = QHBoxLayout()
    read_btn = QPushButton("Read monitors")
    summary = QLabel("No sample yet")
    row.addWidget(read_btn)
    row.addStretch(1)
    row.addWidget(summary)
    body.addLayout(row)
    layout.addWidget(card)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Monitor", "Kind", "Supported", "Complete"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    def _refresh():
        raw = scanner.readiness_raw()
        if len(raw) < 1:
            return
        info = decode_pid01(raw)
        summary.setText(
            "MIL %s · DTCs %d · %s"
            % ("ON" if info["mil"] else "OFF", info["dtc_count"], info["ignition"]))
        tree.clear()
        for mon in info["monitors"]:
            if not mon["supported"] and mon["kind"] != "continuous":
                continue
            tree.addTopLevelItem(QTreeWidgetItem([
                mon["name"],
                mon["kind"],
                "yes" if mon["supported"] else "no",
                "yes" if mon["complete"] else "no",
            ]))

    def _read():
        if not scanner.request_pid(1, 0x01, functional=True):
            summary.setText("Open Scanner once so ISO-TP is ready")
            return
        log_fn("RX", 0, b"", "Readiness request Mode 01 PID 01")

    read_btn.clicked.connect(_read)
    timer = QTimer(root)
    timer.timeout.connect(_refresh)
    timer.start(400)
    return root
