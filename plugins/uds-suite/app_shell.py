# -*- coding: utf-8 -*-
"""AppShell — UDS Suite.

Minimal shell: activity nav + page stack only.
Connection controls and OUTPUT live on pages that need them
(not forced onto every workspace).
"""

from __future__ import annotations

import time
from typing import Optional

from PyQt6.QtCore import Qt, QSize
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QHeaderView,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QHBoxLayout,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store, vscode_theme, codicons

from session import SharedSession

PLUGIN_ID = "uds-suite"

NAV_PAGES = [
    ("setup", "Setup"),
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
        self._log_rows: list = []  # (ts, dir, id, pdu, note, color)
        self._log_table: Optional[QTableWidget] = None
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
        self.nav.setFixedWidth(148)
        self.nav.setIconSize(QSize(16, 16))
        self.nav.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.nav.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        for key, title in NAV_PAGES:
            item = QListWidgetItem(title)
            item.setData(Qt.ItemDataRole.UserRole, key)
            codicons.set_nav_item(item, key, vscode_theme.TEXT)
            self.nav.addItem(item)
        root.addWidget(self.nav)

        content = QWidget()
        content.setObjectName("SuiteContent")
        cl = QVBoxLayout(content)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)
        self.stack = QStackedWidget()
        cl.addWidget(self.stack, 1)
        root.addWidget(content, 1)

        self.session.set_log_fn(self._log_row)

        from pages import setup, diagnose, scan, batch, security, profiles, log_page

        self._pages = {}
        for key, builder in (
            ("setup", setup.build),
            ("diagnose", diagnose.build),
            ("scan", scan.build),
            ("batch", batch.build),
            ("security", security.build),
            ("profiles", profiles.build),
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
        self.goto_page(page or "diagnose")

        for i in range(len(NAV_PAGES)):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))

    # ------------------------------------------------------------------
    def bind_log_table(self, table: QTableWidget):
        """Log page registers its table as the live view."""
        self._log_table = table
        table.setRowCount(0)
        for row in self._log_rows[-2000:]:
            self._append_to_table(*row)
        table.scrollToBottom()

    def _log_row(self, direction, can_id, pdu, note, color=None):
        ts = time.time()
        self._log_rows.append((ts, direction, can_id, pdu, note, color))
        if len(self._log_rows) > 5000:
            del self._log_rows[: len(self._log_rows) - 5000]
        if self._log_table is not None:
            self._append_to_table(ts, direction, can_id, pdu, note, color)
            while self._log_table.rowCount() > 2000:
                self._log_table.removeRow(0)
            self._log_table.scrollToBottom()
        # One-line status: last traffic without a permanent OUTPUT panel
        if isinstance(can_id, int):
            plugin_shell.set_status(
                self, "%s 0x%X  %s" % (direction, can_id, (note or "")[:48]), 0)
        elif note:
            plugin_shell.set_status(self, str(note)[:64], 0)

    def _append_to_table(self, ts, direction, can_id, pdu, note, color=None):
        if self._log_table is None:
            return
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
            "TX": QColor(vscode_theme.TX),
            "RX": QColor(vscode_theme.RX),
            "ERR": QColor(vscode_theme.ERR),
            "FC": QColor(vscode_theme.FC),
        }
        c = colors.get(color or direction, QColor(vscode_theme.TEXT_MUTED))
        row = self._log_table.rowCount()
        self._log_table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, hex_str, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self._log_table.setItem(row, col, item)

    def clear_log(self):
        self._log_rows.clear()
        if self._log_table is not None:
            self._log_table.setRowCount(0)

    def export_log(self):
        rows = []
        for ts, direction, can_id, pdu, note, _color in self._log_rows:
            tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                    + ".%03d" % int(ts % 1 * 1000))
            if isinstance(pdu, (bytes, bytearray)):
                hex_str = " ".join("%02X" % b for b in pdu)
            else:
                hex_str = str(pdu)
            idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
            rows.append([tstr, direction, idstr, hex_str, note or ""])
        path = plugin_shell.export_csv(
            self, ["Time", "Dir", "CAN ID", "PDU", "Note"], rows, "uds_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    # ------------------------------------------------------------------
    def _on_nav(self, row: int):
        if row < 0:
            return
        self.stack.setCurrentIndex(row)
        plugin_shell.set_status(self, NAV_PAGES[row][1], 1200)
        self._persist()

    def goto_page(self, key: str):
        idx = self._page_index.get(key, 0)
        self.nav.setCurrentRow(idx)
        self.stack.setCurrentIndex(idx)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "tx_id": self.session.tx_id,
            "rx_id": self.session.rx_id,
            "func_id": self.session.func_id,
            "functional": self.session.functional,
            "tester_present": self.session.tester_present,
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "diagnose",
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            tx = int(saved.get("tx_id", self.session.tx_id))
            rx = int(saved.get("rx_id", self.session.rx_id))
            func = int(saved.get("func_id", self.session.func_id))
            self.session.functional = bool(saved.get("functional", False))
            self.session.tester_present = bool(saved.get("tester_present", False))
            self.session.apply_ids(tx, rx, func)
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
