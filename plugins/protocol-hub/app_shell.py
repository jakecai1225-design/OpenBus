# -*- coding: utf-8 -*-
"""AppShell — Protocol Hub main window: nav + channel strip + pages + log."""

from __future__ import annotations

import time
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
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

from _shared import plugin_shell, state_store

from session import SharedSession

PLUGIN_ID = "protocol-hub"

NAV_PAGES = [
    ("nm", "NM"),
    ("isotp", "ISO-TP"),
    ("isobus", "ISOBUS"),
    ("nmea2000", "NMEA2000"),
    ("gbt27930", "GBT27930"),
    ("xcp", "XCP"),
    ("log", "Log"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("Protocol Hub")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_buffer: list = []
        self._page_index = {key: i for i, (key, _) in enumerate(NAV_PAGES)}

        plugin_shell.attach_status_bar(self, "Ready")

        central = QWidget()
        self.setCentralWidget(central)
        root = QHBoxLayout(central)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        self.nav = QListWidget()
        self.nav.setFixedWidth(130)
        self.nav.setSpacing(2)
        font = QFont()
        font.setPointSize(11)
        self.nav.setFont(font)
        for key, title in NAV_PAGES:
            item = QListWidgetItem(title)
            item.setData(Qt.ItemDataRole.UserRole, key)
            self.nav.addItem(item)
        root.addWidget(self.nav)

        right = QWidget()
        right_l = QVBoxLayout(right)
        right_l.setContentsMargins(0, 0, 0, 0)
        right_l.setSpacing(4)

        right_l.addWidget(self._build_session_strip())

        splitter = QSplitter(Qt.Orientation.Vertical)
        self.stack = QStackedWidget()
        splitter.addWidget(self.stack)

        log_widget = self._build_log_panel()
        splitter.addWidget(log_widget)
        splitter.setSizes([520, 220])
        right_l.addWidget(splitter, 1)
        root.addWidget(right, 1)

        self.session.set_log_fn(self._log_row)

        from pages import (
            gbt27930, isobus, isotp, log_page, nm, nmea2000, xcp,
        )

        self._pages = {}
        builders = [
            ("nm", nm.build),
            ("isotp", isotp.build),
            ("isobus", isobus.build),
            ("nmea2000", nmea2000.build),
            ("gbt27930", gbt27930.build),
            ("xcp", xcp.build),
            ("log", log_page.build),
        ]
        for key, builder in builders:
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
        self.goto_page(page or "nm")

        for i in range(len(NAV_PAGES)):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))

        self._log_row(
            "SYS", "-", b"",
            "Protocol Hub ready — NM / ISO-TP / ISOBUS / NMEA2000 / GBT27930 / XCP / Log")

    def _build_session_strip(self) -> QWidget:
        bar = QGroupBox("Session")
        row = QHBoxLayout(bar)

        row.addWidget(QLabel("Channel filter hint:"))
        self.hint_edit = QLineEdit()
        self.hint_edit.setPlaceholderText(
            "e.g. CH0 / vcan0 — advisory label for which bus you are watching")
        self.hint_edit.setMinimumWidth(280)
        row.addWidget(self.hint_edit, 1)

        apply_btn = QPushButton("Apply")
        apply_btn.setToolTip("Apply channel filter hint to shared session")
        apply_btn.clicked.connect(self._on_apply_hint)
        row.addWidget(apply_btn)

        clear_btn = QPushButton("Clear")
        clear_btn.clicked.connect(self._on_clear_hint)
        row.addWidget(clear_btn)

        self.hint_status = QLabel("(no filter)")
        self.hint_status.setStyleSheet("color:#666;")
        row.addWidget(self.hint_status)
        return bar

    def _build_log_panel(self) -> QWidget:
        group = QGroupBox("Activity log")
        v = QVBoxLayout(group)

        self.log_table = QTableWidget(0, 5)
        self.log_table.setHorizontalHeaderLabels(
            ["Time", "Dir", "CAN ID", "Data", "Note"])
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

    def _on_apply_hint(self):
        self.session.set_channel_hint(self.hint_edit.text())
        hint = self.session.channel_hint
        self.hint_status.setText(hint or "(no filter)")
        plugin_shell.set_status(self, "Channel hint applied", 2500)
        self._persist()

    def _on_clear_hint(self):
        self.hint_edit.clear()
        self.session.set_channel_hint("")
        self.hint_status.setText("(no filter)")
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
            "SYS": QColor("#6A1B9A"),
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
            ["Time", "Dir", "CAN ID", "Data", "Note"],
            rows,
            "protocol_hub_log.csv",
        )
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "channel_hint": self.hint_edit.text(),
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "nm",
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            hint = saved.get("channel_hint") or ""
            self.hint_edit.setText(hint)
            self.session.channel_hint = hint
            self.hint_status.setText(hint or "(no filter)")
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
