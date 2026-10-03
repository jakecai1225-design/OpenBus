# -*- coding: utf-8 -*-
"""AppShell — UDS Suite (Diagnose leaves + File menu + Context Next).

Activities: Diagnose / Scan / Batch / Security + Setup footer.
Profiles live under Diagnose sidebar and File menu.
"""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QKeySequence
from PyQt6.QtWidgets import (
    QCheckBox,
    QFileDialog,
    QLabel,
    QMainWindow,
    QMenuBar,
    QMessageBox,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QWidget,
)

from _shared import codicons, plugin_shell, state_store, suite_chrome, vscode_theme
from _shared import suite_tabs
from pages import _ui
from session import SharedSession

PLUGIN_ID = "uds-suite"
MAX_RECENT = 12

NAV_PAGES = [
    ("diagnose", "Diagnose"),
    ("scan", "Scan"),
    ("batch", "Batch"),
    ("security", "Security"),
    ("setup", "Setup"),
]

FEATURE_ROUTE = {
    "session": ("diagnose", 0),
    "services": ("diagnose", 1),
    "did": ("diagnose", 2),
    "dtc": ("diagnose", 3),
    "sec_access": ("diagnose", 4),
    "flash": ("diagnose", 5),
    "profiles": ("diagnose", 6),
    "scan": ("scan", 0),
    "batch": ("batch", 0),
    "security": ("security", 0),
    "setup": ("setup", 0),
    "diagnose": ("diagnose", 0),
}

FEATURE_TITLES = {
    "session": "Session",
    "services": "Services",
    "did": "DID",
    "dtc": "DTC",
    "sec_access": "SecAccess",
    "flash": "Flash",
    "profiles": "Profiles",
    "scan": "Scan",
    "batch": "Batch",
    "security": "Security",
    "setup": "Setup",
}

_WORKSPACE_DEFAULT = {
    "diagnose": "services",
    "scan": "scan",
    "batch": "batch",
    "security": "security",
    "setup": "setup",
}

_PAGE_ALIASES = {
    "log": "services",
    "profile": "profiles",
    "sec": "sec_access",
    "security_access": "sec_access",
    "session_tab": "session",
}

_ACTIVITY_KEYS = frozenset(k for k, _ in NAV_PAGES)
# Leaves that share an Activity id (scan/batch/…) must still open as tabs.
_LEAF_KEYS = frozenset(FEATURE_TITLES.keys())


