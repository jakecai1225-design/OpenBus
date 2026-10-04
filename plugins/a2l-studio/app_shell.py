# -*- coding: utf-8 -*-
"""AppShell — A2L Studio (Edit / Analyze / Deliver + Tabs + OUTPUT)."""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtGui import QColor, QKeySequence
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QFileDialog,
    QHeaderView,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QStackedWidget,
    QTableWidgetItem,
    QWidget,
)

from _shared import codicons, plugin_shell, state_store, suite_chrome, suite_tabs
from document import A2lDocumentSession
from pages import _ui

PLUGIN_ID = "a2l-studio"

NAV_PAGES = [
    ("edit", "Edit"),
    ("analyze", "Analyze"),
    ("deliver", "Deliver"),
]

FEATURE_ROUTE = {
    "objects": ("edit", 0),
    "validate": ("analyze", 0),
    "compare": ("analyze", 1),
    "export": ("deliver", 0),
    "edit": ("edit", 0),
    "analyze": ("analyze", 0),
    "deliver": ("deliver", 0),
}

FEATURE_TITLES = {
    "objects": "Objects",
    "validate": "Check",
    "compare": "Compare",
    "export": "Export",
}

_WORKSPACE_DEFAULT = {
    "edit": "objects",
    "analyze": "validate",
    "deliver": "export",
}

_PAGE_ALIASES = {
    "editor": "objects",
    "check": "validate",
    "lint": "validate",
    "diff": "compare",
}

_ACTIVITY_KEYS = frozenset(k for k, _ in NAV_PAGES)
_LEAF_KEYS = frozenset(FEATURE_TITLES.keys())


