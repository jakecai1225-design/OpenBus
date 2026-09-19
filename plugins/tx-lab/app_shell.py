# -*- coding: utf-8 -*-
"""AppShell — TX Lab main window: nav + strip + pages + log."""

from __future__ import annotations

import os
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

from _shared import dbc_picker, plugin_shell, state_store, vscode_theme, codicons

from session import SharedSession

PLUGIN_ID = "tx-lab"

NAV_PAGES = [
    ("generator", "Transmit"),
    ("restbus", "Restbus"),
    ("replay", "Replay"),
    ("dashboard", "Panels"),
    ("log", "Log"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("TX Lab")
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
        self.nav.setSpacing(2)
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

        right_l.addWidget(self._build_strip())

        splitter = QSplitter(Qt.Orientation.Vertical)
        self.stack = QStackedWidget()
        splitter.addWidget(self.stack)

        log_widget = self._build_log_panel()
        splitter.addWidget(log_widget)
        splitter.setSizes([520, 200])
        right_l.addWidget(splitter, 1)
        root.addWidget(right, 1)

        self.session.set_log_fn(self.log)

        from pages import dashboard, generator, log_page, replay, restbus

        self._pages = {}
        builders = [
            ("generator", generator.build),
            ("restbus", restbus.build),
            ("replay", replay.build),
            ("dashboard", dashboard.build),
            ("log", log_page.build),
        ]
        for key, builder in builders:
            w = builder(self, self.session, self.log)
            self._pages[key] = w
            self.stack.addWidget(w)

        self.nav.currentRowChanged.connect(self._on_nav)
        self.session.on_dbc_changed(self._sync_dbc_label)
        self.session.on_status_changed(self._sync_status_label)

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        self.goto_page(page or "generator")

        for i in range(len(NAV_PAGES)):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))

        self.log("SYS", "TX Lab ready — Generator / Restbus / Dashboard / Log")
        self._sync_dbc_label()
        self.session.refresh_bus_status()

    # ------------------------------------------------------------------
    def _build_strip(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("SuiteToolbar")
        row = QHBoxLayout(bar)
        row.setContentsMargins(12, 6, 12, 6)
        row.setSpacing(8)

        self.dbc_label = QLabel("(no DBC)")
        self.dbc_label.setStyleSheet("font-weight:bold;")
        self.dbc_label.setMinimumWidth(220)
        row.addWidget(QLabel("DBC:"))
        row.addWidget(self.dbc_label, 1)

        browse_btn = QPushButton("Browse…")
        browse_btn.setToolTip("Optional shared DBC for Generator / Restbus / Dashboard")
        browse_btn.clicked.connect(self._browse_dbc)
        clear_btn = QPushButton("Clear DBC")
        clear_btn.clicked.connect(self._clear_dbc)
        row.addWidget(browse_btn)
        row.addWidget(clear_btn)

        sep = QLabel("|")
        sep.setStyleSheet("color:#E5E5E5;")
        row.addWidget(sep)

        self.status_label = QLabel(self.session.bus_status)
        self.status_label.setStyleSheet("color:#555;")
        row.addWidget(self.status_label)

        sep2 = QLabel("|")
        sep2.setStyleSheet("color:#E5E5E5;")
        row.addWidget(sep2)

        start_btn = QPushButton("Start all TX")
        start_btn.setToolTip("Start Generator cyclic + Restbus simulation")
        start_btn.clicked.connect(self._on_start_all)
        stop_btn = QPushButton("Stop all TX")
        stop_btn.setToolTip("Stop all suite TX sources")
        stop_btn.clicked.connect(self._on_stop_all)
        row.addWidget(start_btn)
        row.addWidget(stop_btn)
        return bar

    def _build_log_panel(self) -> QWidget:
        group = QWidget()
        group.setObjectName("SuiteLogHost")
        v = QVBoxLayout(group)
        v.setContentsMargins(8, 6, 8, 8)
        v.setSpacing(6)
        _log_title = QLabel("OUTPUT")
        _log_title.setObjectName("SuiteToolbarTitle")
        v.addWidget(_log_title)

        self.log_table = QTableWidget(0, 3)
        self.log_table.setHorizontalHeaderLabels(["Time", "Source", "Message"])
        self.log_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.log_table.verticalHeader().setVisible(False)
        self.log_table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.log_table.horizontalHeader().setSectionResizeMode(
            2, QHeaderView.ResizeMode.Stretch)
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

        self.log_pause.toggled.connect(self._on_log_pause)
        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)
        return group

    # ------------------------------------------------------------------
    def _browse_dbc(self):
        path = dbc_picker.pick_dbc(self, "Select shared DBC")
        if path and self.session.load_dbc(path):
            self._persist()
            plugin_shell.set_status(self, "DBC loaded", 3000)

    def _clear_dbc(self):
        self.session.clear_dbc()
        self._persist()

    def _sync_dbc_label(self):
        path = self.session.dbc_path
        if path:
            self.dbc_label.setText(os.path.basename(path))
            self.dbc_label.setToolTip(path)
            self.dbc_label.setStyleSheet("font-weight:bold;color:#2e7d32;")
        else:
            self.dbc_label.setText("(no DBC)")
            self.dbc_label.setToolTip("")
            self.dbc_label.setStyleSheet("font-weight:bold;color:#888;")

    def _sync_status_label(self, text: str):
        self.status_label.setText(text or "")

    def _on_start_all(self):
        n = self.session.start_all_tx()
        plugin_shell.set_status(
            self, "Started %d TX source(s)" % n if n else "Nothing to start", 2500)
        self._persist()

    def _on_stop_all(self):
        self.session.stop_all_tx()
        plugin_shell.set_status(self, "All TX stopped", 2500)
        self._persist()

    def _on_nav(self, row: int):
        if row < 0:
            return
        self.stack.setCurrentIndex(row)
        self._persist()

    def goto_page(self, key: str):
        idx = self._page_index.get(key, 0)
        self.nav.setCurrentRow(idx)
        self.stack.setCurrentIndex(idx)

    # ------------------------------------------------------------------
    def log(self, source: str, message: str, color: str | None = None):
        if self.log_pause.isChecked():
            if len(self._log_buffer) < 5000:
                self._log_buffer.append((time.time(), source, message, color))
            return
        self._append_log_row(time.time(), source, message, color)
        while self.log_table.rowCount() > 2000:
            self.log_table.removeRow(0)

    def _append_log_row(self, ts, source, message, color=None):
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        colors = {
            "ERR": QColor("#C62828"),
            "WARN": QColor("#EF6C00"),
            "OK": QColor("#2E7D32"),
            "SYS": QColor("#1565C0"),
            "TX": QColor("#1565C0"),
        }
        c = colors.get(color or source, QColor("#333"))
        row = self.log_table.rowCount()
        self.log_table.insertRow(row)
        for col, text in enumerate((tstr, source, message or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self.log_table.setItem(row, col, item)
        bar = self.log_table.verticalScrollBar()
        bar.setValue(bar.maximum())

    def _on_log_pause(self, checked: bool):
        if not checked and self._log_buffer:
            for row in self._log_buffer[:2000]:
                self._append_log_row(*row)
            del self._log_buffer[:2000]

    def clear_log(self):
        self.log_table.setRowCount(0)
        self._log_buffer.clear()

    def _export_log(self):
        rows = []
        for r in range(self.log_table.rowCount()):
            rows.append([
                self.log_table.item(r, c).text()
                if self.log_table.item(r, c) else ""
                for c in range(3)
            ])
        path = plugin_shell.export_csv(
            self, ["Time", "Source", "Message"], rows, "tx_lab_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def iter_log_rows(self):
        for r in range(self.log_table.rowCount()):
            yield [
                self.log_table.item(r, c).text()
                if self.log_table.item(r, c) else ""
                for c in range(3)
            ]

    # ------------------------------------------------------------------
    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "generator",
            "last_dbc": self.session.dbc_path or "",
            "tx_running": self.session.any_tx_running(),
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            last = saved.get("last_dbc") or ""
            if last and os.path.isfile(last):
                self.session.load_dbc(last)
            geo = saved.get("geometry_hex")
            if geo:
                from PyQt6.QtCore import QByteArray
                self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))
        except (TypeError, ValueError, OSError):
            pass

    def closeEvent(self, event):
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
        self.session.shutdown()
