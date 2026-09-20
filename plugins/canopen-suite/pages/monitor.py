# -*- coding: utf-8 -*-
"""Monitor — live COB classification. One tool row, then the list."""

from __future__ import annotations

import time

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QHBoxLayout,
    QHeaderView,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core.cob_classify import classify_cob


KINDS = [
    "NMT", "SYNC", "TIME", "EMCY",
    "TPDO1", "TPDO2", "TPDO3", "TPDO4",
    "RPDO1", "RPDO2", "RPDO3", "RPDO4",
    "TSDO", "RSDO", "HB", "UNKNOWN",
]


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(8, 6, 8, 6)
    layout.setSpacing(6)

    tools = QHBoxLayout()
    filters = {}
    for kind in ("NMT", "SYNC", "EMCY", "TPDO1", "RPDO1", "TSDO", "RSDO", "HB", "UNKNOWN"):
        chk = QCheckBox(kind)
        chk.setChecked(True)
        chk.setToolTip("Show %s frames" % kind)
        filters[kind] = chk
        tools.addWidget(chk)
    for kind in KINDS:
        filters.setdefault(kind, None)
    tools.addStretch(1)
    pause_chk = QCheckBox("Pause")
    pause_chk.setToolTip("Stop appending frames")
    clear_btn = QPushButton("Clear")
    clear_btn.setObjectName("GhostButton")
    clear_btn.setFixedHeight(28)
    clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    export_btn = QPushButton("Export")
    export_btn.setObjectName("GhostButton")
    export_btn.setFixedHeight(28)
    export_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    tools.addWidget(pause_chk)
    tools.addWidget(clear_btn)
    tools.addWidget(export_btn)
    layout.addLayout(tools)

    table = QTableWidget(0, 6)
    table.setHorizontalHeaderLabels(
        ["Time", "CAN ID", "Kind", "Node", "DLC", "Data"])
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    table.setAlternatingRowColors(True)
    table.horizontalHeader().setSectionResizeMode(
        5, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(table, 1)

    def _filter_ok(kind: str) -> bool:
        key = kind
        if kind.startswith("TPDO"):
            key = "TPDO1"
        elif kind.startswith("RPDO"):
            key = "RPDO1"
        chk = filters.get(key)
        if chk is None:
            return True
        return chk.isChecked()

    def on_frame(frame):
        if pause_chk.isChecked():
            return
        data = bytes(frame.data) if frame.data else b""
        kind, node, _label = classify_cob(frame.id)
        if not _filter_ok(kind):
            return
        ts = time.time()
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        hex_str = " ".join("%02X" % b for b in data)
        row = table.rowCount()
        table.insertRow(row)
        vals = [
            tstr,
            "0x%03X" % (frame.id & 0x7FF),
            kind,
            str(node) if node is not None else "-",
            str(len(data)),
            hex_str,
        ]
        for col, text in enumerate(vals):
            table.setItem(row, col, QTableWidgetItem(text))
        while table.rowCount() > 3000:
            table.removeRow(0)
        table.scrollToBottom()

    session.on_bus_frame(on_frame)

    def on_clear():
        table.setRowCount(0)

    def on_export():
        rows = []
        for r in range(table.rowCount()):
            rows.append([
                table.item(r, c).text() if table.item(r, c) else ""
                for c in range(6)
            ])
        path = plugin_shell.export_csv(
            parent,
            ["Time", "CAN ID", "Kind", "Node", "DLC", "Data"],
            rows,
            "canopen_monitor.csv",
        )
        if path:
            plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)
    return root
