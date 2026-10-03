# -*- coding: utf-8 -*-
"""AppShell — OBD Suite (VS Code chrome: Activity + Side Bar + Tabs + OUTPUT)."""

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
    QMenuBar,
    QMessageBox,
    QPushButton,
    QStackedWidget,
    QTableWidgetItem,
    QWidget,
)

from _shared import codicons, plugin_shell, state_store, suite_chrome, suite_tabs
from pages import _ui
from session import SharedSession

PLUGIN_ID = "obd-suite"

NAV_PAGES = [
    ("scanner", "Scanner"),
    ("readiness", "Readiness"),
    ("setup", "Setup"),
]

FEATURE_ROUTE = {
    "scanner": ("scanner", 0),
    "readiness": ("readiness", 0),
    "setup": ("setup", 0),
}

FEATURE_TITLES = {
    "scanner": "Scanner",
    "readiness": "Readiness",
    "setup": "Setup",
}

_WORKSPACE_DEFAULT = {
    "scanner": "scanner",
    "readiness": "readiness",
    "setup": "setup",
}

_PAGE_ALIASES = {
    "log": "scanner",
    "scan": "scanner",
    "monitors": "readiness",
    "ids": "setup",
}

_ACTIVITY_KEYS = frozenset(k for k, _ in NAV_PAGES)
_LEAF_KEYS = frozenset(FEATURE_TITLES.keys())


