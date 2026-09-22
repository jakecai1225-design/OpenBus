# -*- coding: utf-8 -*-
"""AppShell — EDS Studio (VS Code workbench chrome)."""

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
    ai_attach, codicons, plugin_shell, state_store, suite_chrome,
)
from _shared import edsparse

from document import EdsDocument

PLUGIN_ID = "eds-studio"
MAX_RECENT = 12

NAV_PAGES = [
    ("editor", "Editor"),
    ("library", "Library"),
    ("pdo", "PDO Map"),
    ("validate", "Validate"),
    ("compare", "Compare"),
    ("export", "Export"),
    ("timing", "Analysis"),
]


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("EDS Studio")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.document = EdsDocument()
        self._log_buffer: list = []
        self._pages = {}
        self._editor_api = None
        self._lint_before_save = True
        self._chrome_host: Optional[QWidget] = None
        self._recent: list = []

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="EDS Studio", panel_title="OUTPUT",
            panel_visible=False, sidebar_visible=True)
        self.stack = self._wb.stack

        self._init_document_controls()
        self._build_output_panel()

        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        from pages import (
            analysis, compare, editor, export, library, pdo_map, validate,
        )

        builders = [
            ("editor", editor.build),
            ("library", library.build),
            ("pdo", pdo_map.build),
            ("validate", validate.build),
            ("compare", compare.build),
            ("export", export.build),
            ("timing", analysis.build),
        ]
        for key, builder in builders:
            w = builder(self, self.document, self.log)
            self._pages[key] = w
            self.stack.addWidget(w)
            if key == "editor" and hasattr(w, "select_object"):
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
        plugin_shell.bind_shortcut(self, "Ctrl+O", self.open_eds)
        plugin_shell.bind_shortcut(self, "Ctrl+S", self.save_eds)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+S", self.save_eds_as)
        plugin_shell.bind_shortcut(self, "Ctrl+N", self.new_eds)
        plugin_shell.bind_shortcut(self, "Ctrl+Z", self.undo_edit)
        plugin_shell.bind_shortcut(self, "Ctrl+Y", self.redo_edit)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+Z", self.redo_edit)

        self.log("SYS", "EDS Studio ready")
        self._on_document_changed()

    def _init_document_controls(self):
        self.path_label = QLabel("(unsaved)")
        self.path_label.setObjectName("SuiteDocPath")
        self.path_label.setMinimumWidth(120)
        self.path_label.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self.path_label.setStyleSheet(
            "color:#546E7A;font-size:12px;padding:0 4px;")
        self.path_label.setToolTip("Active EDS/DCF path")

        self.dirty_label = QLabel("")
        self.dirty_label.setStyleSheet(
            "color:#C62828;font-size:11px;font-weight:600;padding:0 4px;")

        self._doc_btns = []
        specs = (
            ("", "Undo (Ctrl+Z)", "undo", self.undo_edit, False),
            ("", "Redo (Ctrl+Y)", "redo", self.redo_edit, False),
            ("", "Open EDS/DCF (Ctrl+O)", "browse", self.open_eds, False),
            ("Save", "Save (Ctrl+S)", "save", self.save_eds, True),
            ("", "Save As (Ctrl+Shift+S)", "file", self.save_eds_as, False),
            ("", "New empty document (Ctrl+N)", "add", self.new_eds, False),
            ("", "Open starter templates", "beaker", self.open_starters, False),
            ("", "Attach to AI Chat", "extensions", self.attach_ai, False),
            ("", "Import scanned OD", "device", self.import_scan, False),
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
            "Ask before saving when Validate reports errors")
        self.lint_gate_cb.toggled.connect(self._on_lint_gate_toggled)

    def _mount_chrome(self, page_key: str):
        host = QWidget()
        host.setObjectName("SuiteEditorTabHost")
        row = QHBoxLayout(host)
        row.setContentsMargins(10, 0, 6, 0)
        row.setSpacing(6)

        page = self._pages.get(page_key)
        tabs = getattr(page, "chrome_tabs", None) if page else None
        if tabs is not None:
            tabs.setParent(None)
            row.addWidget(tabs)
        else:
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
                active_plugin="eds-studio", active_page=key)
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
        codicons.set_button(export_btn, "export", size=12)
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("GhostButton")
        clear_btn.setFixedHeight(22)
        clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
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

    def log(self, source: str, message: str, color: str | None = None):
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
            self, ["Time", "Source", "Message"], rows, "eds_studio_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _elide_path(self, path: str) -> str:
        text = path or "(unsaved)"
        fm = QFontMetrics(self.path_label.font())
        width = max(180, self.path_label.width() or 320)
        if fm.horizontalAdvance(text) <= width:
            return text
        base = os.path.basename(text) if text != "(unsaved)" else text
        short = "…/" + base if base and base != text else text
        return fm.elidedText(short, Qt.TextElideMode.ElideLeft, width)

    def _on_document_changed(self):
        label = self.document.strip_label()
        self.path_label.setText(self._elide_path(label))
        self.path_label.setToolTip(label)
        self.dirty_label.setText("Modified" if self.document.dirty else "")
        title = "EDS Studio — %s" % self.document.display_name()
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

    def recent_files(self) -> list:
        return list(self._recent)

    def new_eds(self):
        if not self._confirm_discard():
            return
        box = QMessageBox(self)
        box.setWindowTitle("New document")
        box.setIcon(QMessageBox.Icon.Question)
        box.setText("Start empty, or pick a beginner starter?")
        empty_btn = box.addButton("Empty", QMessageBox.ButtonRole.AcceptRole)
        starter_btn = box.addButton(
            "Starters…", QMessageBox.ButtonRole.ActionRole)
        box.addButton(QMessageBox.StandardButton.Cancel)
        box.setDefaultButton(starter_btn)
        box.exec()
        clicked = box.clickedButton()
        if clicked is starter_btn:
            self.open_starters()
            return
        if clicked is not empty_btn:
            return
        self.document.new()
        self.log("SYS", "New empty EDS")
        plugin_shell.set_status(self, "New document", 2000)

    def open_starters(self):
        self.goto_page("library")
        page = self._pages.get("library")
        tabs = getattr(page, "chrome_tabs", None) if page else None
        if tabs is not None:
            tabs.setCurrentIndex(0)
        plugin_shell.set_status(
            self, "Pick a starter — double-click or Use starter", 3500)

    def open_eds(self):
        if not self._confirm_discard():
            return
        path, _ = QFileDialog.getOpenFileName(
            self, "Open EDS/DCF", self.document.path or "",
            "EDS/DCF (*.eds *.dcf);;EDS (*.eds);;DCF (*.dcf);;All (*)")
        if path:
            self._load_path(path)

    def _load_path(self, path: str) -> bool:
        try:
            self.document.load(path)
        except OSError as e:
            QMessageBox.warning(self, "EDS Studio", "Failed to open:\n%s" % e)
            self.log("ERR", "Open failed: %s" % e)
            return False
        self._remember_path(path)
        n = len(self.document.eds.entries)
        self.log("OK", "Opened %s (%d objects)" % (
            os.path.basename(path), n))
        plugin_shell.set_status(self, "Opened %s" % os.path.basename(path), 3000)
        return True

    def save_eds(self):
        if not self.document.path:
            return self.save_eds_as()
        if not self._warn_lint_before_save():
            return False
        try:
            self.document.save()
        except (OSError, ValueError) as e:
            QMessageBox.warning(self, "EDS Studio", "Save failed:\n%s" % e)
            self.log("ERR", "Save failed: %s" % e)
            return False
        self._remember_path(self.document.path)
        self.log("OK", "Saved %s" % self.document.path)
        plugin_shell.set_status(self, "Saved", 2500)
        self._notify_host_eds(self.document.path)
        return True

    def save_eds_as(self):
        path, selected = QFileDialog.getSaveFileName(
            self, "Save EDS/DCF As",
            self.document.path or self.document.display_name(),
            "EDS (*.eds);;DCF (*.dcf);;All (*)")
        if not path:
            return False
        as_dcf = path.lower().endswith(".dcf") or "DCF" in (selected or "")
        if not path.lower().endswith((".eds", ".dcf")):
            path += ".dcf" if as_dcf else ".eds"
        if not self._warn_lint_before_save():
            return False
        try:
            self.document.save(path, as_dcf=as_dcf)
        except (OSError, ValueError) as e:
            QMessageBox.warning(self, "EDS Studio", "Save failed:\n%s" % e)
            self.log("ERR", "Save failed: %s" % e)
            return False
        self._remember_path(path)
        self.log("OK", "Saved as %s" % path)
        plugin_shell.set_status(self, "Saved as %s" % os.path.basename(path), 3000)
        self._notify_host_eds(path)
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

    def attach_ai(self):
        path = self.document.path
        if not path:
            QMessageBox.information(
                self, "EDS Studio",
                "Save the document first, then Attach to AI Chat.")
            return
        try:
            ai_attach.attach_eds(path, summary=self.document.summary())
            self.log("OK", "Attached %s to AI Chat" % os.path.basename(path))
        except Exception as e:
            self.log("ERR", "AI Attach failed: %s" % e)

    def import_scan(self):
        """Import OD dump produced by canopen-suite scan (JSON or EDS path)."""
        path, _ = QFileDialog.getOpenFileName(
            self, "Import scanned OD", "",
            "EDS/JSON (*.eds *.json);;All (*)")
        if not path:
            return
        try:
            if path.lower().endswith(".json"):
                import json
                with open(path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                entries = []
                for row in data.get("entries") or data.get("objects") or []:
                    entries.append(edsparse.OdEntry(
                        index=int(row.get("index", 0)),
                        subindex=int(row.get("subindex", 0)),
                        name=str(row.get("name", "")),
                        object_type=str(row.get("object_type", "0x7")),
                        data_type=str(row.get("data_type", "")),
                        access_type=str(row.get("access_type", "")),
                        default_value=str(row.get("default_value", "")),
                        parameter_value=str(row.get("parameter_value", "")),
                    ))
                if not entries:
                    raise ValueError("No entries in JSON")
                self.document.upsert_entries(entries, merge=True)
            else:
                scanned = edsparse.parse_eds_file_document(path)
                self.document.upsert_entries(scanned.entries, merge=True)
            self.log("OK", "Imported scan into document")
            plugin_shell.set_status(self, "Scan imported", 3000)
        except Exception as e:
            QMessageBox.warning(self, "EDS Studio", "Import failed:\n%s" % e)
            self.log("ERR", "Scan import failed: %s" % e)

    def _notify_host_eds(self, path: str):
        if not path:
            return
        try:
            import sin
            sin.commands.execute("eds.reload", path)
            self.log("SYS", "Host EDS reload requested")
        except Exception:
            try:
                from sin._transport import send_notification
                send_notification("eds.reload", {"path": path})
            except Exception:
                pass
        try:
            from _shared import state_store as ss
            ss.save_state("canopen-suite", {
                "eds_path": path,
                "from_eds_studio": True,
            }, "eds_handoff.json")
        except Exception:
            pass

    def _on_lint_gate_toggled(self, checked: bool):
        self._lint_before_save = bool(checked)
        self._persist()

    def _warn_lint_before_save(self) -> bool:
        if not self._lint_before_save:
            return True
        try:
            findings = edsparse.validate_document(self.document.eds, deep=True)
            n_err = sum(1 for f in findings if f.get("severity") == "error"
                        or f.get("level") == "error")
            if n_err <= 0:
                return True
            box = QMessageBox(self)
            box.setIcon(QMessageBox.Icon.Warning)
            box.setWindowTitle("Validate before save")
            box.setText(
                "Document has %d error(s).\n"
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

    def goto_editor_object(self, index=None, subindex=0):
        self.goto_page("editor")
        api = self._pages.get("editor")
        if api is not None and hasattr(api, "select_object"):
            api.select_object(index, subindex)

    def goto_page(self, key: str):
        self._wb.goto_page(key)
        self._persist()

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        page = self._wb.current_page() if self._wb else "editor"
        state_store.save_state(PLUGIN_ID, {
            "last_path": self.document.path,
            "recent": list(self._recent),
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
