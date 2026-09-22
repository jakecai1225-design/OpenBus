# -*- coding: utf-8 -*-
"""AppShell — DBC Studio (VS Code workbench chrome).

Layout:
  Activity bar | [page title + document actions + layout toggles]
               | editor body
               | OUTPUT (collapsible)
"""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QFontMetrics
from PyQt6.QtWidgets import (
    QCheckBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSizePolicy,
    QTableWidget,
    QTableWidgetItem,
    QWidget,
)

from _shared import (
    codicons, dbc_picker, plugin_shell, state_store, suite_chrome, vscode_theme,
)

from document import DbcDocument

PLUGIN_ID = "dbc-studio"
MAX_RECENT = 12

NAV_PAGES = [
    ("editor", "Editor"),
    ("matrix", "Matrix"),
    ("valuetables", "Value Tables"),
    ("attributes", "Attributes"),
    ("validate", "Validate"),
    ("timing", "Timing"),
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
        self._favorites: list = []
        self._pages = {}
        self._editor_api = None
        self._lint_before_save = True
        self._chrome_host: Optional[QWidget] = None

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="DBC Studio", panel_title="OUTPUT",
            panel_visible=False, sidebar_visible=True)
        self.stack = self._wb.stack

        self._init_document_controls()
        self._build_output_panel()

        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        from pages import (
            attributes, compare, editor, export, library, matrix, merge,
            timing, validate, value_tables,
        )

        builders = [
            ("editor", editor.build),
            ("matrix", matrix.build),
            ("valuetables", value_tables.build),
            ("attributes", attributes.build),
            ("validate", validate.build),
            ("timing", timing.build),
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

        self.document.on_changed(self._on_document_changed)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        self.goto_page(page or "editor")

        suite_chrome.bind_nav_shortcuts(self, NAV_PAGES, self.goto_page)
        plugin_shell.bind_shortcut(
            self, "Ctrl+J",
            lambda: self._wb.set_panel_visible(not self._wb.is_panel_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+B",
            lambda: self._wb.set_sidebar_visible(
                not self._wb.is_sidebar_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+Shift+E",
            lambda: self._wb.set_maximized(not self._wb.is_maximized()))
        plugin_shell.bind_shortcut(self, "Ctrl+O", self.open_dbc)
        plugin_shell.bind_shortcut(self, "Ctrl+S", self.save_dbc)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+S", self.save_dbc_as)
        plugin_shell.bind_shortcut(self, "Ctrl+N", self.new_dbc)
        plugin_shell.bind_shortcut(self, "Ctrl+Z", self.undo_edit)
        plugin_shell.bind_shortcut(self, "Ctrl+Y", self.redo_edit)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+Z", self.redo_edit)

        self.log("SYS", "DBC Studio ready")
        self._on_document_changed()

    # ------------------------------------------------------------------
    def _init_document_controls(self):
        """Shared chrome pieces — remounted into the single editor chrome row."""
        self.path_label = QLabel("(unsaved)")
        self.path_label.setObjectName("SuiteDocPath")
        self.path_label.setMinimumWidth(120)
        self.path_label.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self.path_label.setStyleSheet(
            "color:#546E7A;font-size:12px;padding:0 4px;")
        self.path_label.setToolTip("Active DBC path")

        self.dirty_label = QLabel("")
        self.dirty_label.setStyleSheet(
            "color:#C62828;font-size:11px;font-weight:600;padding:0 4px;")

        self._doc_btns = []
        specs = (
            ("", "Undo (Ctrl+Z)", "undo", self.undo_edit, False),
            ("", "Redo (Ctrl+Y)", "redo", self.redo_edit, False),
            ("", "Open DBC (Ctrl+O)", "browse", self.open_dbc, False),
            ("Save", "Save (Ctrl+S)", "save", self.save_dbc, True),
            ("", "Save As (Ctrl+Shift+S)", "file", self.save_dbc_as, False),
            ("", "New document (Ctrl+N)", "add", self.new_dbc, False),
            ("", "Reload from disk", "refresh", self.reload_dbc, False),
            ("", "Open from workspace", "database", self.open_workspace_dbc, False),
        )
        for text, tip, icon, slot, primary in specs:
            btn = QPushButton(text)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setToolTip(tip)
            if text:
                btn.setFixedHeight(26)
            else:
                btn.setFixedSize(28, 26)
            if not primary:
                btn.setObjectName("GhostButton")
            codicons.set_button(btn, icon, size=12, primary=primary)
            btn.clicked.connect(slot)
            self._doc_btns.append(btn)

        self.lint_gate_cb = QCheckBox("Lint on save")
        self.lint_gate_cb.setChecked(True)
        self.lint_gate_cb.setToolTip(
            "Ask before saving when lint reports errors")
        self.lint_gate_cb.toggled.connect(self._on_lint_gate_toggled)

    def _mount_chrome(self, page_key: str):
        host = QWidget()
        host.setObjectName("SuiteEditorTabHost")
        row = QHBoxLayout(host)
        row.setContentsMargins(10, 0, 6, 0)
        row.setSpacing(6)

        title = QLabel(dict(NAV_PAGES).get(page_key, page_key))
        title.setObjectName("SuiteEditorTitle")
        row.addWidget(title)

        row.addWidget(self.path_label, 1)
        row.addWidget(self.dirty_label)
        for btn in self._doc_btns:
            row.addWidget(btn)
        row.addWidget(self.lint_gate_cb)
        self._chrome_host = host
        self._wb.set_editor_tabs(host)

    def _on_workbench_page(self, key: str):
        self._mount_chrome(key)
        try:
            from _shared import activity_snapshot
            activity_snapshot.update(
                active_plugin="dbc-studio", active_page=key)
        except Exception:
            pass

    def _build_output_panel(self):
        pause = QCheckBox("Pause")
        pause.setToolTip("Hold new log lines until unchecked")
        self.log_pause = pause
        self._wb.panel_tools.addWidget(pause)
        self._wb.panel_tools.addStretch(1)

        export_btn = QPushButton("Export")
        export_btn.setObjectName("GhostButton")
        export_btn.setFixedHeight(22)
        export_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        export_btn.setToolTip("Export OUTPUT as CSV")
        codicons.set_button(export_btn, "export", size=12)
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("GhostButton")
        clear_btn.setFixedHeight(22)
        clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        clear_btn.setToolTip("Clear OUTPUT")
        codicons.set_button(clear_btn, "clear", size=12)
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)

        from PyQt6.QtWidgets import QAbstractItemView, QHeaderView
        table = QTableWidget(0, 3)
        table.setObjectName("OutputTable")
        table.setHorizontalHeaderLabels(["Time", "Source", "Message"])
        table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        table.verticalHeader().setVisible(False)
        table.setShowGrid(False)
        table.setAlternatingRowColors(True)
        table.setSelectionBehavior(
            QAbstractItemView.SelectionBehavior.SelectRows)
        table.verticalHeader().setDefaultSectionSize(22)
        hdr = table.horizontalHeader()
        hdr.setHighlightSections(False)
        hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        hdr.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        hdr.setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        self.log_table = table
        self._wb.panel_body.addWidget(table, 1)

        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)

    # ------------------------------------------------------------------
    def log(self, source: str, message: str, color: str | None = None):
        # Always mirror to status bar — OUTPUT is collapsed by default.
        plugin_shell.set_status(
            self, "%s  %s" % (source, (message or "")[:72]), 4000)
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
        c = colors.get(color or source, QColor("#546E7A"))
        row = self.log_table.rowCount()
        self.log_table.insertRow(row)
        for col, text in enumerate((tstr, source, message or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self.log_table.setItem(row, col, item)
        self.log_table.scrollToBottom()

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
    def _elide_path(self, path: str) -> str:
        text = path or "(unsaved)"
        fm = QFontMetrics(self.path_label.font())
        width = max(180, self.path_label.width() or 320)
        if fm.horizontalAdvance(text) <= width:
            return text
        base = os.path.basename(text) if text != "(unsaved)" else text
        # Prefer showing the filename; elide the directory from the left
        short = "…/" + base if base and base != text else text
        return fm.elidedText(short, Qt.TextElideMode.ElideLeft, width)

    def _on_document_changed(self):
        label = self.document.strip_label()
        self.path_label.setText(self._elide_path(label))
        self.path_label.setToolTip(label)
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
        self._notify_host_dbc(self.document.path)
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
        self._notify_host_dbc(path)
        return True

    def undo_edit(self):
        if self.document.undo():
            self.log("SYS", "Undo")
            plugin_shell.set_status(self, "Undo", 2000)
        else:
            plugin_shell.set_status(self, "Nothing to undo", 1500)

    def redo_edit(self):
        if self.document.redo():
            self.log("SYS", "Redo")
            plugin_shell.set_status(self, "Redo", 2000)
        else:
            plugin_shell.set_status(self, "Nothing to redo", 1500)

    def _notify_host_dbc(self, path: str):
        """Ask host to reload this DBC so Trace / suites see the save."""
        if not path:
            return
        try:
            import sin
            sin.commands.execute("dbc.reload", path)
            self.log("SYS", "Host DBC reload requested")
        except Exception:
            try:
                from sin._transport import send_notification
                send_notification("dbc.reload", {"path": path})
            except Exception:
                pass

    def _on_lint_gate_toggled(self, checked: bool):
        self._lint_before_save = bool(checked)
        self._persist()

    def _warn_lint_before_save(self) -> bool:
        if not self._lint_before_save:
            return True
        try:
            from core.lint_engine import lint_dbc, load_rules
            findings = lint_dbc(self.document.db, load_rules())
            n_err = sum(1 for f in findings if f["severity"] == "error")
            if n_err <= 0:
                return True
            box = QMessageBox(self)
            box.setIcon(QMessageBox.Icon.Warning)
            box.setWindowTitle("Validate before save")
            box.setText(
                "Document has %d lint error(s).\n"
                "Review in Validate, or save anyway." % n_err)
            review_btn = box.addButton(
                "Review", QMessageBox.ButtonRole.ActionRole)
            save_btn = box.addButton(
                "Save anyway", QMessageBox.ButtonRole.AcceptRole)
            box.addButton(QMessageBox.StandardButton.Cancel)
            box.setDefaultButton(review_btn)
            box.exec()
            clicked = box.clickedButton()
            if clicked is review_btn:
                self.goto_page("validate")
                page = self._pages.get("validate")
                if page is not None and hasattr(page, "run_lint"):
                    page.run_lint()
                return False
            return clicked is save_btn
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
    def goto_page(self, key: str):
        self._wb.goto_page(key)
        self._persist()

    def recent_files(self) -> list:
        return list(self._recent)

    def favorite_files(self) -> list:
        return list(self._favorites)

    def add_favorite(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self._favorites = [
            p for p in self._favorites if os.path.normpath(p) != path]
        self._favorites.insert(0, path)
        self._favorites = self._favorites[:24]
        self._persist()

    def remove_favorite(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self._favorites = [
            p for p in self._favorites if os.path.normpath(p) != path]
        self._persist()

    def clear_recent(self):
        self._recent = []
        self._persist()

    # ------------------------------------------------------------------
    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        page = self._wb.current_page() if self._wb else "editor"
        state_store.save_state(PLUGIN_ID, {
            "last_path": self.document.path,
            "recent": list(self._recent),
            "favorites": list(self._favorites),
            "nav_page": page or "editor",
            "geometry_hex": geo,
            "lint_before_save": bool(self._lint_before_save),
            "sidebar": self._wb.is_sidebar_visible() if self._wb else True,
            "panel": False,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            self._recent = [p for p in (saved.get("recent") or []) if p][:MAX_RECENT]
            self._favorites = [
                p for p in (saved.get("favorites") or []) if p][:24]
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
            lint_gate = saved.get("lint_before_save")
            if lint_gate is not None:
                self._lint_before_save = bool(lint_gate)
                self.lint_gate_cb.blockSignals(True)
                self.lint_gate_cb.setChecked(self._lint_before_save)
                self.lint_gate_cb.blockSignals(False)
            if "sidebar" in saved:
                self._wb.set_sidebar_visible(bool(saved["sidebar"]))
            # OUTPUT stays collapsed by default — DBC Studio is file editing,
            # not live TX/RX. Use Ctrl+J when a log is needed.
            self._wb.set_panel_visible(False)
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
