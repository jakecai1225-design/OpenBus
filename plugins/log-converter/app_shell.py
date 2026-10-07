# -*- coding: utf-8 -*-
"""AppShell — Log Converter (one activity icon + one Convert page)."""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtGui import QColor, QKeySequence
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QHeaderView,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QTableWidgetItem,
    QWidget,
)

from _shared import codicons, plugin_shell, state_store, suite_chrome
from pages import _ui
from session import SharedSession

PLUGIN_ID = "log-converter"

# Activity key MUST match a codicon stem (export.svg). Title is user-facing.
NAV_PAGES = [
    ("export", "Convert"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("Log Converter")
        self.setMinimumSize(960, 640)
        self.resize(1040, 720)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_buffer: list = []
        self._pages = {}
        self._status_chrome_mounted = False

        try:
            codicons.clear_pixmap_cache()
        except Exception:
            pass

        _icon_svg = os.path.join(os.path.dirname(__file__), "icon.svg")
        if os.path.isfile(_icon_svg):
            try:
                self.setWindowIcon(codicons.window_icon_from_svg(_icon_svg))
            except Exception:
                pass

        plugin_shell.attach_status_bar(self, "Ready")
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="Log Converter", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=False,
            side_bar_enabled=False, side_bar_visible=False,
            lock_activity=True)
        _ui.apply_converter_chrome(self)
        self.stack = self._wb.stack
        self._wb.set_editor_title("Convert")

        self._init_status_controls()
        self._build_menubar()
        self._build_log_panel()
        self.session.set_log_fn(self._log_row)

        from pages import home
        self._pages["export"] = home.build(self, self.session, self._log_row)
        self.stack.addWidget(self._pages["export"])

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)

        self._wb.set_panel_visible(True)
        suite_chrome.bind_nav_shortcuts(
            self, NAV_PAGES, self._on_activity_clicked)
        plugin_shell.bind_shortcut(
            self, "Ctrl+J",
            lambda: self._wb.set_panel_visible(not self._wb.is_panel_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+O", lambda: self.run_action("log.convert.browse"))

        try:
            self._wb.highlight_activity("export")
        except Exception:
            pass

        self._log_row(
            "SYS", "-", b"",
            "Log Converter ready — BLF / ASC / CSV / PCAP / TRC")
        self._ensure_status_chrome()
        # start_page accepted for API compat (single page)
        _ = start_page

    def _init_status_controls(self):
        self.fmt_label = QLabel("BLF ASC CSV PCAP TRC")
        self.fmt_label.setObjectName("SuiteDocPath")
        self.fmt_label.setMinimumWidth(140)
        self.fmt_label.setMaximumWidth(280)

    def _ensure_status_chrome(self):
        bar = self.statusBar()
        if bar is None:
            return
        if self._status_chrome_mounted:
            if self.fmt_label.parent() is not bar:
                bar.addPermanentWidget(self.fmt_label)
            return
        self._status_chrome_mounted = True
        self.fmt_label.setParent(bar)
        bar.addPermanentWidget(self.fmt_label)

    def _on_activity_clicked(self, key: str):
        try:
            self._wb.highlight_activity(key)
        except Exception:
            pass
        self._wb.set_editor_title("Convert")

    def _build_menubar(self):
        bar = suite_chrome.begin_suite_menubar(self)

        def _act(menu, label, slot, shortcut=None):
            a = menu.addAction(label)
            a.triggered.connect(slot)
            if shortcut:
                a.setShortcut(QKeySequence(shortcut))
            return a

        m_file = bar.addMenu("&File")
        _act(m_file, "&Add logs…",
             lambda: self.run_action("log.convert.browse"), "Ctrl+O")
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

        m_view = bar.addMenu("&View")
        _act(m_view, "Toggle &OUTPUT",
             lambda: self._wb.set_panel_visible(
                 not self._wb.is_panel_visible()), "Ctrl+J")
        _act(m_view, "&Maximize Editor",
             lambda: self._wb.set_maximized(
                 not self._wb.is_maximized()), "Ctrl+Shift+E")

        m_help = bar.addMenu("&Help")
        _act(m_help, "&About Log Converter", self._menu_about)

        suite_chrome.attach_layout_toggles_to_menubar(self, self._wb)

    def _menu_about(self):
        QMessageBox.information(
            self, "About Log Converter",
            "Log Converter — CAN / CAN FD log format conversion.\n\n"
            "Formats: BLF · ASC · CSV · PCAP/PCAPNG · TRC\n"
            "Add one or many files, choose target format, Convert.\n"
            "Uses the OpenBus host CanFileIO engine (async + cancel).")

    def run_action(self, action: str, **kwargs):
        if action in ("log.convert.browse", "log.goto"):
            from PyQt6.QtWidgets import QFileDialog
            from formats import OPEN_FILTER
            paths, _ = QFileDialog.getOpenFileNames(
                self, "Add CAN logs", self.session.start_dir(), OPEN_FILTER)
            page = self._pages.get("export")
            if paths and page is not None:
                if hasattr(page, "add_paths"):
                    page.add_paths(paths)
                elif hasattr(page, "set_source") and len(paths) == 1:
                    page.set_source(paths[0])
            plugin_shell.set_status(self, "Add logs", 2000)
            return
        plugin_shell.set_status(self, "Unknown action: %s" % action, 3000)

    def goto_page(self, key: str):
        self._on_activity_clicked("export")

    def rerun_convert(self, source: str, fmt: str = ""):
        page = self._pages.get("export")
        if page is not None and hasattr(page, "set_source") and source:
            page.set_source(source)
        if fmt:
            self.session.last_fmt = fmt
        plugin_shell.set_status(self, "Ready — press Convert", 3000)

    def notify_jobs_changed(self):
        pass

    def _build_log_panel(self):
        self.log_pause = QCheckBox("Pause")
        self.log_pause.setToolTip("Hold new rows until unchecked")
        self._wb.panel_tools.addWidget(self.log_pause)
        self._wb.panel_tools.addStretch(1)
        export_btn = QPushButton("Export")
        export_btn.setObjectName("GhostButton")
        export_btn.setFixedHeight(22)
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("GhostButton")
        clear_btn.setFixedHeight(22)
        codicons.set_button(export_btn, "export", size=12)
        codicons.set_button(clear_btn, "clear", size=12)
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)
        self.log_table = suite_chrome.make_output_table()
        self.log_table.setHorizontalHeaderLabels(
            ["Time", "Dir", "CAN ID", "Data", "Note"])
        self.log_table.setEditTriggers(
            QAbstractItemView.EditTrigger.NoEditTriggers)
        self.log_table.verticalHeader().setVisible(False)
        self.log_table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        self.log_table.horizontalHeader().setSectionResizeMode(
            4, QHeaderView.ResizeMode.Stretch)
        self._wb.panel_body.addWidget(self.log_table, 1)
        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)

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
        for col, text in enumerate(
                (tstr, direction, idstr, hex_str, note or "")):
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
                self.log_table.item(r, c).text()
                if self.log_table.item(r, c) else ""
                for c in range(5)
            ])
        path = plugin_shell.export_csv(
            self, ["Time", "Dir", "CAN ID", "Data", "Note"], rows,
            "log_converter_output.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": "export",
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
