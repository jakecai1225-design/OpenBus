# -*- coding: utf-8 -*-
"""AppShell — J1939 Suite (VS Code chrome: Activity + Side Bar + Tabs + OUTPUT)."""

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

from _shared import (
    codicons, dbc_picker, plugin_shell, state_store, suite_chrome, suite_tabs,
)
from pages import _ui
from session import SharedSession

PLUGIN_ID = "j1939-suite"
MAX_RECENT = 12

NAV_PAGES = [
    ("analyzer", "Live"),
    ("transport", "Transport"),
    ("diagnostics", "Diagnostics"),
    ("network", "Network"),
]

FEATURE_ROUTE = {
    "analyzer": ("analyzer", 0),
    "transport": ("transport", 0),
    "diagnostics": ("diagnostics", 0),
    "network": ("network", 0),
}

FEATURE_TITLES = {
    "analyzer": "Live",
    "transport": "Transport",
    "diagnostics": "Diagnostics",
    "network": "Network",
}

_WORKSPACE_DEFAULT = {
    "analyzer": "analyzer",
    "transport": "transport",
    "diagnostics": "diagnostics",
    "network": "network",
}

_PAGE_ALIASES = {
    "log": "analyzer",
    "live": "analyzer",
    "tp": "transport",
    "dm": "diagnostics",
    "addr": "network",
}

_ACTIVITY_KEYS = frozenset(k for k, _ in NAV_PAGES)
_LEAF_KEYS = frozenset(FEATURE_TITLES.keys())


