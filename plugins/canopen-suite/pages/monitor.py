# -*- coding: utf-8 -*-
"""Monitor workspace — live COB classification table + CSV export."""

from __future__ import annotations

import time

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

    layout.addWidget(plugin_shell.help_label(
        "Live bus frames classified by pre-defined connection set "
        "(NMT / SYNC / EMCY / PDO / SDO / Heartbeat). Toggle filters below."))

    filters = {}
    filt_row = QHBoxLayout()
    for kind in ("NMT", "SYNC", "EMCY", "TPDO1", "RPDO1", "TSDO", "RSDO", "HB", "UNKNOWN"):
        chk = QCheckBox(kind)
        chk.setChecked(True)
        filters[kind] = chk
        filt_row.addWidget(chk)
    # PDO2-4 share TPDO/RPDO parent toggles loosely — keep all kinds enabled by default
    for kind in KINDS:
        if kind not in filters:
            filters[kind] = None  # always show
    filt_row.addStretch()
    layout.addLayout(filt_row)

    btn_row = QHBoxLayout()
    pause_chk = QCheckBox("Pause")
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    btn_row.addWidget(pause_chk)
    btn_row.addStretch()
    btn_row.addWidget(clear_btn)
    btn_row.addWidget(export_btn)
    layout.addLayout(btn_row)

    table = QTableWidget(0, 6)
    table.setHorizontalHeaderLabels(
        ["Time", "CAN ID", "Kind", "Node", "DLC", "Data"])
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    table.horizontalHeader().setSectionResizeMode(
        5, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(table, 1)

    def _filter_ok(kind: str) -> bool:
        # Map TPDO2-4 -> TPDO1 checkbox, RPDO2-4 -> RPDO1
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
        bar = table.verticalScrollBar()
        bar.setValue(bar.maximum())

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
