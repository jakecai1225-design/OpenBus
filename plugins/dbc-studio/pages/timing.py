# -*- coding: utf-8 -*-
"""Timing workspace — cyclic bus-load estimate."""

from __future__ import annotations

from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, plugin_shell, state_store, suite_chrome
from core import timing_lite

PLUGIN_ID = "dbc-studio"


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    crow.addWidget(QLabel("Baud"))
    baud = QComboBox()
    baud.addItems([
        "125000", "250000", "500000", "666666", "800000", "1000000",
    ])
    baud.setCurrentText("500000")
    baud.setFixedHeight(28)
    baud.setMinimumWidth(110)
    crow.addWidget(baud)
    stuffing = QCheckBox("Stuffing ×1.2")
    stuffing.setChecked(True)
    stuffing.setToolTip("Lite worst-case bit-stuffing factor")
    crow.addWidget(stuffing)
    crow.addStretch(1)
    refresh_btn = QPushButton("Recalculate")
    refresh_btn.setFixedHeight(28)
    codicons.set_button(refresh_btn, "refresh", primary=True)
    export_btn = QPushButton("Export CSV")
    export_btn.setObjectName("GhostButton")
    export_btn.setFixedHeight(28)
    codicons.set_button(export_btn, "export")
    crow.addWidget(refresh_btn)
    crow.addWidget(export_btn)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl)
    bl.setSpacing(10)

    hint = QLabel(
        "Timing lite — estimate cyclic bus load from GenMsgCycleTime / cycle. "
        "Event frames (cycle 0) are listed but excluded from load%.")
    hint.setWordWrap(True)
    hint.setStyleSheet("color:#78909c;font-size:12px;")
    bl.addWidget(hint)

    summary = QLabel("Open a DBC with cycle times to estimate load")
    summary.setObjectName("SuiteSectionTitle")
    bl.addWidget(summary)

    band = QLabel("")
    band.setStyleSheet("font-size:12px;font-weight:600;")
    bl.addWidget(band)

    table = QTableWidget(0, 7)
    table.setObjectName("SuiteMatrix")
    table.setHorizontalHeaderLabels([
        "Message", "ID", "DLC", "Cycle ms", "Bits", "fps", "Share %",
    ])
    table.setAlternatingRowColors(True)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.verticalHeader().setDefaultSectionSize(28)
    table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    table.setStyleSheet(
        "QTableWidget#SuiteMatrix { gridline-color: #EEEEEE; }"
        "QTableWidget#SuiteMatrix::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
    )
    bl.addWidget(table, 1)
    layout.addWidget(body, 1)

    last = {"result": None}

    def _paint():
        try:
            br = int(baud.currentText())
        except ValueError:
            br = 500000
        result = timing_lite.estimate_bus_load(
            document.db, br, stuffing.isChecked())
        last["result"] = result
        load = result["load_pct"]
        summary.setText(
            "Load  %.1f%%   ·   %d cyclic / %d event   ·   "
            "%.0f bit/s @ %d bit/s"
            % (load, result["cyclic"], result["event"],
               result["bits_per_s"], result["baud"]))
        level = timing_lite.load_band(load)
        if level == "ok":
            band.setText("●  Comfortable (< 30%)")
            band.setStyleSheet("color:#2E7D32;font-size:12px;font-weight:600;")
        elif level == "warn":
            band.setText("●  Elevated (30–50%) — review high-rate frames")
            band.setStyleSheet("color:#EF6C00;font-size:12px;font-weight:600;")
        else:
            band.setText("●  High (≥ 50%) — risk of bus saturation")
            band.setStyleSheet("color:#C62828;font-size:12px;font-weight:600;")

        rows = [r for r in result["rows"] if r["kind"] == "cyclic"]
        rows.sort(key=lambda r: -r["share_pct"])
        # Append event rows after cyclic
        rows += [r for r in result["rows"] if r["kind"] == "event"]

        table.setRowCount(len(rows))
        for i, r in enumerate(rows):
            cells = [
                r["name"] + ("" if r["kind"] == "cyclic" else "  [event]"),
                "0x%X" % r["can_id"],
                str(r["dlc"]),
                str(r["cycle_ms"]) if r["cycle_ms"] else "—",
                str(r["bits"]),
                ("%.1f" % r["fps"]) if r["fps"] else "—",
                ("%.2f" % r["share_pct"]) if r["share_pct"] else "—",
            ]
            color = None
            if r["kind"] == "cyclic" and r["share_pct"] >= 10:
                color = QColor("#C62828")
            elif r["kind"] == "cyclic" and r["share_pct"] >= 5:
                color = QColor("#EF6C00")
            for c, text in enumerate(cells):
                item = QTableWidgetItem(text)
                if color is not None and c == 6:
                    item.setForeground(color)
                table.setItem(i, c, item)
        plugin_shell.set_status(
            shell, "Bus load %.1f%%" % load, 2500)

    def _export():
        result = last.get("result") or timing_lite.estimate_bus_load(
            document.db, int(baud.currentText()), stuffing.isChecked())
        rows = [
            [
                r["name"], "0x%X" % r["can_id"], r["dlc"], r["cycle_ms"],
                r["bits"], "%.3f" % r["fps"], "%.4f" % r["share_pct"], r["kind"],
            ]
            for r in result["rows"]
        ]
        path = plugin_shell.export_csv(
            shell,
            ["message", "id", "dlc", "cycle_ms", "bits", "fps", "share_pct", "kind"],
            rows, "dbc_timing.csv")
        if path:
            log_fn("Timing", "CSV %s" % path)
            _maybe_open(path)

    def _maybe_open(path):
        try:
            import os
            os.startfile(path)  # noqa: PTH123 — Windows open
        except Exception:
            pass

    refresh_btn.clicked.connect(_paint)
    export_btn.clicked.connect(_export)
    baud.currentIndexChanged.connect(lambda _i: _paint())
    stuffing.toggled.connect(lambda _c: _paint())
    document.on_changed(_paint)

    saved = state_store.load_state(PLUGIN_ID, "timing.json") or {}
    if saved.get("baud") in [baud.itemText(i) for i in range(baud.count())]:
        baud.setCurrentText(saved["baud"])
    if "stuffing" in saved:
        stuffing.setChecked(bool(saved["stuffing"]))

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "baud": baud.currentText(),
            "stuffing": stuffing.isChecked(),
        }, "timing.json")

    baud.currentIndexChanged.connect(lambda _i: _persist())
    stuffing.toggled.connect(lambda _c: _persist())

    _paint()
    return root