def _is_leaf_feature(feature: str) -> bool:
    return feature in _LEAF_KEYS and feature in FEATURE_ROUTE


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("UDS Suite")
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
        self._active_feature = "services"
        self._open_tabs: list[str] = []
        self._tab_bar = None
        self._tab_guard = False
        self._next_action = ("", "", {})
        self._status_chrome_mounted = False
        self._recent_profiles: list = []

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
            self, NAV_PAGES, title="UDS Suite", panel_title="OUTPUT",
            panel_visible=True, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=200)
        _ui.apply_uds_chrome(self)
        self.stack = self._wb.stack

        self._init_status_controls()
        self._build_menubar()
        self._build_output_panel()
        self.session.set_log_fn(self._log_row)

        from pages import (
            batch, diagnose, profiles, scan, security, setup, workspace_sidebar,
        )

        diag = diagnose.build(self, self.session, self._log_row)
        # Keep holder alive for QMessageBox parents inside diagnose closures.
        self._diagnose_host = diag
        for key, w in (getattr(diag, "leaf_pages", None) or {}).items():
            self._pages[key] = w

        self._pages["profiles"] = profiles.build(
            self, self.session, self._log_row)
        self._pages["scan"] = scan.build(self, self.session, self._log_row)
        self._pages["batch"] = batch.build(self, self.session, self._log_row)
        self._pages["security"] = security.build(
            self, self.session, self._log_row)
        self._pages["setup"] = setup.build(self, self.session, self._log_row)

        self._sidebars["diagnose"] = workspace_sidebar.build_diagnose_sidebar(
            self)
        self._sidebars["scan"] = workspace_sidebar.build_scan_sidebar(self)
        self._sidebars["batch"] = workspace_sidebar.build_batch_sidebar(self)
        self._sidebars["security"] = workspace_sidebar.build_security_sidebar(
            self)
        self._sidebars["setup"] = workspace_sidebar.build_setup_sidebar(self)

        self._add_workspace("diagnose", [
            ("session", self._pages["session"]),
            ("services", self._pages["services"]),
            ("did", self._pages["did"]),
            ("dtc", self._pages["dtc"]),
            ("sec_access", self._pages["sec_access"]),
            ("flash", self._pages["flash"]),
            ("profiles", self._pages["profiles"]),
        ])
        self._add_workspace("scan", [("scan", self._pages["scan"])])
        self._add_workspace("batch", [("batch", self._pages["batch"])])
        self._add_workspace("security", [
            ("security", self._pages["security"])])
        self._add_workspace("setup", [("setup", self._pages["setup"])])

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        if page == "log":
            self._wb.expand_panel()
            page = "services"
        page = _PAGE_ALIASES.get(page or "", page or "services")
        if page in _ACTIVITY_KEYS and page not in _LEAF_KEYS:
            page = _WORKSPACE_DEFAULT.get(page, "services")
        if not _is_leaf_feature(page):
            page = "services"
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
        plugin_shell.bind_shortcut(
            self, "Ctrl+Shift+E",
            lambda: self._wb.set_maximized(not self._wb.is_maximized()))
        plugin_shell.bind_shortcut(
            self, "Ctrl+O", lambda: self.run_action("uds.profile_open"))
        plugin_shell.bind_shortcut(
            self, "Ctrl+S", lambda: self.run_action("uds.profile_save"))

        self._log_row(
            "SYS", "-", b"",
            "UDS Suite ready — Session IDs, then Extended, then Services.")
        self._ensure_status_chrome()
        self._sync_conn_label()
        self._sync_next_hint()
        self.session.on_ids_changed(self._sync_conn_label)
        self.session.on_session_changed(
            lambda _n: (self._sync_conn_label(), self._sync_next_hint()))

    # ------------------------------------------------------------------
    def _init_status_controls(self):
        self.conn_label = QLabel("")
        self.conn_label.setObjectName("SuiteDocPath")
        self.conn_label.setMinimumWidth(120)
        self.conn_label.setMaximumWidth(280)
        self.conn_label.setToolTip("Shared TX / RX / session")

        self.next_btn = _ui.ghost_btn(
            "Next", "Suggested next step", "arrow-right")
        self.next_btn.setMaximumWidth(140)
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
        text = "TX 0x%X / RX 0x%X · %s" % (
            s.tx_id, s.rx_id, s.session_name or "unknown")
        self.conn_label.setText(text)
        self.conn_label.setToolTip(text)

    def _mount_chrome(self, feature: str):
        # Prefer editor tabs; title-only is a fallback for empty strip.
        if self._open_tabs:
            self._mount_tab_bar()
        else:
            title = FEATURE_TITLES.get(feature, feature)
            self._wb.set_editor_title(title)
        self._ensure_status_chrome()

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
                FEATURE_ROUTE.get(feature, ("diagnose",))[0], "services")
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
        _act(m_file, "&Open Profile…",
             lambda: self.run_action("uds.profile_open"), "Ctrl+O")
        _act(m_file, "&Save Profile…",
             lambda: self.run_action("uds.profile_save"), "Ctrl+S")
        self._recent_menu = m_file.addMenu("Recent &Profiles")
        self._fill_recent_menu()
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

        m_edit = bar.addMenu("&Edit")
        _act(m_edit, "Open &Extended session",
             lambda: self.run_action("uds.extended"))
        _act(m_edit, "Apply IDs from &Setup",
             lambda: self.run_action("uds.goto", page="setup"))

        m_view = bar.addMenu("&View")
        for key, title in NAV_PAGES:
            _act(m_view, title,
                 lambda _c=False, k=key: self._on_activity_clicked(k))
        m_view.addSeparator()
        for feat, title in (
                ("session", "&Session"),
                ("services", "S&ervices"),
                ("profiles", "&Profiles"),
                ("scan", "S&can")):
            _act(m_view, title, lambda _c=False, f=feat: self.goto_page(f))
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
        _act(m_help, "&About UDS Suite", self._menu_about)

        self._menubar_trailing = suite_chrome.attach_layout_toggles_to_menubar(
            self, self._wb)

    def _fill_recent_menu(self):
        menu = getattr(self, "_recent_menu", None)
        if menu is None:
            return
        menu.clear()
        if not self._recent_profiles:
            a = menu.addAction("(empty)")
            a.setEnabled(False)
            return
        for path in self._recent_profiles:
            label = os.path.basename(path) or path
            act = menu.addAction(label)
            act.setToolTip(path)
            act.triggered.connect(
                lambda _c=False, p=path: self.run_action(
                    "uds.profile_open", path=p))

    def _menu_about(self):
        QMessageBox.information(
            self, "About UDS Suite",
            "UDS Suite — Diagnose, Scan, Batch, Security.\n"
            "ISO 14229 / ISO 15765-2 over the shared OpenBus bus.")

    # ------------------------------------------------------------------
    def run_action(self, action: str, **kwargs):
        label = action
        if action == "uds.goto":
            page = kwargs.get("page") or "services"
            self.goto_page(str(page))
            label = "Go %s" % page
        elif action == "uds.extended":
            self.session.go_session(0x03)
            self.goto_page("services")
            label = "Extended session"
        elif action == "uds.apply_scan_ids":
            tx = int(kwargs.get("req_id") or kwargs.get("tx_id") or 0)
            rx = int(kwargs.get("rsp_id") or kwargs.get("rx_id") or 0)
            if tx and rx:
                self.session.apply_ids(tx, rx)
                self.session.set_focus("services")
                self.goto_page("services")
                label = "Applied scan IDs"
        elif action == "uds.profile_open":
            self._profile_open(kwargs.get("path"))
            label = "Open profile"
        elif action == "uds.profile_save":
            self._profile_save()
            label = "Save profile"
        elif action == "uds.goto_service":
            self.goto_service_target(int(kwargs.get("service") or 0))
            label = "Go service"
        elif action == "uds.goto_did":
            self.goto_did_target(int(kwargs.get("did") or 0))
            label = "Go DID"
        elif action == "uds.goto_dtc":
            self.goto_dtc_target(str(kwargs.get("dtc") or ""))
            label = "Go DTC"
        else:
            plugin_shell.set_status(self, "Unknown action: %s" % action, 3000)
            return
        if action not in (
                "uds.goto", "uds.goto_service", "uds.goto_did", "uds.goto_dtc"):
            self.session.advance_next_hint()
        self._sync_next_hint()
        plugin_shell.set_status(self, label, 2000)

    def goto_service_target(self, service: int):
        sid = int(service or 0) & 0xFF
        self.session.set_focus(leaf="services", service=sid)
        self.goto_page("services")
        page = self._pages.get("services")
        if page is not None and hasattr(page, "select_service"):
            page.select_service(sid)

    def goto_did_target(self, did: int):
        did = int(did or 0) & 0xFFFF
        self.session.set_focus(leaf="did", did=did)
        self.goto_page("did")
        page = self._pages.get("did")
        if page is not None and hasattr(page, "select_did"):
            page.select_did(did)

    def goto_dtc_target(self, dtc: str = ""):
        self.session.set_focus(leaf="dtc", dtc=dtc or "")
        self.goto_page("dtc")
        page = self._pages.get("dtc")
        if page is not None and hasattr(page, "select_dtc") and dtc:
            page.select_dtc(dtc)

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

    def _profile_open(self, path: Optional[str] = None):
        from core import load_profile_file
        if not path:
            path, _ = QFileDialog.getOpenFileName(
                self, "Open ECU profile", "", "JSON (*.json)")
        if not path:
            return
        try:
            p = load_profile_file(path)
        except Exception as exc:
            QMessageBox.warning(self, "UDS Suite", "Failed to open:\n%s" % exc)
            return
        if not p:
            QMessageBox.information(self, "UDS Suite", "Empty profile")
            return
        self.session.apply_ids(
            int(p.get("tx_id", self.session.tx_id)),
            int(p.get("rx_id", self.session.rx_id)),
            int(p.get("func_id", self.session.func_id)))
        self.session._profile_path = path
        self._remember_profile(path)
        state_store.save_state(PLUGIN_ID, p, "profile.json")
        self.goto_page("profiles")
        page = self._pages.get("profiles")
        # Profiles page fills from state on next open; force sync via apply
        self._log_row("SYS", "-", b"", "Opened profile %s" % os.path.basename(path))

    def _profile_save(self):
        from core import default_profile, save_profile_file
        p = default_profile()
        p["tx_id"] = self.session.tx_id
        p["rx_id"] = self.session.rx_id
        p["func_id"] = self.session.func_id
        path, _ = QFileDialog.getSaveFileName(
            self, "Save ECU profile", "ecu_profile.json", "JSON (*.json)")
        if not path:
            return
        save_profile_file(path, p)
        state_store.save_state(PLUGIN_ID, p, "profile.json")
        self._remember_profile(path)
        self._log_row("SYS", "-", b"", "Saved profile %s" % os.path.basename(path))

    def _remember_profile(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self._recent_profiles = [
            p for p in self._recent_profiles if os.path.normpath(p) != path]
        self._recent_profiles.insert(0, path)
        self._recent_profiles = self._recent_profiles[:MAX_RECENT]
        self._fill_recent_menu()
        self._persist()

    # ------------------------------------------------------------------
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
        default = _WORKSPACE_DEFAULT.get(key, "services")
        self._open_feature_tab(default, activate=True)
        self._sync_next_hint()
        self._persist()

    def _on_workbench_page(self, key: str):
        self._on_activity_clicked(key)

    def _activate_feature(self, feature: str):
        if not _is_leaf_feature(feature):
            return
        route = FEATURE_ROUTE[feature]
        workspace, _ = route
        self._active_feature = feature
        self.session.set_focus(feature)
        wi = self._workspace_stack_index.get(workspace)
        if wi is not None and self.stack.currentIndex() != wi:
            self.stack.setCurrentIndex(wi)
        outer = self._workspace_stacks.get(workspace)
        if outer is not None:
            apply = getattr(outer, "_apply_feature", None)
            if callable(apply):
                apply(feature)
            else:
                idx = getattr(outer, "_feature_index", {}).get(feature, 0)
                outer.setCurrentIndex(idx)
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
        # Activity-only keys (e.g. diagnose) map to the workspace default leaf.
        # Keys that are both Activity and leaf (scan/batch/security/setup) stay.
        if key in _ACTIVITY_KEYS and key not in _LEAF_KEYS:
            key = _WORKSPACE_DEFAULT.get(key, "services")
        if not _is_leaf_feature(key):
            key = "services"
        self._open_feature_tab(key, activate=True)
        self._sync_next_hint()
        self._persist()

    # ------------------------------------------------------------------
    def _build_output_panel(self):
        frames = QCheckBox("ISO-TP frames")
        frames.setToolTip("Include ISO-TP flow-control frames in the list")
        frames.setFixedHeight(_ui.CTRL_H)
        frames.toggled.connect(
            lambda c: setattr(self.session, "show_isotp_frames", c))
        self._wb.panel_tools.addWidget(frames)
        self._wb.panel_tools.addStretch(1)

        export_btn = _ui.ghost_btn("Export", "Export log as CSV", "export")
        clear_btn = _ui.ghost_btn("Clear", "Clear OUTPUT list", "clear")
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)

        table = suite_chrome.make_output_table()
        self._wb.panel_body.addWidget(table, 1)
        self.bind_log_table(table)
        export_btn.clicked.connect(self.export_log)
        clear_btn.clicked.connect(self.clear_log)

    def bind_log_table(self, table: QTableWidget):
        self._log_table = table
        table.setRowCount(0)
        for row in self._log_rows[-2000:]:
            self._append_to_table(*row)
        table.scrollToBottom()

    def _log_row(self, direction, can_id, pdu, note, color=None):
        ts = time.time()
        self._log_rows.append((ts, direction, can_id, pdu, note, color))
        if len(self._log_rows) > 5000:
            del self._log_rows[: len(self._log_rows) - 5000]
        if self._log_table is not None:
            self._append_to_table(ts, direction, can_id, pdu, note, color)
            while self._log_table.rowCount() > 2000:
                self._log_table.removeRow(0)
            self._log_table.scrollToBottom()
        if isinstance(can_id, int):
            plugin_shell.set_status(
                self, "%s 0x%X  %s" % (direction, can_id, (note or "")[:48]), 0)
        elif note:
            plugin_shell.set_status(self, str(note)[:64], 0)
        self._sync_next_hint()

    def _append_to_table(self, ts, direction, can_id, pdu, note, color=None):
        if self._log_table is None:
            return
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
            "TX": QColor(vscode_theme.TX),
            "RX": QColor(vscode_theme.RX),
            "ERR": QColor(vscode_theme.ERR),
            "FC": QColor(vscode_theme.FC),
            "SYS": QColor(vscode_theme.TEXT_MUTED),
        }
        c = colors.get(color or direction, QColor(vscode_theme.TEXT_MUTED))
        row = self._log_table.rowCount()
        self._log_table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, hex_str, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self._log_table.setItem(row, col, item)

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
            "uds_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    # ------------------------------------------------------------------
    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        sizes_v = self._wb.v_splitter.sizes()
        state_store.save_state(PLUGIN_ID, {
            "tx_id": self.session.tx_id,
            "rx_id": self.session.rx_id,
            "func_id": self.session.func_id,
            "functional": self.session.functional,
            "tester_present": self.session.tester_present,
            "nav_page": self._active_feature or "services",
            "geometry_hex": geo,
            "sidebar_visible": self._wb.is_sidebar_visible(),
            "panel_visible": self._wb.is_panel_visible(),
            "splitter_v": sizes_v,
            "recent_profiles": list(self._recent_profiles),
            "ids_touched": self.session._ids_touched,
            "open_tabs": list(self._open_tabs),
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            tx = int(saved.get("tx_id", self.session.tx_id))
            rx = int(saved.get("rx_id", self.session.rx_id))
            func = int(saved.get("func_id", self.session.func_id))
            self.session.functional = bool(saved.get("functional", False))
            self.session.tester_present = bool(
                saved.get("tester_present", False))
            self.session.apply_ids(tx, rx, func)
            if not saved.get("ids_touched"):
                self.session._ids_touched = False
            geo = saved.get("geometry_hex")
            if geo:
                from PyQt6.QtCore import QByteArray
                self.restoreGeometry(QByteArray.fromHex(geo.encode("ascii")))
            sv = saved.get("splitter_v")
            if isinstance(sv, list) and len(sv) == 2:
                self._wb.v_splitter.setSizes([int(sv[0]), int(sv[1])])
            recent = saved.get("recent_profiles") or []
            if isinstance(recent, list):
                self._recent_profiles = [str(p) for p in recent][:MAX_RECENT]
                self._fill_recent_menu()
            self._open_tabs = suite_tabs.normalize_open_tabs(
                saved.get("open_tabs"),
                titles=FEATURE_TITLES,
                routes=FEATURE_ROUTE,
                activity_keys=_ACTIVITY_KEYS,
                aliases=_PAGE_ALIASES,
            )
            self._wb.set_sidebar_visible(True)
            self._wb.set_panel_visible(True)
        except (TypeError, ValueError):
            pass

    def closeEvent(self, event):
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
        self.session.shutdown()
