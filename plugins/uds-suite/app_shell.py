# -*- coding: utf-8 -*-
"""AppShell — UDS Suite (minimal vertical chrome).

Layout:
  Activity bar | [editor tabs OR page title + layout toggles]
               | editor body
               | OUTPUT (collapsible)

Diagnose owns the top tab strip (Session / Services / DID / …).
Other activity pages show a single title in that same chrome row.
"""

from __future__ import annotations

import time
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QCheckBox,
    QHBoxLayout,
    QMainWindow,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QWidget,
)

from _shared import plugin_shell, state_store, suite_chrome, vscode_theme, codicons

from session import SharedSession

PLUGIN_ID = "uds-suite"

NAV_PAGES = [
    ("diagnose", "Diagnose"),
    ("scan", "Scan"),
    ("batch", "Batch"),
    ("security", "Security"),
    ("profiles", "Profiles"),
    ("setup", "Setup"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("UDS Suite")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_rows: list = []
        self._log_table: Optional[QTableWidget] = None
        self._page_index = {key: i for i, (key, _) in enumerate(NAV_PAGES)}
        self._diagnose_tabs = None

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="UDS Suite", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True)
        self.stack = self._wb.stack

        self._build_output_panel()
        self.session.set_log_fn(self._log_row)

        from pages import setup, diagnose, scan, batch, security, profiles

        self._pages = {}
        for key, builder in (
            ("diagnose", diagnose.build),
            ("scan", scan.build),
            ("batch", batch.build),
            ("security", security.build),
            ("profiles", profiles.build),
            ("setup", setup.build),
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
            self._wb.expand_panel()
            page = "diagnose"
        self.goto_page(page or "diagnose")

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
            "Tabs: Session / Services / DID / … — Extended session then Send.")

    # ------------------------------------------------------------------
    def _on_workbench_page(self, key: str):
        """Swap the single chrome row between Diagnose tabs and a page title."""
        if key == "diagnose" and self._diagnose_tabs is not None:
            self._wb.set_editor_tabs(self._diagnose_tabs)
        else:
            self._wb.set_editor_title(dict(NAV_PAGES).get(key, key))
        try:
            from _shared import activity_snapshot
            activity_snapshot.update(
                active_plugin="uds-suite", active_page=key)
        except Exception:
            pass

    def _build_output_panel(self):
        # One chrome row: OUTPUT | tools … | collapse (list gets the rest)
        frames = QCheckBox("ISO-TP frames")
        frames.setToolTip("Include ISO-TP flow-control frames in the list")
        frames.toggled.connect(lambda c: setattr(self.session, "show_isotp_frames", c))
        self._wb.panel_tools.addWidget(frames)
        self._wb.panel_tools.addStretch(1)

        export_btn = QPushButton("Export")
        export_btn.setObjectName("GhostButton")
        export_btn.setFixedHeight(22)
        export_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        export_btn.setToolTip("Export log as CSV")
        codicons.set_button(export_btn, "export", size=12)
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("GhostButton")
        clear_btn.setFixedHeight(22)
        clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        clear_btn.setToolTip("Clear OUTPUT list")
        codicons.set_button(clear_btn, "clear", size=12)
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)

        table = suite_chrome.make_output_table()
        self._wb.panel_body.addWidget(table, 1)
        self.bind_log_table(table)
        export_btn.clicked.connect(self.export_log)
        clear_btn.clicked.connect(self.clear_log)

    # ------------------------------------------------------------------
    def bind_log_table(self, table: QTableWidget):
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
            "SYS": QColor(vscode_theme.TEXT_MUTED),
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
        sizes_v = self._wb.v_splitter.sizes()
        state_store.save_state(PLUGIN_ID, {
            "tx_id": self.session.tx_id,
            "rx_id": self.session.rx_id,
            "func_id": self.session.func_id,
            "functional": self.session.functional,
            "tester_present": self.session.tester_present,
            "nav_page": self._wb.current_page() or "diagnose",
            "geometry_hex": geo,
            "sidebar_visible": self._wb.is_sidebar_visible(),
            "panel_visible": self._wb.is_panel_visible(),
            "splitter_v": sizes_v,
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
            sv = saved.get("splitter_v")
            if isinstance(sv, list) and len(sv) == 2:
                self._wb.v_splitter.setSizes([int(sv[0]), int(sv[1])])
            # Always show activity + OUTPUT on launch (user can still hide)
            self._wb.set_sidebar_visible(True)
            self._wb.set_panel_visible(True)
        except (TypeError, ValueError):
            pass

    def closeEvent(self, event):
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
        self.session.shutdown()