def _is_leaf_feature(feature: str) -> bool:
    return feature in _LEAF_KEYS and feature in FEATURE_ROUTE


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("OBD Suite")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_buffer: list = []
        self._pages = {}
        self._workspace_stacks: dict = {}
        self._workspace_features: dict = {}
        self._workspace_stack_index: dict[str, int] = {}
        self._sidebars: dict = {}
        self._active_feature = "scanner"
        self._open_tabs: list[str] = []
        self._tab_bar = None
        self._next_action = ("", "", {})
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
            self, NAV_PAGES, title="OBD Suite", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=200)
        _ui.apply_obd_chrome(self)
        self.stack = self._wb.stack

        self._init_status_controls()
        self._build_menubar()
        self._build_log_panel()
        self.session.set_log_fn(self._log_row)

        from pages import readiness, scanner, setup, workspace_sidebar

        self._pages["scanner"] = scanner.build(self, self.session, self._log_row)
        self._pages["readiness"] = readiness.build(
            self, self.session, self._log_row)
        self._pages["setup"] = setup.build(self, self.session, self._log_row)

        self._sidebars["scanner"] = workspace_sidebar.build_scanner_sidebar(self)
        self._sidebars["readiness"] = workspace_sidebar.build_readiness_sidebar(
            self)
        self._sidebars["setup"] = workspace_sidebar.build_setup_sidebar(self)

        self._add_workspace("scanner", [("scanner", self._pages["scanner"])])
        self._add_workspace(
            "readiness", [("readiness", self._pages["readiness"])])
        self._add_workspace("setup", [("setup", self._pages["setup"])])

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        settings = state_store.load_state(PLUGIN_ID, "settings.json") or {}
        if settings.get("tx_id") and settings.get("rx_id"):
            self.session.apply_ids(
                int(settings["tx_id"]), int(settings["rx_id"]))

        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        if page == "log":
            self._wb.expand_panel()
            page = "scanner"
        page = _PAGE_ALIASES.get(page or "", page or "scanner")
        if page in _ACTIVITY_KEYS and page not in _LEAF_KEYS:
            page = _WORKSPACE_DEFAULT.get(page, "scanner")
        if not _is_leaf_feature(page):
            page = "scanner"
        activity = FEATURE_ROUTE[page][0]
        self._switch_activity(activity)
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

        self._log_row(
            "SYS", "-", b"",
            "OBD Suite ready — Set IDs on Setup, then Scanner.")
        self._ensure_status_chrome()
        self._sync_conn_label()
        self._sync_next_hint()
        self.session.on_ids_changed(
            lambda *_a: (self._sync_conn_label(), self._sync_next_hint()))

    def _init_status_controls(self):
        self.conn_label = QLabel("")
        self.conn_label.setObjectName("SuiteDocPath")
        self.conn_label.setMinimumWidth(120)
        self.conn_label.setMaximumWidth(280)
        self.conn_label.setToolTip("Shared request / response IDs")
        self.next_btn = _ui.ghost_btn(
            "Next", "Suggested next step", "arrow-right")
        self.next_btn.setMaximumWidth(160)
        self.next_btn.clicked.connect(self._run_next_hint)

    def _ensure_status_chrome(self):
        bar = self.statusBar()
        if bar is None:
            return
        widgets = (self.conn_label, self.next_btn)
        if self._status_chrome_mounted:
            for w in widgets:
                if w.parent() is not bar:
                    bar.addPermanentWidget(w)
            return
        self._status_chrome_mounted = True
        for w in widgets:
            w.setParent(bar)
            bar.addPermanentWidget(w)

    def _sync_conn_label(self):
        s = self.session
        text = "Req 0x%X / Rsp 0x%X" % (s.tx_id, s.rx_id)
        self.conn_label.setText(text)
        self.conn_label.setToolTip(text)

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
                FEATURE_ROUTE.get(feature, ("scanner",))[0], "scanner")
            self._open_feature_tab(default, activate=True)
            return
        nxt = (
            self._active_feature
            if self._active_feature in self._open_tabs
            else self._open_tabs[-1])
        self._mount_tab_bar()
        self._activate_feature(nxt)

    def _build_menubar(self):
        """VS Code text menubar — File | Edit | View | Help (+ Run)."""
        bar = suite_chrome.begin_suite_menubar(self)

        def _act(menu, label, slot, shortcut=None):
            a = menu.addAction(label)
            a.triggered.connect(slot)
            if shortcut:
                a.setShortcut(QKeySequence(shortcut))
            return a

        m_file = bar.addMenu("&File")
        _act(m_file, "Export live &CSV…",
             lambda: self.run_action("obd.export"))
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

        m_edit = bar.addMenu("&Edit")
        _act(m_edit, "Apply IDs from &Setup",
             lambda: self.run_action("obd.goto", page="setup"))
        _act(m_edit, "&Discover PIDs",
             lambda: self.run_action("obd.discover"))

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
        _act(m_view, "&Maximize Editor",
             lambda: self._wb.set_maximized(
                 not self._wb.is_maximized()), "Ctrl+Shift+E")

        m_help = bar.addMenu("&Help")
        _act(m_help, "&About OBD Suite", self._menu_about)

        self._menubar_trailing = suite_chrome.attach_layout_toggles_to_menubar(
            self, self._wb)

    def _menu_about(self):
        QMessageBox.information(
            self, "About OBD Suite",
            "OBD Suite — Scanner, Readiness, Setup.\n"
            "ISO 15031-5 / SAE J1979 over ISO-TP.")

    def run_action(self, action: str, **kwargs):
        label = action
        if action == "obd.goto":
            page = kwargs.get("page") or "scanner"
            self.goto_page(str(page))
            label = "Go %s" % page
        elif action == "obd.discover":
            self.goto_page("scanner")
            page = self._pages.get("scanner")
            if page is not None and hasattr(page, "do_discover"):
                page.do_discover()
            self.session.note_scan()
            label = "Discover PIDs"
        elif action == "obd.export":
            self.goto_page("scanner")
            page = self._pages.get("scanner")
            if page is not None and hasattr(page, "do_export"):
                page.do_export()
            label = "Export CSV"
        elif action == "obd.read_monitors":
            self.goto_page("readiness")
            page = self._pages.get("readiness")
            if page is not None and hasattr(page, "do_read"):
                page.do_read()
            self.session.note_readiness()
            label = "Read monitors"
        elif action == "obd.goto_pid":
            self.goto_pid_target(
                int(kwargs.get("pid") or 0),
                int(kwargs.get("mode") or 1))
            label = "Go PID"
        elif action == "obd.goto_readiness":
            self.goto_readiness_target()
            label = "Go Readiness"
        else:
            plugin_shell.set_status(self, "Unknown action: %s" % action, 3000)
            return
        if action not in ("obd.goto", "obd.goto_pid", "obd.goto_readiness"):
            self.session.advance_next_hint()
        self._sync_next_hint()
        plugin_shell.set_status(self, label, 2000)

    def goto_pid_target(self, pid: int, mode: int = 1):
        """Open Scanner and highlight PID (interop §13.2)."""
        self.session.set_focus(mode=int(mode or 1), pid=int(pid or 0))
        self.goto_page("scanner")
        page = self._pages.get("scanner")
        if page is not None and hasattr(page, "select_pid"):
            page.select_pid(int(pid or 0))

    def goto_readiness_target(self):
        self.session.set_focus(mode=1, pid=0x01)
        self.goto_page("readiness")
        page = self._pages.get("readiness")
        if page is not None and hasattr(page, "do_read"):
            pass
        self.session.note_readiness()

    def _run_next_hint(self):
        _lab, action, kw = self._next_action
        if action:
            self.run_action(action, **(kw or {}))

    def _sync_next_hint(self):
        btn = getattr(self, "next_btn", None)
        if btn is None:
            return
        label, action, kw = self.session.next_hint()
        self._next_action = (label, action, kw or {})
        btn.setText(label or "Next")
        btn.setEnabled(bool(action))
        btn.setToolTip("Next: %s" % (label or "(none)"))

    def _add_workspace(
            self, workspace: str,
            features: list[tuple[str, QWidget]]) -> None:
        self._workspace_stack_index[workspace] = self.stack.count()
        self.stack.addWidget(self._build_feature_workspace(workspace, features))

    def _build_feature_workspace(
            self, workspace: str,
            features: list[tuple[str, QWidget]]) -> QStackedWidget:
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
        default = _WORKSPACE_DEFAULT.get(key, "scanner")
        self._open_feature_tab(default, activate=True)
        self._sync_next_hint()
        self._persist()

    def _on_workbench_page(self, key: str):
        self._on_activity_clicked(key)

    def _activate_feature(self, feature: str):
        if not _is_leaf_feature(feature):
            return
        workspace, _ = FEATURE_ROUTE[feature]
        self._active_feature = feature
        if feature == "scanner":
            self.session.note_scan()
        elif feature == "readiness":
            self.session.note_readiness()
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
            key = _WORKSPACE_DEFAULT.get(key, "scanner")
        if not _is_leaf_feature(key):
            key = "scanner"
        self._open_feature_tab(key, activate=True)
        self._sync_next_hint()
        self._persist()

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
        for col, text in enumerate((tstr, direction, idstr, hex_str, note or "")):
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
                self.log_table.item(r, c).text() if self.log_table.item(r, c) else ""
                for c in range(5)
            ])
        path = plugin_shell.export_csv(
            self, ["Time", "Dir", "CAN ID", "Data", "Note"], rows,
            "obd_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": self._active_feature or "scanner",
            "geometry_hex": geo,
            "open_tabs": list(self._open_tabs),
            "sidebar_visible": self._wb.is_sidebar_visible(),
            "panel_visible": self._wb.is_panel_visible(),
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

    def shutdown(self):
        self._persist()
        self.session.shutdown()