def _is_leaf_feature(feature: str) -> bool:
    return feature in _LEAF_KEYS and feature in FEATURE_ROUTE


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("J1939 Suite")
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
        self._active_feature = "analyzer"
        self._open_tabs: list[str] = []
        self._tab_bar = None
        self._recent_dbc: list = []
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

        _ui.apply_j1939_chrome(self)
        plugin_shell.attach_status_bar(self, "Ready")
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="J1939 Suite", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=200)
        self.stack = self._wb.stack

        self._init_status_controls()
        self._build_menubar()
        self._build_log_panel()
        self.session.set_log_fn(self._log_row)

        from pages import analyzer, views, workspace_sidebar

        self._pages["analyzer"] = analyzer.build(
            self, self.session, self._log_row)
        self._pages["transport"] = views.build_transport(
            self, self.session, self._log_row)
        self._pages["diagnostics"] = views.build_diagnostics(
            self, self.session, self._log_row)
        self._pages["network"] = views.build_network(
            self, self.session, self._log_row)

        self._sidebars["analyzer"] = workspace_sidebar.build_live_sidebar(self)
        self._sidebars["transport"] = workspace_sidebar.build_transport_sidebar(
            self)
        self._sidebars["diagnostics"] = (
            workspace_sidebar.build_diagnostics_sidebar(self))
        self._sidebars["network"] = workspace_sidebar.build_network_sidebar(self)

        self._add_workspace("analyzer", [("analyzer", self._pages["analyzer"])])
        self._add_workspace(
            "transport", [("transport", self._pages["transport"])])
        self._add_workspace(
            "diagnostics", [("diagnostics", self._pages["diagnostics"])])
        self._add_workspace("network", [("network", self._pages["network"])])

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        settings = state_store.load_state(PLUGIN_ID, "settings.json") or {}
        if settings.get("dbc_path"):
            self.session.note_dbc(str(settings["dbc_path"]))

        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        if page == "log":
            self._wb.expand_panel()
            page = "analyzer"
        page = _PAGE_ALIASES.get(page or "", page or "analyzer")
        if page in _ACTIVITY_KEYS and page not in _LEAF_KEYS:
            page = _WORKSPACE_DEFAULT.get(page, "analyzer")
        if not _is_leaf_feature(page):
            page = "analyzer"
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
            self, "Ctrl+O", lambda: self.run_action("j1939.load_dbc"))

        self._log_row("SYS", "-", b"", "J1939 Suite ready")
        self._ensure_status_chrome()
        self._sync_conn_label()
        self._sync_next_hint()

    def _init_status_controls(self):
        self.conn_label = QLabel("")
        self.conn_label.setObjectName("SuiteDocPath")
        self.conn_label.setMinimumWidth(120)
        self.conn_label.setMaximumWidth(320)
        self.conn_label.setToolTip("Loaded J1939 DBC")
        self.next_btn = _ui.ghost_btn(
            "Next", "Suggested next step", "arrow-right")
        self.next_btn.setMaximumWidth(180)
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
        path = self.session.dbc_path or ""
        text = ("DBC %s" % os.path.basename(path)) if path else "DBC (none)"
        self.conn_label.setText(text)
        self.conn_label.setToolTip(path or "No J1939 DBC loaded")

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
                FEATURE_ROUTE.get(feature, ("analyzer",))[0], "analyzer")
            self._open_feature_tab(default, activate=True)
            return
        nxt = (
            self._active_feature
            if self._active_feature in self._open_tabs
            else self._open_tabs[-1])
        self._mount_tab_bar()
        self._activate_feature(nxt)

    def _build_menubar(self):
        bar = self.menuBar()
        if bar is None:
            bar = QMenuBar(self)
            self.setMenuBar(bar)
        bar.clear()
        bar.setVisible(True)

        def _act(menu, label, slot, shortcut=None):
            a = menu.addAction(label)
            a.triggered.connect(slot)
            if shortcut:
                a.setShortcut(QKeySequence(shortcut))
            return a

        m_file = bar.addMenu("&File")
        _act(m_file, "&Load DBC…",
             lambda: self.run_action("j1939.load_dbc"), "Ctrl+O")
        self._recent_menu = m_file.addMenu("Recent &DBC")
        self._fill_recent_menu()
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

        m_help = bar.addMenu("&Help")
        _act(m_help, "&About J1939 Suite", self._menu_about)

    def _fill_recent_menu(self):
        menu = getattr(self, "_recent_menu", None)
        if menu is None:
            return
        menu.clear()
        if not self._recent_dbc:
            a = menu.addAction("(empty)")
            a.setEnabled(False)
            return
        for path in self._recent_dbc:
            label = os.path.basename(path) or path
            act = menu.addAction(label)
            act.setToolTip(path)
            act.triggered.connect(
                lambda _c=False, p=path: self.run_action(
                    "j1939.load_dbc", path=p))

    def _menu_about(self):
        QMessageBox.information(
            self, "About J1939 Suite",
            "J1939 Suite — Live, Transport, Diagnostics, Network.\n"
            "SAE J1939 PGN / SPN / DM / TP over the shared OpenBus bus.")

    def run_action(self, action: str, **kwargs):
        label = action
        if action == "j1939.goto":
            page = kwargs.get("page") or "analyzer"
            self.goto_page(str(page))
            label = "Go %s" % page
        elif action == "j1939.load_dbc":
            self._load_dbc(kwargs.get("path"))
            label = "Load DBC"
        elif action == "j1939.goto_pgn":
            pgn = int(kwargs.get("pgn") or 0)
            self.goto_pgn_target(pgn)
            label = "Go PGN 0x%X" % pgn
        elif action == "j1939.goto_diagnostics":
            pgn = int(kwargs.get("pgn") or 0)
            if pgn:
                self.session.set_focus(pgn=pgn)
            self.goto_page("diagnostics")
            label = "Go Diagnostics"
        else:
            plugin_shell.set_status(self, "Unknown action: %s" % action, 3000)
            return
        if action not in ("j1939.goto", "j1939.goto_pgn", "j1939.goto_diagnostics"):
            self.session.advance_next_hint()
        self._sync_next_hint()
        plugin_shell.set_status(self, label, 2000)

    def goto_pgn_target(self, pgn: int):
        pgn = int(pgn or 0) & 0x3FFFF
        self.session.set_focus(pgn=pgn)
        self.goto_page("analyzer")
        page = self._pages.get("analyzer")
        if page is not None and hasattr(page, "select_pgn"):
            page.select_pgn(pgn)

    def _load_dbc(self, path: Optional[str] = None):
        from pages import analyzer
        if not path:
            path = dbc_picker.pick_dbc(self, "Load J1939 DBC")
        if not path:
            return
        if not analyzer.load_dbc(path):
            QMessageBox.warning(self, "J1939 Suite", "Failed to load DBC")
            return
        self.session.note_dbc(path)
        self._remember_dbc(path)
        self._sync_conn_label()
        page = self._pages.get("analyzer")
        if page is not None and hasattr(page, "refresh_dbc_label"):
            page.refresh_dbc_label()
        self._log_row(
            "SYS", "-", b"", "Loaded DBC %s" % os.path.basename(path))

    def _remember_dbc(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self._recent_dbc = [
            p for p in self._recent_dbc if os.path.normpath(p) != path]
        self._recent_dbc.insert(0, path)
        self._recent_dbc = self._recent_dbc[:MAX_RECENT]
        self._fill_recent_menu()
        self._persist()

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
        default = _WORKSPACE_DEFAULT.get(key, "analyzer")
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
            key = _WORKSPACE_DEFAULT.get(key, "analyzer")
        if not _is_leaf_feature(key):
            key = "analyzer"
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
            "j1939_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        state_store.save_state(PLUGIN_ID, {
            "nav_page": self._active_feature or "analyzer",
            "geometry_hex": geo,
            "recent_dbc": list(self._recent_dbc),
            "open_tabs": list(self._open_tabs),
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        geo = saved.get("geometry_hex")
        if geo:
            from PyQt6.QtCore import QByteArray
            self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))
        recent = saved.get("recent_dbc") or []
        if isinstance(recent, list):
            self._recent_dbc = [p for p in recent if isinstance(p, str)][:MAX_RECENT]
            self._fill_recent_menu()
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
