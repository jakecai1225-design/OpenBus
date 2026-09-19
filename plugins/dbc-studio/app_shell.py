# -*- coding: utf-8 -*-
"""AppShell — DBC Studio main window: nav + document strip + pages + log."""

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
    QMessageBox,
    QPushButton,
    QSplitter,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import dbc_picker, plugin_shell, state_store, vscode_theme, codicons

from document import DbcDocument

PLUGIN_ID = "dbc-studio"
MAX_RECENT = 12

NAV_PAGES = [
    ("editor", "Editor"),
    ("validate", "Validate"),
    ("compare", "Compare"),
    ("merge", "Merge"),
    ("export", "Export"),
    ("library", "Library"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("DBC Studio")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.document = DbcDocument()
        self._log_buffer: list = []
        self._page_index = {key: i for i, (key, _) in enumerate(NAV_PAGES)}
        self._recent: list = []
        self._pages = {}
        self._editor_api = None

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

        right_l.addWidget(self._build_document_strip())

        splitter = QSplitter(Qt.Orientation.Vertical)
        self.stack = QStackedWidget()
        splitter.addWidget(self.stack)

        log_widget = self._build_log_panel()
        splitter.addWidget(log_widget)
        splitter.setSizes([520, 200])
        right_l.addWidget(splitter, 1)
        root.addWidget(right, 1)

        from pages import compare, editor, export, library, merge, validate

        builders = [
            ("editor", editor.build),
            ("validate", validate.build),
            ("compare", compare.build),
            ("merge", merge.build),
            ("export", export.build),
            ("library", library.build),
        ]
        for key, builder in builders:
            w = builder(self, self.document, self.log)
            self._pages[key] = w
            self.stack.addWidget(w)
            if key == "editor" and hasattr(w, "select_target"):
                self._editor_api = w

        self.nav.currentRowChanged.connect(self._on_nav)
        self.document.on_changed(self._on_document_changed)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        self.goto_page(page or "editor")

        for i in range(6):
            plugin_shell.bind_shortcut(
                self, "Ctrl+%d" % (i + 1),
                lambda _=False, idx=i: self.goto_page(NAV_PAGES[idx][0]))
        plugin_shell.bind_shortcut(self, "Ctrl+O", self.open_dbc)
        plugin_shell.bind_shortcut(self, "Ctrl+S", self.save_dbc)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+S", self.save_dbc_as)
        plugin_shell.bind_shortcut(self, "Ctrl+N", self.new_dbc)

        self.log("SYS", "DBC Studio ready")
        self._on_document_changed()

    # ------------------------------------------------------------------
    def _build_document_strip(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("SuiteToolbar")
        row = QHBoxLayout(bar)
        row.setContentsMargins(12, 6, 12, 6)
        row.setSpacing(8)
        _t = QLabel("DOCUMENT")
        _t.setObjectName("SuiteToolbarTitle")
        row.addWidget(_t)

        self.path_label = QLabel("(unsaved)")
        self.path_label.setStyleSheet("font-weight:bold;")
        self.path_label.setMinimumWidth(280)
        self.dirty_label = QLabel("")
        self.dirty_label.setStyleSheet("color:#c62828;font-weight:bold;")

        row.addWidget(QLabel("DBC:"))
        row.addWidget(self.path_label, 1)
        row.addWidget(self.dirty_label)

        for text, slot in (
            ("Open…", self.open_dbc),
            ("Save", self.save_dbc),
            ("Save As…", self.save_dbc_as),
            ("New", self.new_dbc),
            ("Reload", self.reload_dbc),
            ("Workspace…", self.open_workspace_dbc),
        ):
            btn = QPushButton(text)
            btn.clicked.connect(slot)
            row.addWidget(btn)
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

    def _append_log_row(self, ts, source, message, color=None):
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        colors = {
            "ERR": QColor("#C62828"),
            "WARN": QColor("#EF6C00"),
            "OK": QColor("#2E7D32"),
            "SYS": QColor("#1565C0"),
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
            self, ["Time", "Source", "Message"], rows, "dbc_studio_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    # ------------------------------------------------------------------
    def _on_document_changed(self):
        self.path_label.setText(self.document.strip_label())
        self.dirty_label.setText("Modified" if self.document.dirty else "")
        title = "DBC Studio — %s" % self.document.display_name()
        if self.document.dirty:
            title += " *"
        self.setWindowTitle(title)
        self._persist()

    def _remember_path(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self._recent = [p for p in self._recent if os.path.normpath(p) != path]
        self._recent.insert(0, path)
        self._recent = self._recent[:MAX_RECENT]
        self._persist()

    def new_dbc(self):
        if not self._confirm_discard():
            return
        self.document.new()
        self.log("SYS", "New empty DBC")
        plugin_shell.set_status(self, "New document", 2000)

    def open_dbc(self):
        if not self._confirm_discard():
            return
        path, _ = QFileDialog.getOpenFileName(
            self, "Open DBC", self.document.path or "",
            "DBC (*.dbc);;All files (*)")
        if path:
            self._load_path(path)

    def open_workspace_dbc(self):
        if not self._confirm_discard():
            return
        path = dbc_picker.pick_dbc(self, "Select workspace DBC")
        if path:
            self._load_path(path)

    def _load_path(self, path: str) -> bool:
        try:
            self.document.load(path)
        except OSError as e:
            QMessageBox.warning(self, "DBC Studio", "Failed to open:\n%s" % e)
            self.log("ERR", "Open failed: %s" % e)
            return False
        self._remember_path(path)
        n = len(self.document.db.messages)
        self.log("OK", "Opened %s (%d messages)" % (os.path.basename(path), n))
        plugin_shell.set_status(self, "Opened %s" % os.path.basename(path), 3000)
        return True

    def reload_dbc(self):
        if not self.document.path:
            QMessageBox.information(self, "DBC Studio", "No file path to reload")
            return
        if self.document.dirty:
            ans = QMessageBox.question(
                self, "Reload",
                "Discard unsaved changes and reload from disk?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            if ans != QMessageBox.StandardButton.Yes:
                return
        self._load_path(self.document.path)

    def save_dbc(self):
        if not self.document.path:
            return self.save_dbc_as()
        if not self._warn_lint_before_save():
            return False
        try:
            self.document.save()
        except (OSError, ValueError) as e:
            QMessageBox.warning(self, "DBC Studio", "Save failed:\n%s" % e)
            self.log("ERR", "Save failed: %s" % e)
            return False
        self._remember_path(self.document.path)
        self.log("OK", "Saved %s" % self.document.path)
        plugin_shell.set_status(self, "Saved", 2500)
        return True

    def save_dbc_as(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Save DBC As",
            self.document.path or self.document.display_name(),
            "DBC (*.dbc);;All files (*)")
        if not path:
            return False
        if not path.lower().endswith(".dbc"):
            path += ".dbc"
        if not self._warn_lint_before_save():
            return False
        try:
            self.document.save(path)
        except (OSError, ValueError) as e:
            QMessageBox.warning(self, "DBC Studio", "Save failed:\n%s" % e)
            self.log("ERR", "Save failed: %s" % e)
            return False
        self._remember_path(path)
        self.log("OK", "Saved as %s" % path)
        plugin_shell.set_status(self, "Saved as %s" % os.path.basename(path), 3000)
        return True

    def _warn_lint_before_save(self) -> bool:
        try:
            from core.lint_engine import lint_dbc, load_rules
            findings = lint_dbc(self.document.db, load_rules())
            n_err = sum(1 for f in findings if f["severity"] == "error")
            if n_err <= 0:
                return True
            ans = QMessageBox.question(
                self, "Validate before save",
                "Document has %d lint error(s).\nSave anyway?" % n_err,
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No)
            return ans == QMessageBox.StandardButton.Yes
        except Exception:
            return True

    def _confirm_discard(self) -> bool:
        if not self.document.dirty:
            return True
        ans = QMessageBox.question(
            self, "Unsaved changes",
            "Document has unsaved changes. Discard?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        return ans == QMessageBox.StandardButton.Yes

    def goto_editor_target(self, can_id=None, signal=None):
        self.goto_page("editor")
        api = self._pages.get("editor")
        if api is not None and hasattr(api, "select_target"):
            api.select_target(can_id, signal)

    # ------------------------------------------------------------------
    def _on_nav(self, row: int):
        if row < 0:
            return
        self.stack.setCurrentIndex(row)
        self._persist()

    def goto_page(self, key: str):
        idx = self._page_index.get(key, 0)
        self.nav.setCurrentRow(idx)
        self.stack.setCurrentIndex(idx)

    def recent_files(self) -> list:
        return list(self._recent)

    # ------------------------------------------------------------------
    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "last_path": self.document.path,
            "recent": list(self._recent),
            "nav_page": NAV_PAGES[self.nav.currentRow()][0]
            if self.nav.currentRow() >= 0 else "editor",
            "geometry_hex": geo,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            self._recent = [p for p in (saved.get("recent") or []) if p][:MAX_RECENT]
            geo = saved.get("geometry_hex")
            if geo:
                from PyQt6.QtCore import QByteArray
                self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))
            last = saved.get("last_path") or ""
            if last and os.path.isfile(last):
                try:
                    self.document.load(last)
                    self.log("SYS", "Restored %s" % os.path.basename(last))
                except OSError:
                    pass
        except (TypeError, ValueError):
            pass

    def closeEvent(self, event):
        if self.document.dirty:
            ans = QMessageBox.question(
                self, "Unsaved changes",
                "Document has unsaved changes. Close anyway?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            if ans != QMessageBox.StandardButton.Yes:
                event.ignore()
                return
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
