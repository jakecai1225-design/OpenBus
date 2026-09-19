# -*- coding: utf-8 -*-
"""AppShell — CANopen Suite main window: nav + strip + pages + log."""

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
    QListWidget,
    QListWidgetItem,
    QMainWindow,
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

from session import SharedSession, NMT_START, NMT_STOP, NMT_RESET_NODE

PLUGIN_ID = "canopen-suite"

NAV_PAGES = [
    ("network", "Network"),
    ("monitor", "Monitor"),
    ("object_dict", "OD"),
    ("pdo", "PDO"),
    ("profiles", "Drive"),
    ("eds_editor", "EDS"),
    ("log", "Log"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("CANopen Suite")
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
            eds_editor, log_page, monitor, network, object_dict, pdo, profiles,
        )

        self._pages = {}
        builders = [
            ("network", network.build),
            ("monitor", monitor.build),
            ("object_dict", object_dict.build),
            ("pdo", pdo.build),
            ("profiles", profiles.build),
            ("eds_editor", eds_editor.build),
            ("log", log_page.build),
        ]
        for key, builder in builders:
            w = builder(self, self.session, self._log_row)
            self._pages[key] = w
            self.stack.addWidget(w)

        self.nav.currentRowChanged.connect(self._on_nav)
        self.session.on_node_changed(self._sync_strip_from_session)
        self.session.on_od_changed(self._sync_eds_label)

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        self.goto_page(page or "network")

        for i in range(len(NAV_PAGES)):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))

        self._log_row(
            "RX", "-", b"",
            "CANopen Suite ready — Network / Monitor / OD / PDO / Drive / EDS / Log")

    # ------------------------------------------------------------------
    def _build_session_strip(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("SuiteToolbar")
        row = QHBoxLayout(bar)
        row.setContentsMargins(12, 6, 12, 6)
        row.setSpacing(8)

        self.node_spin = QSpinBox()
        self.node_spin.setRange(1, 127)
        self.node_spin.setValue(1)
        self.node_spin.setToolTip("Selected Node-ID")
        row.addWidget(QLabel("Node-ID:"))
        row.addWidget(self.node_spin)

        apply_btn = QPushButton("Apply")
        apply_btn.setToolTip("Apply Node-ID to shared session")
        apply_btn.clicked.connect(self._on_apply_node)
        row.addWidget(apply_btn)

        sep = QLabel("|")
        sep.setStyleSheet("color:#E5E5E5;")
        row.addWidget(sep)

        self.eds_label = QLabel("(no EDS)")
        self.eds_label.setStyleSheet("font-weight:bold;")
        self.eds_label.setMinimumWidth(200)
        row.addWidget(QLabel("EDS:"))
        row.addWidget(self.eds_label, 1)

        browse_btn = QPushButton("Browse…")
        browse_btn.clicked.connect(self._browse_eds)
        clear_eds_btn = QPushButton("Clear EDS")
        clear_eds_btn.clicked.connect(self._clear_eds)
        row.addWidget(browse_btn)
        row.addWidget(clear_eds_btn)

        sep2 = QLabel("|")
        sep2.setStyleSheet("color:#E5E5E5;")
        row.addWidget(sep2)

        for text, cmd in (
            ("NMT Start", NMT_START),
            ("NMT Stop", NMT_STOP),
            ("NMT Reset", NMT_RESET_NODE),
        ):
            b = QPushButton(text)
            b.clicked.connect(lambda _=False, c=cmd: self.session.send_nmt(c))
            row.addWidget(b)

        self.bitrate_label = QLabel(self.session.bitrate_hint)
        self.bitrate_label.setStyleSheet("color:#666;")
        row.addWidget(self.bitrate_label)
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
    def _on_apply_node(self):
        self.session.set_node_id(self.node_spin.value())
        plugin_shell.set_status(self, "Node-ID applied", 2500)
        self._persist()

    def _browse_eds(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Open EDS/DCF",
            self.session.eds_path or "",
            "EDS/DCF (*.eds *.dcf);;All files (*.*)")
        if path:
            if self.session.load_eds(path):
                self._sync_eds_label()
                self._persist()
                plugin_shell.set_status(self, "EDS loaded", 3000)

    def _clear_eds(self):
        self.session.clear_eds()
        self._sync_eds_label()
        self._persist()

    def _sync_strip_from_session(self):
        self.node_spin.blockSignals(True)
        self.node_spin.setValue(self.session.node_id)
        self.node_spin.blockSignals(False)

    def _sync_eds_label(self):
        path = self.session.eds_path
        if path:
            self.eds_label.setText(os.path.basename(path))
            self.eds_label.setToolTip(path)
        else:
            n = len(self.session.od_entries)
            self.eds_label.setText(
                "(no EDS%s)" % (", %d draft objs" % n if n else ""))
            self.eds_label.setToolTip("")

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
            "canopen_suite_log.csv",
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
            "node_id": self.node_spin.value(),
            "eds_path": self.session.eds_path,
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "network",
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            if "node_id" in saved:
                nid = int(saved["node_id"])
                self.node_spin.setValue(nid)
                self.session.set_node_id(nid)
            eds = saved.get("eds_path") or ""
            if eds and os.path.isfile(eds):
                self.session.load_eds(eds)
            self._sync_eds_label()
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