def _is_leaf_feature(feature: str) -> bool:
    return feature in _LEAF_KEYS and feature in FEATURE_ROUTE


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("A2L Studio")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)
        self._context = context
        self.session = A2lDocumentSession()
        self.document = self.session  # alias
        self._log_buffer: list = []
        self._pages = {}
        self._workspace_stacks: dict = {}
        self._workspace_features: dict = {}
        self._workspace_stack_index: dict[str, int] = {}
        self._sidebars: dict = {}
        self._active_feature = "objects"
        self._open_tabs: list[str] = []
        self._tab_bar = None
        self._next_action = ("", "", {})
        self._status_chrome_mounted = False
        self._recent: list = []

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
            self, NAV_PAGES, title="A2L Studio", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=200)
        _ui.apply_a2l_chrome(self)
        self.stack = self._wb.stack

        self._init_status_controls()
        self._build_menubar()
        self._build_log_panel()

        from pages import compare, export, objects, validate, workspace_sidebar

        self._pages["objects"] = objects.build(self, self.session, self._log_row)
        self._pages["validate"] = validate.build(self, self.session, self._log_row)
        self._pages["compare"] = compare.build(self, self.session, self._log_row)
        self._pages["export"] = export.build(self, self.session, self._log_row)

        self._sidebars["edit"] = workspace_sidebar.build_edit_sidebar(self)
        self._sidebars["analyze"] = workspace_sidebar.build_analyze_sidebar(self)
        self._sidebars["deliver"] = workspace_sidebar.build_deliver_sidebar(self)

        self._add_workspace("edit", [("objects", self._pages["objects"])])
        self._add_workspace("analyze", [
            ("validate", self._pages["validate"]),
            ("compare", self._pages["compare"]),
        ])
        self._add_workspace("deliver", [("export", self._pages["export"])])

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        self._recent = list(saved.get("recent") or [])[:12]
        handoff = state_store.load_state(PLUGIN_ID, "a2l_handoff.json") or {}
        if not handoff.get("a2l_path"):
            handoff = state_store.load_state("xcp-studio", "a2l_handoff.json") or {}
        if handoff.get("open_in_a2l") and handoff.get("a2l_path"):
            self.session.load_path(str(handoff["a2l_path"]))
            try:
                state_store.clear_state(PLUGIN_ID, "a2l_handoff.json")
                state_store.clear_state("xcp-studio", "a2l_handoff.json")
            except Exception:
                pass

        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        page = _PAGE_ALIASES.get(page or "", page or "objects")
        if page in _ACTIVITY_KEYS and page not in _LEAF_KEYS:
            page = _WORKSPACE_DEFAULT.get(page, "objects")
        if not _is_leaf_feature(page):
            page = "objects"
        self._switch_activity(FEATURE_ROUTE[page][0])
        self.goto_page(page)

        self._wb.set_sidebar_visible(True)
        self._wb.set_panel_visible(True)
        suite_chrome.bind_nav_shortcuts(
            self, NAV_PAGES, self._on_activity_clicked)
        plugin_shell.bind_shortcut(
            self, "Ctrl+J",
            lambda: self._wb.set_panel_visible(not self._wb.is_panel_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+B",
            lambda: self._wb.set_sidebar_visible(
                not self._wb.is_sidebar_visible()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+O", lambda: self.run_action("a2l.open"))
        plugin_shell.bind_shortcut(
            self, "Ctrl+S", lambda: self.run_action("a2l.save"))
        plugin_shell.bind_shortcut(
            self, "Ctrl+N", lambda: self.run_action("a2l.new"))

        self._log_row("SYS", "-", b"", "A2L Studio ready — Open ASAP2, Check, Apply → XCP.")
        self._ensure_status_chrome()
        self._sync_path_label()
        self._sync_next_hint()
        self.session.on_changed(
            lambda: (self._sync_path_label(), self._sync_next_hint()))

    def _init_status_controls(self):
        self.path_label = QLabel("")
        self.path_label.setObjectName("SuiteDocPath")
        self.path_label.setMinimumWidth(120)
        self.path_label.setMaximumWidth(320)
        self.next_btn = _ui.ghost_btn("Next", "Suggested next step", "arrow-right")
        self.next_btn.setMaximumWidth(160)
        self.next_btn.clicked.connect(self._run_next_hint)

    def _ensure_status_chrome(self):
        bar = self.statusBar()
        if bar is None:
            return
        widgets = (self.path_label, self.next_btn)
        if self._status_chrome_mounted:
            for w in widgets:
                if w.parent() is not bar:
                    bar.addPermanentWidget(w)
            return
        self._status_chrome_mounted = True
        for w in widgets:
            w.setParent(bar)
            bar.addPermanentWidget(w)

    def _sync_path_label(self):
        dirty = "● " if self.session.dirty else ""
        p = self.session.path
        text = ("%s%s" % (dirty, os.path.basename(p))) if p else (
            "%sUntitled" % dirty if self.session.doc.symbols else "No A2L")
        self.path_label.setText(text)
        self.path_label.setToolTip(p or "No file")

    def _mount_tab_bar(self):
        suite_tabs.mount_editor_tabs(
            self,
            open_tabs=self._open_tabs,
            active_feature=self._active_feature,
            titles=FEATURE_TITLES,
            on_activate=self._activate_feature,
            on_close=self._close_feature_tab,
            on_reorder=lambda order: setattr(self, "_open_tabs", order),
            ensure_status=self._ensure_status_chrome,
        )

    def _open_feature_tab(self, feature: str, *, activate: bool = True):
        if not _is_leaf_feature(feature):
            return
        if feature not in self._open_tabs:
            self._open_tabs.append(feature)
        if activate:
            self._active_feature = feature
        self._mount_tab_bar()
        self._activate_feature(feature)

    def _close_feature_tab(self, feature: str):
        if feature in self._open_tabs:
            self._open_tabs.remove(feature)
        if not self._open_tabs:
            default = _WORKSPACE_DEFAULT.get(
                FEATURE_ROUTE.get(feature, ("edit",))[0], "objects")
            self._open_feature_tab(default, activate=True)
            return
        nxt = (
            self._active_feature
            if self._active_feature in self._open_tabs
            else self._open_tabs[-1])
        self._mount_tab_bar()
        self._activate_feature(nxt)

    def _build_menubar(self):
        bar = suite_chrome.begin_suite_menubar(self)

        def _act(menu, label, slot, shortcut=None):
            a = menu.addAction(label)
            a.triggered.connect(slot)
            if shortcut:
                a.setShortcut(QKeySequence(shortcut))
            return a

        m_file = bar.addMenu("&File")
        _act(m_file, "&New", lambda: self.run_action("a2l.new"), "Ctrl+N")
        _act(m_file, "&Open…", lambda: self.run_action("a2l.open"), "Ctrl+O")
        _act(m_file, "&Save", lambda: self.run_action("a2l.save"), "Ctrl+S")
        _act(m_file, "Save &As…", lambda: self.run_action("a2l.save_as"))
        m_file.addSeparator()
        _act(m_file, "Apply → &XCP Studio",
             lambda: self.run_action("a2l.apply_xcp"))
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

        m_edit = bar.addMenu("&Edit")
        _act(m_edit, "&Check", lambda: self.run_action("a2l.goto", page="validate"))

        m_view = bar.addMenu("&View")
        for key, title in NAV_PAGES:
            _act(m_view, title,
                 lambda _c=False, k=key: self._on_activity_clicked(k))
        m_view.addSeparator()
        _act(m_view, "Toggle &Side Bar",
             lambda: self._wb.set_sidebar_visible(
                 not self._wb.is_sidebar_visible()), "Ctrl+B")
        _act(m_view, "Toggle &OUTPUT",
             lambda: self._wb.set_panel_visible(
                 not self._wb.is_panel_visible()), "Ctrl+J")

        m_help = bar.addMenu("&Help")
        _act(m_help, "&About A2L Studio", self._menu_about)
        self._menubar_trailing = suite_chrome.attach_layout_toggles_to_menubar(
            self, self._wb)

    def _menu_about(self):
        QMessageBox.information(
            self, "About A2L Studio",
            "A2L Studio — ASAP2 Symbol Explorer.\n"
            "Top2: Vector CANape · ETAS INCA (file side).\n"
            "Live M&C: XCP Studio.")

    def run_action(self, action: str, **kwargs):
        if action == "a2l.goto":
            self.goto_page(str(kwargs.get("page") or "objects"))
        elif action == "a2l.new":
            self.session.new_empty()
            self.goto_page("objects")
        elif action == "a2l.open":
            path, _ = QFileDialog.getOpenFileName(
                self, "Open A2L", "",
                "ASAP2 (*.a2l *.A2L);;All (*.*)")
            if path and self.session.load_path(path):
                self._push_recent(path)
                self._log_row("SYS", "-", b"", "Opened %s" % path)
                self.goto_page("objects")
        elif action == "a2l.save":
            if not self.session.path:
                return self.run_action("a2l.save_as")
            if self.session.save():
                self._log_row("SYS", "-", b"", "Saved %s" % self.session.path)
        elif action == "a2l.save_as":
            path, _ = QFileDialog.getSaveFileName(
                self, "Save A2L", self.session.path or "untitled.a2l",
                "ASAP2 (*.a2l);;All (*.*)")
            if path and self.session.save(path):
                self._push_recent(path)
                self._log_row("SYS", "-", b"", "Saved %s" % path)
        elif action == "a2l.apply_xcp":
            self._apply_xcp()
        else:
            plugin_shell.set_status(self, "Unknown action: %s" % action, 3000)
            return
        self._sync_next_hint()
        self._persist()

    def _apply_xcp(self):
        if not self.session.path:
            if not self.session.save():
                self.run_action("a2l.save_as")
            if not self.session.path:
                return
        payload = self.session.handoff_payload()
        state_store.save_state("xcp-studio", payload, "a2l_handoff.json")
        state_store.save_state(
            "xcp-studio", {"start_page": "setup"}, "goto.json")
        self._log_row("SYS", "-", b"", "Apply → XCP Studio (%s)" % self.session.path)
        plugin_shell.set_status(self, "Handoff saved — open XCP Studio", 5000)
        try:
            import sin
            sin.commands.execute("xcpStudio.open")
        except Exception:
            pass

    def _push_recent(self, path: str):
        path = os.path.normpath(path)
        self._recent = [p for p in self._recent if p != path]
        self._recent.insert(0, path)
        self._recent = self._recent[:12]

    def _run_next_hint(self):
        _lab, action, kw = self._next_action
        if action:
            self.run_action(action, **(kw or {}))
            if action not in ("a2l.goto",):
                self.session.advance_next_hint()

    def _sync_next_hint(self):
        label, action, kw = self.session.next_hint()
        self._next_action = (label, action, kw or {})
        self.next_btn.setText(label or "Next")
        self.next_btn.setEnabled(bool(action))

    def _add_workspace(self, workspace: str, features: list):
        self._workspace_stack_index[workspace] = self.stack.count()
        self.stack.addWidget(self._build_feature_workspace(workspace, features))

    def _build_feature_workspace(self, workspace: str, features: list):
        stack = QStackedWidget()
        stack.setObjectName("SuiteEditorStack")
        fmap = {}
        for i, (key, page) in enumerate(features):
            stack.addWidget(page)
            fmap[key] = i
        self._workspace_stacks[workspace] = stack
        self._workspace_features[workspace] = [k for k, _ in features]
        stack._feature_index = fmap  # type: ignore[attr-defined]

        def apply(key: str):
            stack.setCurrentIndex(fmap.get(key, 0))

        stack._apply_feature = apply  # type: ignore[attr-defined]
        return stack

    def _switch_activity(self, key: str):
        if key not in dict(NAV_PAGES):
            return
        wi = self._workspace_stack_index.get(key)
        if wi is not None:
            self.stack.setCurrentIndex(wi)
        sb = self._sidebars.get(key)
        if sb is not None and self._wb.set_side_bar_widget:
            self._wb.set_side_bar_widget(sb)
        try:
            self._wb.highlight_activity(key)
        except Exception:
            pass
        if self._open_tabs:
            self._mount_tab_bar()

    def _on_activity_clicked(self, key: str):
        self._switch_activity(key)
        self._open_feature_tab(_WORKSPACE_DEFAULT.get(key, "objects"), activate=True)
        self._sync_next_hint()
        self._persist()

    def _on_workbench_page(self, key: str):
        self._on_activity_clicked(key)

    def _activate_feature(self, feature: str):
        if not _is_leaf_feature(feature):
            return
        workspace, _ = FEATURE_ROUTE[feature]
        self._active_feature = feature
        wi = self._workspace_stack_index.get(workspace)
        if wi is not None and self.stack.currentIndex() != wi:
            self.stack.setCurrentIndex(wi)
        outer = self._workspace_stacks.get(workspace)
        if outer is not None:
            apply = getattr(outer, "_apply_feature", None)
            if callable(apply):
                apply(feature)
        if feature not in self._open_tabs:
            self._open_tabs.append(feature)
        self._mount_tab_bar()
        sb = self._sidebars.get(workspace)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                sb.select_section(feature)
            except Exception:
                pass
        try:
            self._wb.highlight_activity(workspace)
        except Exception:
            pass

    def goto_page(self, key: str):
        key = _PAGE_ALIASES.get(key, key)
        if key in _ACTIVITY_KEYS and key not in _LEAF_KEYS:
            key = _WORKSPACE_DEFAULT.get(key, "objects")
        if not _is_leaf_feature(key):
            key = "objects"
        self._open_feature_tab(key, activate=True)
        self._sync_next_hint()
        self._persist()

    def goto_objects_target(self, kind=None, name=None):
        if kind and name:
            self.session.set_focus(kind, name)
        self.goto_page("objects")
        page = self._pages.get("objects")
        if page is not None and hasattr(page, "refresh"):
            page.refresh()

    def _build_log_panel(self):
        self.log_pause = QCheckBox("Pause")
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
        self.log_table.horizontalHeader().setSectionResizeMode(
            4, QHeaderView.ResizeMode.Stretch)
        self._wb.panel_body.addWidget(self.log_table, 1)
        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)

    def _log_row(self, direction, can_id, pdu, note, color=None):
        if self.log_pause.isChecked():
            return
        self._append_log_row(time.time(), direction, can_id, pdu, note, color)

    def log(self, direction, note):
        self._log_row(direction, "-", b"", note)

    def _append_log_row(self, ts, direction, can_id, pdu, note, color=None):
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        hex_str = (
            " ".join("%02X" % b for b in pdu[:48])
            if isinstance(pdu, (bytes, bytearray)) else str(pdu))
        idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
        colors = {
            "TX": QColor("#1565C0"), "RX": QColor("#2E7D32"),
            "ERR": QColor("#C62828"), "SYS": QColor("#6A1B9A"), "OK": QColor("#2E7D32"),
        }
        c = colors.get(color or direction, QColor("#333"))
        row = self.log_table.rowCount()
        self.log_table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, hex_str, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self.log_table.setItem(row, col, item)
        self.log_table.verticalScrollBar().setValue(
            self.log_table.verticalScrollBar().maximum())

    def clear_log(self):
        self.log_table.setRowCount(0)

    def _export_log(self):
        rows = []
        for r in range(self.log_table.rowCount()):
            rows.append([
                self.log_table.item(r, c).text() if self.log_table.item(r, c) else ""
                for c in range(5)])
        path = plugin_shell.export_csv(
            self, ["Time", "Dir", "CAN ID", "Data", "Note"], rows,
            "a2l_studio_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": self._active_feature or "objects",
            "geometry_hex": geo,
            "open_tabs": list(self._open_tabs),
            "recent": list(self._recent),
            "a2l_path": self.session.path,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        geo = saved.get("geometry_hex")
        if geo:
            from PyQt6.QtCore import QByteArray
            self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))
        self._open_tabs = suite_tabs.normalize_open_tabs(
            saved.get("open_tabs"),
            titles=FEATURE_TITLES,
            routes=FEATURE_ROUTE,
            activity_keys=_ACTIVITY_KEYS,
            aliases=_PAGE_ALIASES,
        )
        path = saved.get("a2l_path")
        if path and os.path.isfile(path):
            self.session.load_path(path)

    def shutdown(self):
        self._persist()
