# -*- coding: utf-8 -*-
"""AppShell — Bus Security main window: nav + rate strip + pages + log."""

from __future__ import annotations

import time
from typing import Optional

from PyQt6.QtCore import Qt, QSize
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QDoubleSpinBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store, vscode_theme, codicons

from session import SharedSession

PLUGIN_ID = "bus-security"

NAV_PAGES = [
    ("fuzzer", "Fuzzer"),
    ("ids", "IDS"),
    ("stress", "Stress"),
    ("e2e", "Integrity"),
    ("findings", "Findings"),
    ("log", "Log"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("Bus Security")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_buffer: list = []
        self._page_index = {key: i for i, (key, _) in enumerate(NAV_PAGES)}

        plugin_shell.attach_status_bar(self, "Ready — Stop All aborts TX")
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

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

        right_l.addWidget(self._build_rate_strip())

        splitter = QSplitter(Qt.Orientation.Vertical)
        self.stack = QStackedWidget()
        splitter.addWidget(self.stack)

        log_widget = self._build_log_panel()
        splitter.addWidget(log_widget)
        splitter.setSizes([520, 200])
        right_l.addWidget(splitter, 1)
        root.addWidget(right, 1)

        self.session.set_log_fn(self.log)

        from pages import e2e, findings, fuzzer, ids, log_page, stress

        self._pages = {}
        builders = [
            ("fuzzer", fuzzer.build),
            ("ids", ids.build),
            ("stress", stress.build),
            ("e2e", e2e.build),
            ("findings", findings.build),
            ("log", log_page.build),
        ]
        for key, builder in builders:
            w = builder(self, self.session, self.log)
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
        self.goto_page(page or "fuzzer")

        for i in range(len(NAV_PAGES)):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))

        self.log(
            "SYS",
            "Bus Security ready — Fuzzer / IDS / Stress / E2E / Log "
            "(bench / isolated networks only for TX)")

    # ------------------------------------------------------------------
    def _build_rate_strip(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("SuiteToolbar")
        row = QHBoxLayout(bar)
        row.setContentsMargins(12, 6, 12, 6)
        row.setSpacing(8)
        _t = QLabel("SAFETY")
        _t.setObjectName("SuiteToolbarTitle")
        row.addWidget(_t)

        self.interval_spin = QSpinBox()
        self.interval_spin.setRange(1, 10000)
        self.interval_spin.setValue(50)
        self.interval_spin.setSuffix(" ms")
        self.interval_spin.setToolTip("Default fuzzer send interval")

        self.limit_spin = QSpinBox()
        self.limit_spin.setRange(0, 1000000)
        self.limit_spin.setValue(500)
        self.limit_spin.setSpecialValueText("unlimited")
        self.limit_spin.setToolTip("Default fuzzer send cap (0 = unlimited)")

        self.load_spin = QDoubleSpinBox()
        self.load_spin.setRange(1, 100)
        self.load_spin.setValue(50)
        self.load_spin.setSuffix(" %")
        self.load_spin.setToolTip("Default stress load target")

        self.gap_spin = QSpinBox()
        self.gap_spin.setRange(0, 10000)
        self.gap_spin.setValue(0)
        self.gap_spin.setSuffix(" ms")
        self.gap_spin.setToolTip("Default min frame gap for stress")

        for lbl, w in (
            ("Interval:", self.interval_spin),
            ("Send limit:", self.limit_spin),
            ("Load:", self.load_spin),
            ("Min gap:", self.gap_spin),
        ):
            row.addWidget(QLabel(lbl))
            row.addWidget(w)

        apply_btn = QPushButton("Apply defaults")
        apply_btn.setToolTip("Push strip values to Fuzzer / Stress pages")
        apply_btn.clicked.connect(self._on_apply_defaults)
        row.addWidget(apply_btn)

        sep = QLabel("|")
        sep.setStyleSheet("color:#E5E5E5;")
        row.addWidget(sep)

        stop_btn = QPushButton("Stop All")
        stop_btn.setToolTip("Emergency stop — abort all TX and monitors")
        stop_btn.setStyleSheet(
            "QPushButton{background:#c62828;color:white;font-weight:bold;"
            "padding:4px 14px;}"
            "QPushButton:hover{background:#b71c1c;}")
        stop_btn.clicked.connect(self._on_stop_all)
        row.addWidget(stop_btn)
        row.addStretch()
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
    def log(self, source: str, message: str, color: str | None = None):
        if self.log_pause.isChecked():
            if len(self._log_buffer) < 5000:
                self._log_buffer.append((time.time(), source, message, color))
            return
        self._append_log_row(time.time(), source, message, color)
        while self.log_table.rowCount() > 2000:
            self.log_table.removeRow(0)

    def _on_log_pause(self, checked: bool):
        if not checked and self._log_buffer:
            for row in self._log_buffer[:2000]:
                self._append_log_row(*row)
            del self._log_buffer[:2000]

    def _append_log_row(self, ts, source, message, color=None):
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        colors = {
            "ERR": QColor("#C62828"),
            "WARN": QColor("#EF6C00"),
            "OK": QColor("#2E7D32"),
            "SYS": QColor("#1565C0"),
            "TX": QColor("#6A1B9A"),
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
            self, ["Time", "Source", "Message"], rows, "bus_security_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    # ------------------------------------------------------------------
    def _on_apply_defaults(self):
        self.session.apply_defaults(
            interval_ms=self.interval_spin.value(),
            send_limit=self.limit_spin.value(),
            load_pct=self.load_spin.value(),
            gap_ms=self.gap_spin.value(),
        )
        plugin_shell.set_status(self, "Defaults applied", 2500)
        self._persist()

    def _on_stop_all(self):
        self.session.stop_all()
        plugin_shell.set_status(self, "Stop All — TX halted", 4000)

    def _on_nav(self, row: int):
        if row < 0:
            return
        self.stack.setCurrentIndex(row)
        self._persist()

    def goto_page(self, key: str):
        idx = self._page_index.get(key, 0)
        self.nav.setCurrentRow(idx)
        self.stack.setCurrentIndex(idx)

    def confirm_danger(self, title: str, text: str) -> bool:
        reply = QMessageBox.warning(
            self, title, text,
            QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Cancel)
        return reply == QMessageBox.StandardButton.Ok

    # ------------------------------------------------------------------
    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "interval_ms": self.interval_spin.value(),
            "send_limit": self.limit_spin.value(),
            "load_pct": self.load_spin.value(),
            "gap_ms": self.gap_spin.value(),
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "fuzzer",
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            if "interval_ms" in saved:
                self.interval_spin.setValue(int(saved["interval_ms"]))
            if "send_limit" in saved:
                self.limit_spin.setValue(int(saved["send_limit"]))
            if "load_pct" in saved:
                self.load_spin.setValue(float(saved["load_pct"]))
            if "gap_ms" in saved:
                self.gap_spin.setValue(int(saved["gap_ms"]))
            self.session.apply_defaults(
                interval_ms=self.interval_spin.value(),
                send_limit=self.limit_spin.value(),
                load_pct=self.load_spin.value(),
                gap_ms=self.gap_spin.value(),
            )
            geo = saved.get("geometry_hex")
            if geo:
                from PyQt6.QtCore import QByteArray
                self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))
        except (TypeError, ValueError):
            pass

    def closeEvent(self, event):
        self.session.stop_all()
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
        self.session.shutdown()
