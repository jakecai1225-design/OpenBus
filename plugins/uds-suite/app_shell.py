# -*- coding: utf-8 -*-
"""AppShell — UDS Suite main window: nav + connection strip + pages + log."""

from __future__ import annotations

import time
from typing import Callable, Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPushButton,
    QSplitter,
    QSpinBox,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store

from session import SharedSession

PLUGIN_ID = "uds-suite"

NAV_PAGES = [
    ("diagnose", "Diagnose"),
    ("scan", "Scan"),
    ("batch", "Batch"),
    ("security", "Security"),
    ("profiles", "Profiles"),
    ("log", "Log"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("UDS Suite")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_paused = False
        self._log_buffer: list = []
        self._page_index = {key: i for i, (key, _) in enumerate(NAV_PAGES)}

        plugin_shell.attach_status_bar(self, "Ready")

        central = QWidget()
        self.setCentralWidget(central)
        root = QHBoxLayout(central)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        # ---- Left nav ----
        self.nav = QListWidget()
        self.nav.setFixedWidth(140)
        self.nav.setSpacing(2)
        font = QFont()
        font.setPointSize(11)
        self.nav.setFont(font)
        for key, title in NAV_PAGES:
            item = QListWidgetItem(title)
            item.setData(Qt.ItemDataRole.UserRole, key)
            self.nav.addItem(item)
        root.addWidget(self.nav)

        # ---- Right column: strip + stack + log ----
        right = QWidget()
        right_l = QVBoxLayout(right)
        right_l.setContentsMargins(0, 0, 0, 0)
        right_l.setSpacing(4)

        right_l.addWidget(self._build_connection_strip())

        splitter = QSplitter(Qt.Orientation.Vertical)
        self.stack = QStackedWidget()
        splitter.addWidget(self.stack)

        log_widget = self._build_log_panel()
        splitter.addWidget(log_widget)
        splitter.setSizes([520, 220])
        right_l.addWidget(splitter, 1)
        root.addWidget(right, 1)

        self.session.set_log_fn(self._log_row)

        # Build pages
        from pages import diagnose, scan, batch, security, profiles, log_page

        self._pages = {}
        builders = [
            ("diagnose", diagnose.build),
            ("scan", scan.build),
            ("batch", batch.build),
            ("security", security.build),
            ("profiles", profiles.build),
            ("log", log_page.build),
        ]
        for key, builder in builders:
            w = builder(self, self.session, self._log_row)
            self._pages[key] = w
            self.stack.addWidget(w)

        self.nav.currentRowChanged.connect(self._on_nav)
        self.session.on_session_changed(self._on_session_label)
        self.session.on_ids_changed(self._sync_spins_from_session)

        # Frame feed
        context.on_frame(self.session.on_frame)

        # Restore state / start page
        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        self.goto_page(page or "diagnose")

        for i in range(6):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))

        self._log_row(
            "RX", "-", b"",
            "UDS Suite ready — use connection strip, then Diagnose / Scan / …")

    # ------------------------------------------------------------------
    def _hex_spin(self, lo, hi, val):
        s = QSpinBox()
        s.setRange(lo, hi)
        s.setDisplayIntegerBase(16)
        s.setPrefix("0x")
        s.setValue(val)
        return s

    def _build_connection_strip(self) -> QWidget:
        bar = QGroupBox("Connection")
        row = QHBoxLayout(bar)

        self.tx_spin = self._hex_spin(1, 0x7FF, 0x7E0)
        self.func_spin = self._hex_spin(1, 0x7FF, 0x7DF)
        self.rx_spin = self._hex_spin(1, 0x7FF, 0x7E8)
        self.func_check = QCheckBox("Functional")
        self.func_check.setToolTip("Functional addressing for requests")

        for lbl, w in (
            ("TX:", self.tx_spin),
            ("Func:", self.func_spin),
            ("RX:", self.rx_spin),
            (None, self.func_check),
        ):
            if lbl:
                row.addWidget(QLabel(lbl))
            row.addWidget(w)

        apply_btn = QPushButton("Apply")
        apply_btn.setToolTip("Apply IDs to shared session")
        apply_btn.clicked.connect(self._on_apply_ids)
        row.addWidget(apply_btn)

        sep = QLabel("|")
        sep.setStyleSheet("color:#aaa;")
        row.addWidget(sep)

        self.session_label = QLabel("Session: unknown")
        self.session_label.setStyleSheet("font-weight:bold;")
        for code, label in (
            (0x01, "Default"),
            (0x03, "Extended"),
            (0x02, "Programming"),
        ):
            b = QPushButton(label)
            b.setToolTip("DiagnosticSessionControl 0x%02X" % code)
            b.clicked.connect(lambda _c=False, s=code: self.session.go_session(s))
            row.addWidget(b)
        row.addWidget(self.session_label)

        self.tp_check = QCheckBox("TesterPresent")
        self.tp_check.setToolTip("Send 3E 80 every 2s (suppress positive)")
        self.tp_check.toggled.connect(self._on_tp_toggled)
        row.addWidget(self.tp_check)
        row.addStretch()
        return bar

    def _build_log_panel(self) -> QWidget:
        group = QGroupBox("Activity log")
        v = QVBoxLayout(group)

        self.log_table = QTableWidget(0, 5)
        self.log_table.setHorizontalHeaderLabels(
            ["Time", "Dir", "CAN ID", "PDU", "Note"])
        self.log_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.log_table.verticalHeader().setVisible(False)
        self.log_table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.log_table.horizontalHeader().setSectionResizeMode(
            3, QHeaderView.ResizeMode.ResizeToContents)
        self.log_table.horizontalHeader().setSectionResizeMode(
            4, QHeaderView.ResizeMode.Stretch)
        v.addWidget(self.log_table, 1)

        btn_row = QHBoxLayout()
        self.log_pause = QCheckBox("Pause")
        self.log_frames = QCheckBox("Show ISO-TP frames")
        self.log_frames.toggled.connect(
            lambda c: setattr(self.session, "show_isotp_frames", c))
        export_btn = QPushButton("Export CSV")
        clear_btn = QPushButton("Clear")
        btn_row.addWidget(self.log_pause)
        btn_row.addWidget(self.log_frames)
        btn_row.addStretch()
        btn_row.addWidget(export_btn)
        btn_row.addWidget(clear_btn)
        v.addLayout(btn_row)

        self.log_pause.toggled.connect(self._on_log_pause)
        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)
        return group

    # ------------------------------------------------------------------
    def _on_apply_ids(self):
        self.session.functional = self.func_check.isChecked()
        self.session.apply_ids(
            self.tx_spin.value(), self.rx_spin.value(), self.func_spin.value())
        plugin_shell.set_status(self, "Connection applied", 2500)
        self._persist()

    def _on_tp_toggled(self, checked: bool):
        self.session.tester_present = checked
        plugin_shell.set_status(
            self, "TesterPresent %s" % ("on" if checked else "off"), 2000)

    def _on_session_label(self, name: str):
        self.session_label.setText("Session: %s" % name)

    def _sync_spins_from_session(self):
        self.tx_spin.blockSignals(True)
        self.rx_spin.blockSignals(True)
        self.func_spin.blockSignals(True)
        self.tx_spin.setValue(self.session.tx_id)
        self.rx_spin.setValue(self.session.rx_id)
        self.func_spin.setValue(self.session.func_id)
        self.tx_spin.blockSignals(False)
        self.rx_spin.blockSignals(False)
        self.func_spin.blockSignals(False)

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
            if len(pdu) > 48:
                hex_str += " ..."
        else:
            hex_str = str(pdu)
        idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
        colors = {
            "TX": QColor("#1565C0"),
            "RX": QColor("#2E7D32"),
            "ERR": QColor("#C62828"),
            "FC": QColor("#6A1B9A"),
        }
        c = colors.get(color or direction, QColor("#333"))
        row = self.log_table.rowCount()
        self.log_table.insertRow(row)
        for col, text in enumerate(
                (tstr, direction, idstr, hex_str, note or "")):
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
                for c in range(5)
            ])
        path = plugin_shell.export_csv(
            self,
            ["Time", "Dir", "CAN ID", "PDU", "Note"],
            rows,
            "uds_suite_log.csv",
        )
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def iter_log_rows(self):
        for r in range(self.log_table.rowCount()):
            yield [
                self.log_table.item(r, c).text()
                if self.log_table.item(r, c) else ""
                for c in range(5)
            ]

    # ------------------------------------------------------------------
    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "tx_id": self.tx_spin.value(),
            "rx_id": self.rx_spin.value(),
            "func_id": self.func_spin.value(),
            "functional": self.func_check.isChecked(),
            "tester_present": self.tp_check.isChecked(),
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "diagnose",
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            if "tx_id" in saved:
                self.tx_spin.setValue(int(saved["tx_id"]))
            if "rx_id" in saved:
                self.rx_spin.setValue(int(saved["rx_id"]))
            if "func_id" in saved:
                self.func_spin.setValue(int(saved["func_id"]))
            if "functional" in saved:
                self.func_check.setChecked(bool(saved["functional"]))
            if "tester_present" in saved:
                self.tp_check.setChecked(bool(saved["tester_present"]))
            self.session.functional = self.func_check.isChecked()
            self.session.tester_present = self.tp_check.isChecked()
            self.session.apply_ids(
                self.tx_spin.value(),
                self.rx_spin.value(),
                self.func_spin.value(),
            )
            geo = saved.get("geometry_hex")
            if geo:
                from PyQt6.QtCore import QByteArray
                self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))
        except (TypeError, ValueError):
            pass

    def closeEvent(self, event):
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
        self.session.shutdown()
