# -*- coding: utf-8 -*-
"""AppShell — EtherCAT Suite (VS Code chrome: Activity + Side Bar + Tabs + OUTPUT)."""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QKeySequence
from PyQt6.QtWidgets import (
    QMainWindow,
    QMenuBar,
    QMessageBox,
    QPushButton,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
)

from _shared import (
    codicons, plugin_shell, state_store, suite_chrome, suite_tabs, vscode_theme,
)
from pages import _ui
from session import SharedSession

PLUGIN_ID = "ethercat-suite"

NAV_PAGES = [
    ("network", "Network"),
    ("objects", "Objects"),
    ("timing", "Timing"),
    ("setup", "Setup"),
]

FEATURE_ROUTE = {
    "topology": ("network", 0),
    "frames": ("network", 1),
    "pdo": ("objects", 0),
    "coe": ("objects", 1),
    "esi": ("objects", 2),
    "dc": ("timing", 0),
    "setup": ("setup", 0),
}

FEATURE_TITLES = {
    "topology": "Topology",
    "frames": "Frames",
    "pdo": "PDO",
    "coe": "CoE",
    "esi": "ESI",
    "dc": "DC",
    "setup": "Setup",
}

_WORKSPACE_DEFAULT = {
    "network": "topology",
    "objects": "pdo",
    "timing": "dc",
    "setup": "setup",
}

_PAGE_ALIASES = {
    "log": "topology",
    "al": "topology",
    "state": "topology",
    "datagrams": "frames",
    "mailbox": "frames",
}

_ACTIVITY_KEYS = frozenset(k for k, _ in NAV_PAGES)
_LEAF_KEYS = frozenset(FEATURE_TITLES.keys())


def _is_leaf_feature(feature: str) -> bool:
    return feature in _LEAF_KEYS and feature in FEATURE_ROUTE


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
        self._pages = {}
        self._workspace_stacks: dict = {}
        self._workspace_features: dict = {}
        self._workspace_stack_index: dict[str, int] = {}
        self._sidebars: dict = {}
        self._active_feature = "topology"
        self._open_tabs: list[str] = []
        self._tab_bar = None

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

        vscode_theme.apply(self)
        plugin_shell.attach_status_bar(self, "Ready")
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="EtherCAT Suite", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=200)
        _ui.apply_ethercat_chrome(self)
        self.stack = self._wb.stack

        self._build_menubar()
        self._build_output_panel()
        self.session.set_log_fn(self._log_row)

        from pages import coe, dc, esi, frames, network, pdo, setup, workspace_sidebar

        self._pages["topology"] = network.build(self, self.session, self._log_row)
        self._pages["pdo"] = pdo.build(self, self.session, self._log_row)
        self._pages["coe"] = coe.build(self, self.session, self._log_row)
        self._pages["esi"] = esi.build(self, self.session, self._log_row)
        self._pages["dc"] = dc.build(self, self.session, self._log_row)
        self._pages["frames"] = frames.build(self, self.session, self._log_row)
        self._pages["setup"] = setup.build(self, self.session, self._log_row)

        self._sidebars["network"] = workspace_sidebar.build_network_sidebar(self)
        self._sidebars["objects"] = workspace_sidebar.build_objects_sidebar(self)
        self._sidebars["timing"] = workspace_sidebar.build_timing_sidebar(self)
        self._sidebars["setup"] = workspace_sidebar.build_setup_sidebar(self)

        self._add_workspace("network", [
            ("topology", self._pages["topology"]),
            ("frames", self._pages["frames"]),
        ])
        self._add_workspace("objects", [
            ("pdo", self._pages["pdo"]),
            ("coe", self._pages["coe"]),
            ("esi", self._pages["esi"]),
        ])
        self._add_workspace("timing", [("dc", self._pages["dc"])])
        self._add_workspace("setup", [("setup", self._pages["setup"])])

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        if saved.get("session"):
            self.session.apply_state(saved.get("session") or {})
        else:
            self.session.load_sample()
        self._restore_state(saved)

        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        if page == "log":
            self._wb.expand_panel()
            page = "topology"
        page = _PAGE_ALIASES.get(page or "", page or "topology")
        if page in _ACTIVITY_KEYS and page not in _LEAF_KEYS:
            page = _WORKSPACE_DEFAULT.get(page, "topology")
        if not _is_leaf_feature(page):
            page = "topology"
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
            self, "Ctrl+Shift+E",
            lambda: self._wb.set_maximized(not self._wb.is_maximized()))

        self._log_row(
            "SYS", "-", b"",
            "Network / Objects / Timing / Setup. Demo slave loaded. No NIC master.")

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
        _act(m_file, "Load &demo slave",
             lambda: self.run_action("ethercat.demo"))
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

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
        _act(m_help, "&About EtherCAT Suite", self._menu_about)

        self._menubar_trailing = suite_chrome.attach_layout_toggles_to_menubar(
            self, self._wb)

    def _menu_about(self):
        QMessageBox.information(
            self, "About EtherCAT Suite",
            "EtherCAT Suite — Network, Objects, Timing, Setup.\n"
            "Offline model and capture decode (no NIC master).")

    def run_action(self, action: str, **kwargs):
        label = action
        if action == "ethercat.goto":
            page = kwargs.get("page") or "topology"
            self.goto_page(str(page))
            label = "Go %s" % page
        elif action == "ethercat.demo":
            self.session.load_sample()
            self.goto_page("topology")
            label = "Load demo"
        else:
            plugin_shell.set_status(self, "Unknown action: %s" % action, 3000)
            return
        plugin_shell.set_status(self, label, 2000)

    def _mount_tab_bar(self):
        suite_tabs.mount_editor_tabs(
            self,
            open_tabs=self._open_tabs,
            active_feature=self._active_feature,
            titles=FEATURE_TITLES,
            on_activate=self._activate_feature,
            on_close=self._close_feature_tab,
            on_reorder=lambda order: setattr(self, "_open_tabs", order),
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
                FEATURE_ROUTE.get(feature, ("network",))[0], "topology")
            self._open_feature_tab(default, activate=True)
            return
        nxt = (
            self._active_feature
            if self._active_feature in self._open_tabs
            else self._open_tabs[-1])
        self._mount_tab_bar()
        self._activate_feature(nxt)

    def _add_workspace(
            self, workspace: str,
            features: list[tuple[str, object]]) -> None:
        self._workspace_stack_index[workspace] = self.stack.count()
        self.stack.addWidget(self._build_feature_workspace(workspace, features))

    def _build_feature_workspace(
            self, workspace: str,
            features: list[tuple[str, object]]) -> QStackedWidget:
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
        default = _WORKSPACE_DEFAULT.get(key, "topology")
        self._open_feature_tab(default, activate=True)
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
            key = _WORKSPACE_DEFAULT.get(key, "topology")
        if not _is_leaf_feature(key):
            key = "topology"
        self._open_feature_tab(key, activate=True)
        self._persist()

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
            self, ["Time", "Dir", "CAN ID", "PDU", "Note"], rows,
            "ethercat_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": self._active_feature or "topology",
            "geometry_hex": geo,
            "open_tabs": list(self._open_tabs),
            "session": self.session.to_state(),
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
