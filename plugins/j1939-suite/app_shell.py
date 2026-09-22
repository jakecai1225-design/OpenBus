# -*- coding: utf-8 -*-
"""AppShell — J1939 Suite: Analyzer | Log."""

from __future__ import annotations

import time
from typing import Optional

from PyQt6.QtCore import Qt, QSize
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QHeaderView,
    QMainWindow,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
)

from _shared import plugin_shell, state_store, vscode_theme, codicons, suite_chrome

from session import SharedSession

PLUGIN_ID = "j1939-suite"

NAV_PAGES = [
    ("analyzer", "Live"),
    ("transport", "Transport"),
    ("diagnostics", "Diagnostics"),
    ("network", "Network"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("J1939 Suite")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_buffer: list = []
        self._page_index = {key: i for i, (key, _) in enumerate(NAV_PAGES)}

        vscode_theme.apply(self)
        plugin_shell.attach_status_bar(self, "Ready")
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)
        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="J1939 Suite", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True)
        self.stack = self._wb.stack
        self._build_log_panel()
        self.session.set_log_fn(self._log_row)

        from pages import analyzer, views

        self._pages = {}
        for key, builder in (
            ("analyzer", analyzer.build),
            ("transport", views.build_transport),
            ("diagnostics", views.build_diagnostics),
            ("network", views.build_network),
        ):
            w = builder(self, self.session, self._log_row)
            self._pages[key] = w
            self.stack.addWidget(w)

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        if page == "log":
            page = "analyzer"
        self.goto_page(page or "analyzer")
        self._wb.set_sidebar_visible(True)
        self._wb.set_panel_visible(True)
        suite_chrome.bind_nav_shortcuts(self, NAV_PAGES, self.goto_page)
        self._log_row("SYS", "-", b"", "J1939 Suite ready")

    def _build_log_panel(self):
        self.log_pause = QCheckBox("Pause")
        self.log_pause.setToolTip("Hold new rows until unchecked")
        self._wb.panel_tools.addWidget(self.log_pause)
        self._wb.panel_tools.addStretch(1)
        export_btn = QPushButton("Export")
        export_btn.setObjectName("GhostButton")
        export_btn.setFixedHeight(22)
        export_btn.setToolTip("Export log as CSV")
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("GhostButton")
        clear_btn.setFixedHeight(22)
        clear_btn.setToolTip("Clear OUTPUT list")
        codicons.set_button(export_btn, "export", size=12)
        codicons.set_button(clear_btn, "clear", size=12)
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)
        self.log_table = suite_chrome.make_output_table()
        self.log_table.setHorizontalHeaderLabels(
            ["Time", "Dir", "CAN ID", "Data", "Note"])
        self.log_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.log_table.verticalHeader().setVisible(False)
        self.log_table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.log_table.horizontalHeader().setSectionResizeMode(
            4, QHeaderView.ResizeMode.Stretch)
        self._wb.panel_body.addWidget(self.log_table, 1)
        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)

    def goto_page(self, key: str):
        if key == "log":
            self._wb.expand_panel()
            return
        self._wb.goto_page(key)
        self._wb.set_editor_title(dict(NAV_PAGES).get(key, key))
        self._persist()

    def _log_row(self, direction, can_id, pdu, note, color=None):
        if self.log_pause.isChecked():
            if len(self._log_buffer) < 5000:
                self._log_buffer.append(
                    (time.time(), direction, can_id, pdu, note, color))
            return
        self._append_log_row(time.time(), direction, can_id, pdu, note, color)
        while self.log_table.rowCount() > 2000:
            self.log_table.removeRow(0)

    def _append_log_row(self, ts, direction, can_id, pdu, note, color=None):
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        if isinstance(pdu, (bytes, bytearray)):
            hex_str = " ".join("%02X" % b for b in pdu[:48])
        else:
            hex_str = str(pdu)
        idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
        colors = {
            "TX": QColor("#1565C0"), "RX": QColor("#2E7D32"),
            "ERR": QColor("#C62828"), "SYS": QColor("#6A1B9A"),
        }
        c = colors.get(color or direction, QColor("#333"))
        row = self.log_table.rowCount()
        self.log_table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, hex_str, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self.log_table.setItem(row, col, item)
        self.log_table.verticalScrollBar().setValue(
            self.log_table.verticalScrollBar().maximum())

    def clear_log(self):
        self.log_table.setRowCount(0)
        self._log_buffer.clear()

    def _export_log(self):
        rows = []
        for r in range(self.log_table.rowCount()):
            rows.append([
                self.log_table.item(r, c).text() if self.log_table.item(r, c) else ""
                for c in range(5)
            ])
        path = plugin_shell.export_csv(
            self, ["Time", "Dir", "CAN ID", "Data", "Note"], rows,
            "j1939_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": self._wb.current_page() or "analyzer",
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        geo = saved.get("geometry_hex")
        if geo:
            from PyQt6.QtCore import QByteArray
            self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))

    def shutdown(self):
        self._persist()
        self.session.shutdown()
