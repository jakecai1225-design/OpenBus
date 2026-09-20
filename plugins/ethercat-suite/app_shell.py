# -*- coding: utf-8 -*-
"""AppShell — EtherCAT Suite.

Activity bar | one editor chrome row | body | OUTPUT.
Topology and Frames own editor tabs. No NIC master: ESI, PDO, CoE and captures.
"""

from __future__ import annotations

import time
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QMainWindow,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
)

from _shared import codicons, plugin_shell, state_store, suite_chrome, vscode_theme

from session import SharedSession

PLUGIN_ID = "ethercat-suite"

NAV_PAGES = [
    ("topology", "Topology"),
    ("pdo", "PDO"),
    ("coe", "CoE"),
    ("esi", "ESI"),
    ("dc", "DC"),
    ("frames", "Frames"),
    ("setup", "Setup"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("EtherCAT Suite")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_rows: list = []
        self._log_table: Optional[QTableWidget] = None
        self._page_index = {key: i for i, (key, _) in enumerate(NAV_PAGES)}
        self._net_tabs = None
        self._frame_tabs = None
        self._esi_tabs = None

        vscode_theme.apply(self)
        plugin_shell.attach_status_bar(self, "Ready")
        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="EtherCAT Suite", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True)
        self.stack = self._wb.stack
        self._build_output_panel()
        self.session.set_log_fn(self._log_row)

        from pages import coe, dc, esi, frames, network, pdo, setup

        self._pages = {}
        for key, builder in (
            ("topology", network.build),
            ("pdo", pdo.build),
            ("coe", coe.build),
            ("esi", esi.build),
            ("dc", dc.build),
            ("frames", frames.build),
            ("setup", setup.build),
        ):
            self._pages[key] = builder(self, self.session, self._log_row)
            self.stack.addWidget(self._pages[key])

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        if saved.get("session"):
            self.session.apply_state(saved.get("session") or {})
        else:
            self.session.load_sample()
        self._restore_state(saved)

        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page") or "topology"
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        if page == "log":
            page = "topology"
            self._wb.expand_panel()
        self.goto_page(page if page in self._page_index else "topology")
        self._wb.set_sidebar_visible(True)
        self._wb.set_panel_visible(True)
        suite_chrome.bind_nav_shortcuts(self, NAV_PAGES, self.goto_page)
        plugin_shell.bind_shortcut(
            self, "Ctrl+J",
            lambda: self._wb.set_panel_visible(not self._wb.is_panel_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+B",
            lambda: self._wb.set_sidebar_visible(not self._wb.is_sidebar_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+Shift+E",
            lambda: self._wb.set_maximized(not self._wb.is_maximized()))
        self._log_row(
            "SYS", "-", b"",
            "Topology / PDO / CoE / ESI / DC / Frames. Demo slave loaded. No NIC master.")

    def _on_workbench_page(self, key: str):
        if key == "topology" and self._net_tabs is not None:
            self._wb.set_editor_tabs(self._net_tabs)
        elif key == "frames" and self._frame_tabs is not None:
            self._wb.set_editor_tabs(self._frame_tabs)
        elif key == "esi" and getattr(self, "_esi_tabs", None) is not None:
            self._wb.set_editor_tabs(self._esi_tabs)
        else:
            self._wb.set_editor_title(dict(NAV_PAGES).get(key, key))

    def _build_output_panel(self):
        self._wb.panel_tools.addStretch(1)
        export_btn = QPushButton("Export")
        export_btn.setObjectName("GhostButton")
        export_btn.setFixedHeight(22)
        export_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        export_btn.setToolTip("Export OUTPUT as CSV")
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("GhostButton")
        clear_btn.setFixedHeight(22)
        clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        clear_btn.setToolTip("Clear OUTPUT")
        codicons.set_button(export_btn, "export", size=12)
        codicons.set_button(clear_btn, "clear", size=12)
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)
        table = suite_chrome.make_output_table()
        self._log_table = table
        self._wb.panel_body.addWidget(table, 1)
        export_btn.clicked.connect(self.export_log)
        clear_btn.clicked.connect(self.clear_log)

    def _log_row(self, direction, can_id, pdu, note, color=None):
        ts = time.time()
        self._log_rows.append((ts, direction, can_id, pdu, note, color))
        if len(self._log_rows) > 5000:
            self._log_rows = self._log_rows[-2000:]
        if self._log_table is None:
            return
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
        c = colors.get(color or direction, QColor("#333333"))
        row = self._log_table.rowCount()
        self._log_table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, hex_str, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self._log_table.setItem(row, col, item)
        while self._log_table.rowCount() > 2000:
            self._log_table.removeRow(0)
        self._log_table.scrollToBottom()

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
            self, ["Time", "Dir", "CAN ID", "PDU", "Note"], rows, "ethercat_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def goto_page(self, key: str):
        if key == "log":
            self._wb.expand_panel()
            return
        self._wb.goto_page(key)
        self._on_workbench_page(key)
        plugin_shell.set_status(self, dict(NAV_PAGES).get(key, key), 1200)
        self._persist()

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": self._wb.current_page() or "topology",
            "geometry_hex": geo,
            "session": self.session.to_state(),
        })

    def _restore_state(self, saved: dict):
        geo = (saved or {}).get("geometry_hex")
        if not geo:
            return
        from PyQt6.QtCore import QByteArray
        self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))

    def shutdown(self):
        self._persist()
