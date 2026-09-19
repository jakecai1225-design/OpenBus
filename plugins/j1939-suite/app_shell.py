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
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPushButton,
    QSplitter,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store, vscode_theme, codicons

from session import SharedSession

PLUGIN_ID = "j1939-suite"

NAV_PAGES = [
    ("analyzer", "Live"),
    ("transport", "Transport"),
    ("diagnostics", "Diagnostics"),
    ("network", "Network"),
    ("log", "Log"),
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

        central = QWidget()
        self.setCentralWidget(central)
        root = QHBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        self.nav = QListWidget()
        self.nav.setObjectName("SuiteNav")
        self.nav.setIconSize(QSize(16, 16))
        self.nav.setFixedWidth(148)
        for key, title in NAV_PAGES:
            item = QListWidgetItem(title)
            item.setData(Qt.ItemDataRole.UserRole, key)
            codicons.set_nav_item(item, key, vscode_theme.TEXT)
            self.nav.addItem(item)
        root.addWidget(self.nav)

        right = QWidget()
        right.setObjectName("SuiteContent")
        right_l = QVBoxLayout(right)
        right_l.setContentsMargins(0, 0, 0, 0)
        right_l.setSpacing(4)

        splitter = QSplitter(Qt.Orientation.Vertical)
        self.stack = QStackedWidget()
        splitter.addWidget(self.stack)
        splitter.addWidget(self._build_log_panel())
        splitter.setSizes([520, 200])
        right_l.addWidget(splitter, 1)
        root.addWidget(right, 1)

        self.session.set_log_fn(self._log_row)

        from pages import analyzer, log_page, views

        self._pages = {}
        for key, builder in (
            ("analyzer", analyzer.build),
            ("transport", views.build_transport),
            ("diagnostics", views.build_diagnostics),
            ("network", views.build_network),
            ("log", log_page.build),
        ):
            w = builder(self, self.session, self._log_row)
            self._pages[key] = w
            self.stack.addWidget(w)

        self.nav.currentRowChanged.connect(self._on_nav)
        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        self.goto_page(page or "analyzer")

        for i in range(len(NAV_PAGES)):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))

        self._log_row("SYS", "-", b"", "J1939 Suite ready — Analyzer / Log")

    def _build_log_panel(self) -> QWidget:
        group = QWidget()
        group.setObjectName("SuiteLogHost")
        v = QVBoxLayout(group)
        v.setContentsMargins(8, 6, 8, 8)
        v.setSpacing(6)
        _log_title = QLabel("OUTPUT")
        _log_title.setObjectName("SuiteToolbarTitle")
        v.addWidget(_log_title)
        self.log_table = QTableWidget(0, 5)
        self.log_table.setHorizontalHeaderLabels(
            ["Time", "Dir", "CAN ID", "Data", "Note"])
        self.log_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.log_table.verticalHeader().setVisible(False)
        self.log_table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.log_table.horizontalHeader().setSectionResizeMode(
            4, QHeaderView.ResizeMode.Stretch)
        v.addWidget(self.log_table, 1)
        btn_row = QHBoxLayout()
        self.log_pause = QCheckBox("Pause")
        export_btn = QPushButton("Export CSV")
        clear_btn = QPushButton("Clear")
        btn_row.addWidget(self.log_pause)
        btn_row.addStretch()
        btn_row.addWidget(export_btn)
        btn_row.addWidget(clear_btn)
        v.addLayout(btn_row)
        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)
        return group

    def _on_nav(self, row: int):
        if row < 0:
            return
        self.stack.setCurrentIndex(row)
        self._persist()

    def goto_page(self, key: str):
        idx = self._page_index.get(key, 0)
        self.nav.setCurrentRow(idx)
        self.stack.setCurrentIndex(idx)

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
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "analyzer",
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
