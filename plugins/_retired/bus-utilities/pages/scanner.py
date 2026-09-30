# -*- coding: utf-8 -*-
"""ID scanner — live unique-ID census."""

from __future__ import annotations

import time

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

from _shared import plugin_shell, vscode_theme


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "ID scanner",
        "Counts live frames. Does not transmit.")
    row = QHBoxLayout()
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    status = QLabel("Waiting")
    row.addWidget(clear_btn)
    row.addWidget(export_btn)
    row.addStretch(1)
    row.addWidget(status)
    body.addLayout(row)
    layout.addWidget(card)

    tree = QTreeWidget()
    tree.setHeaderLabels(["ID", "Count", "Ext", "DLC", "Last data", "Hz"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSortingEnabled(True)
    tree.header().setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    stats = {}
    t0 = {"v": time.monotonic()}

    def _on_frame(frame):
        st = stats.get(frame.id)
        data = bytes(getattr(frame, "data", b"") or b"")
        if st is None:
            stats[frame.id] = {
                "count": 1,
                "ext": bool(getattr(frame, "extended", False)),
                "dlc": len(data),
                "data": data,
            }
        else:
            st["count"] += 1
            st["dlc"] = len(data)
            st["data"] = data

    def _refresh():
        elapsed = max(0.001, time.monotonic() - t0["v"])
        tree.setSortingEnabled(False)
        tree.clear()
        for cid, st in stats.items():
            tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid,
                str(st["count"]),
                "Y" if st["ext"] else "N",
                str(st["dlc"]),
                " ".join("%02X" % b for b in st["data"][:8]),
                "%.1f" % (st["count"] / elapsed),
            ]))
        tree.setSortingEnabled(True)
        status.setText("%d IDs" % len(stats))

    def _clear():
        stats.clear()
        t0["v"] = time.monotonic()
        _refresh()

    def _export():
        rows = []
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            rows.append([item.text(c) for c in range(6)])
        path = plugin_shell.export_csv(
            parent, ["ID", "Count", "Ext", "DLC", "Last data", "Hz"],
            rows, "id_scan.csv")
        if path:
            log_fn("SYS", 0, b"", "ID scan exported %s" % path)

    session.on_bus_frame(_on_frame)
    clear_btn.clicked.connect(_clear)
    export_btn.clicked.connect(_export)
    timer = QTimer(root)
    timer.timeout.connect(_refresh)
    timer.start(500)
    return root
