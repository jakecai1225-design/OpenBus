# -*- coding: utf-8 -*-
"""AppShell — Log Analysis main window: nav + path strip + pages + log."""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtCore import Qt, QSize
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QFileDialog,
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

from _shared import plugin_shell, state_store, vscode_theme, codicons

from session import SharedSession

PLUGIN_ID = "log-analysis"

NAV_PAGES = [
    ("toolkit", "Open"),
    ("trace", "Trace"),
    ("compare", "Compare"),
    ("trigger", "Trigger"),
    ("quality", "Quality"),
    ("reverse", "Reverse"),
    ("id_scan", "ID Scan"),
    ("report", "Report"),
    ("log", "Log"),
]


def _short_path(path: str, maxlen: int = 42) -> str:
    if not path:
        return "(none)"
    if len(path) <= maxlen:
        return path
    return "…" + path[-(maxlen - 1):]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("Log Analysis")
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

        right_l.addWidget(self._build_path_strip())

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
            compare, id_scan, log_page, quality, report, reverse, toolkit, trace, trigger,
        )

        self._pages = {}
        builders = [
            ("toolkit", toolkit.build),
            ("trace", trace.build),
            ("compare", compare.build),
            ("trigger", trigger.build),
            ("quality", quality.build),
            ("reverse", reverse.build),
            ("id_scan", id_scan.build),
            ("report", report.build),
            ("log", log_page.build),
        ]
        for key, builder in builders:
            w = builder(self, self.session, self._log_row)
            self._pages[key] = w
            self.stack.addWidget(w)

        self.nav.currentRowChanged.connect(self._on_nav)
        self.session.on_paths_changed(self._sync_path_labels)

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        self.goto_page(page or "toolkit")

        for i in range(len(NAV_PAGES)):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))

        self._log_row(
            "RX", "-", b"",
            "Log Analysis ready — Toolkit / Compare / Trigger / Quality / "
            "Reverse / ID Scan / Log")

    # ------------------------------------------------------------------
    def _build_path_strip(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("SuiteToolbar")
        row = QHBoxLayout(bar)
        row.setContentsMargins(12, 6, 12, 6)
        row.setSpacing(8)
        _t = QLabel("PATHS")
        _t.setObjectName("SuiteToolbarTitle")
        row.addWidget(_t)

        self.folder_edit = QLineEdit()
        self.folder_edit.setPlaceholderText("Working folder for open/save dialogs")
        self.folder_edit.setMinimumWidth(180)
        row.addWidget(QLabel("Folder:"))
        row.addWidget(self.folder_edit, 1)

        browse_btn = QPushButton("Browse…")
        browse_btn.clicked.connect(self._browse_folder)
        apply_btn = QPushButton("Apply")
        apply_btn.clicked.connect(self._apply_folder)
        clear_btn = QPushButton("Clear")
        clear_btn.clicked.connect(self._clear_folder)
        row.addWidget(browse_btn)
        row.addWidget(apply_btn)
        row.addWidget(clear_btn)

        sep = QLabel("|")
        sep.setStyleSheet("color:#E5E5E5;")
        row.addWidget(sep)

        self.last_log_label = QLabel("Log: (none)")
        self.last_dbc_label = QLabel("DBC: (none)")
        self.last_export_label = QLabel("Export: (none)")
        for lab in (self.last_log_label, self.last_dbc_label, self.last_export_label):
            lab.setStyleSheet("color:#555;")
            lab.setToolTip("")
            row.addWidget(lab)

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

    # ------------------------------------------------------------------
    def _browse_folder(self):
        start = self.session.start_dir(self.folder_edit.text().strip())
        path = QFileDialog.getExistingDirectory(
            self, "Select working folder", start)
        if path:
            self.folder_edit.setText(path)
            self.session.set_working_folder(path)
            self._persist()

    def _apply_folder(self):
        self.session.set_working_folder(self.folder_edit.text().strip())
        plugin_shell.set_status(self, "Working folder applied", 2000)
        self._persist()

    def _clear_folder(self):
        self.folder_edit.clear()
        self.session.set_working_folder("")
        self._persist()

    def _sync_path_labels(self):
        folder = self.session.working_folder
        if self.folder_edit.text().strip() != folder:
            self.folder_edit.blockSignals(True)
            self.folder_edit.setText(folder)
            self.folder_edit.blockSignals(False)
        self.last_log_label.setText(
            "Log: %s" % _short_path(os.path.basename(self.session.last_log_path)
                                    if self.session.last_log_path else ""))
        self.last_log_label.setToolTip(self.session.last_log_path or "")
        self.last_dbc_label.setText(
            "DBC: %s" % _short_path(os.path.basename(self.session.last_dbc_path)
                                    if self.session.last_dbc_path else ""))
        self.last_dbc_label.setToolTip(self.session.last_dbc_path or "")
        self.last_export_label.setText(
            "Export: %s" % _short_path(
                os.path.basename(self.session.last_export_path)
                if self.session.last_export_path else ""))
        self.last_export_label.setToolTip(self.session.last_export_path or "")
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
            ["Time", "Dir", "CAN ID", "Data", "Note"],
            rows,
            "log_analysis_activity.csv",
        )
        if path:
            self.session.note_export_path(path)
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    # ------------------------------------------------------------------
    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "working_folder": self.session.working_folder,
            "last_log_path": self.session.last_log_path,
            "last_dbc_path": self.session.last_dbc_path,
            "last_export_path": self.session.last_export_path,
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "toolkit",
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            folder = saved.get("working_folder") or ""
            if folder and os.path.isdir(folder):
                self.session.working_folder = folder
                self.folder_edit.setText(folder)
            if saved.get("last_log_path"):
                self.session.last_log_path = str(saved["last_log_path"])
            if saved.get("last_dbc_path"):
                self.session.last_dbc_path = str(saved["last_dbc_path"])
            if saved.get("last_export_path"):
                self.session.last_export_path = str(saved["last_export_path"])
            self._sync_path_labels()
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
