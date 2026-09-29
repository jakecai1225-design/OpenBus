# -*- coding: utf-8 -*-
"""AppShell — CANopen Suite (project + EDS workbench).

MenuBar | Activity | Side Bar (DOCUMENT) | editor tabs | OUTPUT | status chrome.
"""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtCore import QSize, Qt
from PyQt6.QtGui import QColor, QKeySequence
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMenuBar,
    QMessageBox,
    QStackedWidget,
    QTabBar,
    QTableWidget,
    QTableWidgetItem,
    QToolButton,
    QWidget,
)

from _shared import codicons, i18n, plugin_shell, state_store, suite_chrome, vscode_theme
from core import project as can_project
from pages import _ui

from session import SharedSession

PLUGIN_ID = "canopen-suite"
MAX_RECENT = 8

# Activity bar — four pillars (Start folded into EDS files).
# Keys: eds / device(Live) / trace / code.
NAV_PAGES = [
    ("eds", "EDS"),
    ("device", "Live"),
    ("trace", "Trace"),
    ("code", "Code"),
]

# Feature key → (workspace, stack index or None).
FEATURE_ROUTE: dict[str, tuple[str, Optional[int]]] = {
    "eds": ("eds", 0),
    "eds_dict": ("eds", 0),
    "eds_device": ("eds", 0),
    "eds_check": ("eds", 0),
    "eds_editor": ("eds", 0),
    "eds_pdo": ("eds", 0),
    "library": ("eds", 0),
    "lib_301": ("eds", 0),
    "lib_302": ("eds", 0),
    "lib_401": ("eds", 0),
    "lib_402": ("eds", 0),
    "lib_406": ("eds", 0),
    "profiles": ("eds", 0),
    "device": ("device", 0),
    "od": ("device", 0),
    "pdo": ("device", 0),
    "network_scan": ("device", 0),
    "network_nmt": ("device", 0),
    "network": ("device", 0),
    "trace": ("trace", 0),
    "monitor": ("trace", 0),
    "code": ("code", 0),
    "eds_codegen": ("code", 0),
    "project": ("eds", 0),
    "setup": ("eds", 0),
}

FEATURE_TITLES: dict[str, str] = {
    "project": "Dictionary",
    "setup": "Dictionary",
    "network": "Scan",
    "network_scan": "Scan",
    "network_nmt": "NMT",
    "monitor": "Trace",
    "trace": "Trace",
    "device": "Live OD",
    "od": "Live OD",
    "pdo": "Live PDO",
    "eds": "Dictionary",
    "eds_dict": "Dictionary",
    "eds_device": "Device info",
    "eds_check": "Validate",
    "eds_codegen": "Codegen",
    "code": "Codegen",
    "eds_pdo": "PDO map",
    "library": "Profiles",
    "lib_301": "CiA 301",
    "lib_302": "CiA 302",
    "lib_401": "CiA 401",
    "lib_402": "CiA 402",
    "lib_406": "CiA 406",
    "profiles": "Profiles",
}

_LIB_FEATURES = frozenset({
    "library", "lib_301", "lib_302", "lib_401", "lib_402", "lib_406", "profiles",
})

_TAB_CANONICAL = {
    "eds": "eds_dict",
    "eds_editor": "eds_dict",
    "eds_device": "eds_dict",
    "eds_check": "eds_dict",
    "library": "profiles",
    "lib_301": "profiles",
    "lib_302": "profiles",
    "lib_401": "profiles",
    "lib_402": "profiles",
    "lib_406": "profiles",
    "setup": "eds_dict",
    "project": "eds_dict",
    "network": "network_scan",
    "device": "od",
    "trace": "monitor",
    "code": "eds_codegen",
    "pdo": "od",
}

_WORKSPACE_DEFAULT = {
    "eds": "eds_dict",
    "device": "od",
    "trace": "monitor",
    "code": "eds_codegen",
}


def normalize_open_tabs(ot) -> list[str]:
    """Accept global list or legacy per-workspace dict; collapse aliases."""
    restored: list[str] = []
    raw: list[str] = []
    if isinstance(ot, list):
        raw = [x for x in ot if isinstance(x, str)]
    elif isinstance(ot, dict):
        for _ws, v in ot.items():
            if not isinstance(v, list):
                continue
            for x in v:
                if isinstance(x, str) and x not in raw:
                    raw.append(x)
    for x in raw:
        if x not in FEATURE_ROUTE:
            continue
        canon = _TAB_CANONICAL.get(x, x)
        if canon not in restored:
            restored.append(canon)
    return restored


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("CANopen Suite")
        self.setMinimumSize(1100, 720)
        self.resize(1400, 900)

        self._context = context
        self.session = SharedSession(parent=self)
        self._log_rows: list = []
        self._log_table: Optional[QTableWidget] = None
        self._pages = {}
        self._workspace_stacks: dict = {}
        self._workspace_features: dict = {}
        self._workspace_stack_index: dict[str, int] = {}
        self._sidebars: dict = {}
        self._active_feature = "eds_dict"
        self._open_tabs: list[str] = []
        self._tab_bar: Optional[QTabBar] = None
        self._tab_guard = False
        self._recent_projects: list[str] = []
        self._recent_eds: list[str] = []
        self._project_manifest = None
        self._chrome_park = QWidget(self)
        self._chrome_park.hide()
        self._chrome_park.setAttribute(
            Qt.WidgetAttribute.WA_DontShowOnScreen, True)

        try:
            codicons.clear_pixmap_cache()
        except Exception:
            pass

        _ui.apply_canopen_chrome(self)
        plugin_shell.attach_status_bar(self, "Ready")
        plugin_shell.wire_close_deactivates(self, PLUGIN_ID)

        self._wb = suite_chrome.build_workbench(
            self, NAV_PAGES, title="CANopen Suite", panel_title="OUTPUT",
            panel_visible=False, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=240)
        self.stack = self._wb.stack
        i18n.on_language_changed(lambda _loc: self.retranslate())
        self.retranslate()
        hs = getattr(self._wb, "h_splitter", None)
        if hs is not None:
            hs.setChildrenCollapsible(False)
            try:
                hs.setCollapsible(0, False)
            except Exception:
                pass

        self._init_document_controls()
        self._build_menubar()
        self._build_output_panel()
        self.session.set_log_fn(self._log_row)

        from pages import (
            codegen, eds_editor, eds_pdo, library, monitor, network, object_dict,
            pdo, workspace_sidebar,
        )

        self._pages["network"] = network.build(self, self.session, self._log_row)
        self._pages["monitor"] = monitor.build(self, self.session, self._log_row)
        self._pages["od"] = object_dict.build(self, self.session, self._log_row)
        self._pages["pdo"] = pdo.build(self, self.session, self._log_row)
        self._pages["eds"] = eds_editor.build(self, self.session, self._log_row)
        self._pages["eds_pdo"] = eds_pdo.build(self, self.session, self._log_row)
        self._pages["codegen"] = codegen.build(self, self.session, self._log_row)
        self._pages["library"] = library.build(self, self.session, self._log_row)

        self._sidebars["eds"] = workspace_sidebar.build_eds_sidebar(self)
        self._sidebars["device"] = workspace_sidebar.build_section_sidebar(
            self, "Live", workspace_sidebar.LIVE_SECTIONS,
            nested=workspace_sidebar.LIVE_NESTED)
        self._sidebars["trace"] = workspace_sidebar.build_section_sidebar(
            self, "Trace", workspace_sidebar.TRACE_SECTIONS,
            nested=workspace_sidebar.TRACE_NESTED)
        self._sidebars["code"] = workspace_sidebar.build_section_sidebar(
            self, "Code", workspace_sidebar.CODE_SECTIONS,
            nested=workspace_sidebar.CODE_NESTED)
        # Legacy aliases
        self._sidebars["project"] = self._sidebars["eds"]
        self._sidebars["network"] = self._sidebars["trace"]

        self._add_workspace("eds", [
            ("eds_dict", self._pages["eds"]),
            ("eds_pdo", self._pages["eds_pdo"]),
            ("profiles", self._pages["library"]),
        ])
        self._wire_eds_inner()
        self._add_workspace("device", [
            ("od", self._pages["od"]),
            ("pdo", self._pages["pdo"]),
            ("network_scan", self._pages["network"]),
            ("network_nmt", self._pages["network"]),
        ])
        self._wire_live_inner()
        self._add_workspace("trace", [
            ("monitor", self._pages["monitor"]),
        ])
        self._add_workspace("code", [
            ("eds_codegen", self._pages["codegen"]),
        ])

        context.on_frame(self.session.on_frame)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        has_eds = bool(
            getattr(self.session, "eds_path", None)
            or getattr(self.session, "draft_entries", None))
        default_page = "eds_dict"
        page = start_page or goto.get("start_page") or saved.get("nav_page") or default_page
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        aliases = {
            "log": "eds_dict",
            "eds_editor": "eds_dict",
            "object_dict": "od",
            "profiles": "profiles",
            "eds": "eds_dict",
            "network": "network_scan",
            "library": "profiles",
            "device": "od",
            "setup": "eds_dict",
            "project": "eds_dict",
            "trace": "monitor",
            "code": "eds_codegen",
        }
        if page == "log":
            self._wb.set_panel_visible(True)
        page = aliases.get(page, page)
        # Stale Start / Overview deep-links land on Dictionary
        if page in ("project", "setup"):
            page = "eds_dict"
        activity = FEATURE_ROUTE.get(page, ("eds", 0))[0]
        if activity not in dict(NAV_PAGES):
            activity = "eds"
            page = default_page
        self._switch_activity(activity)
        self.goto_page(page if page in FEATURE_ROUTE else default_page)

        self._wb.set_sidebar_visible(True)
        if not saved.get("panel_visible"):
            self._wb.set_panel_visible(False)

        suite_chrome.bind_nav_shortcuts(self, NAV_PAGES, self._on_activity_clicked)
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
            self, "Ctrl+Shift+N", lambda: self.run_action("project.new"))
        plugin_shell.bind_shortcut(
            self, "Ctrl+Shift+O", lambda: self.run_action("project.open"))
        self._log_row(
            "SYS", "-", b"",
            "File menu · Ctrl+N New EDS · Ctrl+O Open · Ctrl+B Side Bar")

        def _on_doc_changed():
            self.session.refresh_dirty()
            self._refresh_doc_chrome()
            if self._open_tabs:
                self._mount_tab_bar()
            sb = self._sidebars.get("eds")
            refresh = getattr(sb, "refresh_doc", None)
            if callable(refresh):
                refresh()

        self.session.on_od_changed(_on_doc_changed)
        self.session.on_node_changed(lambda: self._refresh_doc_chrome())
        self.session.on_project_changed(lambda: self._refresh_doc_chrome())
        self._refresh_doc_chrome()

    # ------------------------------------------------------------------
    def _init_document_controls(self):
        # Parked widgets — shown on status bar, not the editor tab row.
        self.path_label = QLabel("(no EDS)")
        self.path_label.setObjectName("SuiteDocPath")
        self.path_label.setMinimumWidth(80)
        self.dirty_label = QLabel("")
        self.dirty_label.setObjectName("SuiteDirtyDot")
        self.dirty_label.setToolTip("Unsaved EDS changes")
        self.node_label = QLabel("N1")
        self.node_label.setObjectName("SuiteCount")
        self.node_label.setToolTip("Active Node-ID (click to change)")
        self.node_label.setCursor(Qt.CursorShape.PointingHandCursor)
        self.node_label.mousePressEvent = (  # type: ignore[method-assign]
            lambda _e: self.run_action("prefs.open"))
        self._doc_btns = []
        from pages import _ui as _cui
        save_btn = _cui.icon_tool("save", "Save EDS (Ctrl+S)")
        save_btn.clicked.connect(lambda: self.run_action("eds.save"))
        self._doc_btns.append(save_btn)
        apply_btn = _cui.icon_tool("apply", "Apply EDS → Live OD (Ctrl+Return)")
        apply_btn.clicked.connect(lambda: self.run_action("eds.apply_od"))
        self._doc_btns.append(apply_btn)
        # Interop Unity IU-1 — one Next ghost in status chrome.
        self._next_action = ("", "", {})
        self.next_btn = _cui.ghost_btn(
            "Next", "Suggested next step for this session", "arrow-right")
        self.next_btn.setMaximumWidth(128)
        self.next_btn.clicked.connect(self._run_next_hint)
        self._doc_btns.append(self.next_btn)

    def _run_next_hint(self):
        label, action, kw = self._next_action
        if not action:
            return
        self.run_action(action, **(kw or {}))
        if action in ("view.trace", "network.nmt", "eds.codegen"):
            self.session.advance_next_hint()
        self._sync_next_hint()
        plugin_shell.set_status(self, label or action, 2000)

    def _sync_next_hint(self):
        btn = getattr(self, "next_btn", None)
        if btn is None:
            return
        label, action, kw = self.session.next_hint()
        self._next_action = (label, action, kw or {})
        btn.setText(label or "Next")
        btn.setEnabled(bool(action))
        btn.setToolTip("Next: %s" % (label or "(none)"))

    def _refresh_doc_chrome(self):
        self.session.refresh_dirty()
        dirty = "●" if self.session.eds_dirty else ""
        self.dirty_label.setText(dirty)
        if self.session.eds_path:
            text = os.path.basename(self.session.eds_path)
            tip = self.session.eds_path
        elif self.session.draft_entries:
            text = "Untitled.eds"
            tip = "Unsaved draft"
        else:
            text = "No EDS"
            tip = ""
        if self.session.has_project():
            text = "%s · %s" % (self.session.project_name or "Project", text)
        self.path_label.setText(text)
        self.path_label.setToolTip(tip)
        self.node_label.setText("N%d" % self.session.node_id)
        self._sync_next_hint()
        sb = self._sidebars.get("eds")
        refresh = getattr(sb, "refresh_doc", None)
        if callable(refresh):
            refresh()
        self._sync_tab_titles()

    def _sync_tab_titles(self):
        """Refresh dirty-dot / stem on open editor tabs without remounting."""
        bar = getattr(self, "_tab_bar", None)
        if bar is None:
            return
        for i in range(bar.count()):
            feat = bar.tabData(i)
            if feat:
                bar.setTabText(i, self._tab_title(str(feat)))

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
        _act(m_file, "&New EDS…",
             lambda: self.run_action("eds.new"), "Ctrl+N")
        _act(m_file, "&Open EDS…",
             lambda: self.run_action("eds.open"), "Ctrl+O")
        _act(m_file, "&Save",
             lambda: self.run_action("eds.save"), "Ctrl+S")
        _act(m_file, "Save &As…",
             lambda: self.run_action("eds.save_as"), "Ctrl+Shift+S")
        self._recent_eds_menu = m_file.addMenu("Recent &EDS")
        self._fill_recent_eds_menu()
        m_file.addSeparator()
        _act(m_file, "New &Project…",
             lambda: self.run_action("project.new"), "Ctrl+Shift+N")
        _act(m_file, "&Open Project…",
             lambda: self.run_action("project.open"), "Ctrl+Shift+O")
        self._project_eds_menu = m_file.addMenu("Project E&DS")
        self._fill_project_eds_menu()
        self._recent_proj_menu = m_file.addMenu("Recent &Projects")
        self._fill_recent_projects_menu()
        m_file.addSeparator()
        _act(m_file, "&Export Code…",
             lambda: self.run_action("eds.codegen"))
        m_file.addSeparator()
        _act(m_file, "&Preferences…",
             lambda: self.run_action("prefs.open"))
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

        m_edit = bar.addMenu("&Edit")
        _act(m_edit, "&Apply EDS → OD",
             lambda: self.run_action("eds.apply_od"), "Ctrl+Return")

        m_prof = bar.addMenu("&Profile")
        _act(m_prof, "Browse &Profiles…",
             lambda: self.run_action("profile.browse"))
        m_prof.addSeparator()
        for pid, label in (
                ("301", "Insert CiA &301"),
                ("401", "Insert CiA 40&1"),
                ("402", "Insert CiA 40&2"),
                ("406", "Insert CiA 40&6")):
            _act(m_prof, label,
                 lambda _c=False, p=pid: self.run_action(
                     "profile.insert", profile_id=p))

        m_dev = bar.addMenu("&Device")
        _act(m_dev, "Jump to &Live OD",
             lambda: self.run_action("view.od"))
        _act(m_dev, "&Read selection",
             lambda: self.run_action("device.read_sel"))
        _act(m_dev, "Read &EDS vs Live",
             lambda: self.run_action("device.read_all"))
        m_dev.addSeparator()
        _act(m_dev, "Live &PDO…",
             lambda: self.goto_page("pdo"))

        m_net = bar.addMenu("&Network")
        _act(m_net, "&Scan", lambda: self.run_action("network.scan"))
        _act(m_net, "&Trace…", lambda: self.goto_page("monitor"))
        m_net.addSeparator()
        from session import (
            NMT_PREOP, NMT_RESET_COMM, NMT_RESET_NODE, NMT_START, NMT_STOP)
        for label, cmd in (
                ("NMT &Start", NMT_START),
                ("NMT Sto&p", NMT_STOP),
                ("NMT Pre-&op", NMT_PREOP),
                ("NMT Reset &Node", NMT_RESET_NODE),
                ("NMT Reset &Communication", NMT_RESET_COMM)):
            _act(m_net, label,
                 lambda _c=False, c=cmd: self.run_action("network.nmt", cmd=c))

        m_view = bar.addMenu("&View")
        for key, title in NAV_PAGES:
            _act(m_view, title,
                 lambda _c=False, k=key: self._on_activity_clicked(k))
        m_view.addSeparator()
        _act(m_view, "&Dictionary", lambda: self.goto_page("eds_dict"))
        _act(m_view, "Device &info", lambda: self.goto_page("eds_device"))
        _act(m_view, "PDO &map (file)", lambda: self.goto_page("eds_pdo"))
        _act(m_view, "Live &PDO", lambda: self.goto_page("pdo"))
        m_view.addSeparator()
        _act(m_view, "Toggle &Side Bar",
             lambda: self._wb.set_sidebar_visible(
                 not self._wb.is_sidebar_visible()), "Ctrl+B")
        _act(m_view, "Toggle &OUTPUT",
             lambda: self._wb.set_panel_visible(
                 not self._wb.is_panel_visible()), "Ctrl+J")

        m_help = bar.addMenu("&Help")
        _act(m_help, "&Keyboard shortcuts", self._menu_shortcuts)
        _act(m_help, "&About CANopen Suite", self._menu_about)

    def _fill_recent_projects_menu(self):
        menu = getattr(self, "_recent_proj_menu", None)
        if menu is None:
            return
        menu.clear()
        paths = [p for p in self._recent_projects if p]
        if not paths:
            a = menu.addAction("(empty)")
            a.setEnabled(False)
            return
        for path in paths:
            label = os.path.basename(path) or path
            a = menu.addAction(label)
            a.setToolTip(path)
            a.triggered.connect(
                lambda _c=False, p=path: self.run_action(
                    "project.open_path", path=p))

    def _fill_recent_eds_menu(self):
        menu = getattr(self, "_recent_eds_menu", None)
        if menu is None:
            return
        menu.clear()
        paths = [p for p in (self._recent_eds or []) if p]
        cur = os.path.normpath(self.session.eds_path or "")
        shown = 0
        for path in paths:
            np = os.path.normpath(path)
            if not os.path.isfile(np):
                continue
            label = os.path.basename(np)
            if cur and np == cur:
                label = "%s  (current)" % label
            a = menu.addAction(label)
            a.setToolTip(np)
            a.triggered.connect(
                lambda _c=False, p=np: self.open_eds_path(p))
            shown += 1
        if not shown:
            a = menu.addAction("(empty)")
            a.setEnabled(False)

    def _fill_project_eds_menu(self):
        menu = getattr(self, "_project_eds_menu", None)
        if menu is None:
            return
        menu.clear()
        if not self.session.has_project():
            a = menu.addAction("(no project open)")
            a.setEnabled(False)
            return
        try:
            paths = can_project.list_project_eds(
                self.session.project_root, self._project_manifest)
        except Exception:
            paths = []
        primary = ""
        if self._project_manifest is not None:
            try:
                primary = os.path.normpath(
                    self._project_manifest.abs_eds() or "")
            except Exception:
                primary = ""
        cur = os.path.normpath(self.session.eds_path or "")
        if not paths:
            a = menu.addAction("(no EDS in folder)")
            a.setEnabled(False)
            return
        for path in paths:
            np = os.path.normpath(path)
            label = os.path.basename(np)
            if primary and np == primary:
                label = "%s  *" % label
            elif cur and np == cur:
                label = "%s  ·" % label
            a = menu.addAction(label)
            tip = np
            if primary and np == primary:
                tip += "\nPrimary project EDS"
            a.setToolTip(tip)
            a.triggered.connect(
                lambda _c=False, p=np: self.open_eds_path(p))

    def _menu_about(self):
        QMessageBox.information(
            self, "CANopen Suite",
            "Beginner path:\n"
            "  1. New or Open an EDS (Ctrl+N / Ctrl+O)\n"
            "  2. Insert Profiles · edit Dictionary\n"
            "  3. Validate · Save (Ctrl+S)\n"
            "  4. Apply → Live OD · SDO read/write\n"
            "  5. Scan / NMT · Analysis (EDS decode)\n\n"
            "Analysis (Network): import EDS with PDO maps, then watch live\n"
            "bus or Import CSV — Trace decode for PDO objects, SDO (abort/\n"
            "segmented/block), EMCY, LSS, SYNC/TIME, Heartbeat vs Guarding,\n"
            "CiA 402 statusword, Watch pane, protocol Flags, and Send generator.")

    def _menu_shortcuts(self):
        QMessageBox.information(
            self, "Shortcuts",
            "Ctrl+N  New EDS\n"
            "Ctrl+O  Open EDS\n"
            "Ctrl+S  Save\n"
            "Ctrl+Return  Apply EDS → Live OD\n"
            "Ctrl+B  Toggle Side Bar\n"
            "Ctrl+J  Toggle OUTPUT\n"
            "Ctrl+1…4  Activities (EDS / Live / Trace / Code)\n\n"
            "Right-click trees for more actions.\n"
            "Drag a Profile onto Dictionary to insert.")

    # ------------------------------------------------------------------
    def run_action(self, name: str, **kw):
        """Unified command bus for menus, chrome, and pages."""
        handlers = {
            "project.new": self._action_project_new,
            "project.open": self._action_project_open,
            "project.open_path": lambda: self._open_project_path(
                kw.get("path") or ""),
            "project.save": lambda: self._action_project_save(**kw),
            "eds.new": self.new_eds,
            "eds.open": self.open_eds,
            "eds.open_path": lambda: self.open_eds_path(kw.get("path") or ""),
            "eds.save": self.save_eds,
            "eds.save_as": self.save_eds_as,
            "eds.apply_od": self._action_apply_od,
            "eds.codegen": lambda: self.goto_page("eds_codegen"),
            "profile.browse": lambda: self.goto_page("profiles"),
            "profile.insert": lambda: self._action_profile_insert(
                kw.get("profile_id") or "301"),
            "view.eds": lambda: self.goto_page("eds_dict"),
            "view.od": lambda: self.goto_page("od"),
            "view.project": lambda: self.goto_page("eds_dict"),
            "view.pdo_map": lambda: self.goto_page("eds_pdo"),
            "view.check": lambda: self.goto_page("eds_check"),
            "view.trace": lambda: self.goto_page("monitor"),
            "device.read_sel": self._action_device_read_sel,
            "device.read_all": self._action_device_read_all,
            "network.scan": self._action_network_scan,
            "network.nmt": lambda: self.session.send_nmt(int(kw.get("cmd") or 0)),
            "network.use_node": lambda: self._action_use_node(
                int(kw.get("node_id") or self.session.node_id)),
            "eds.focus": lambda: self._action_eds_focus(
                int(kw.get("index") or 0), int(kw.get("subindex") or 0)),
            "device.focus_read": lambda: self._action_device_focus_read(
                int(kw.get("index") or 0), int(kw.get("subindex") or 0)),
            "prefs.open": self._action_prefs,
        }
        fn = handlers.get(name)
        if fn is None:
            plugin_shell.set_status(self, "Unknown action: %s" % name, 2000)
            return
        fn()

    def _action_apply_od(self):
        if not self.session.draft_entries:
            plugin_shell.set_status(self, "No EDS draft to apply", 2500)
            return
        self.session.sync_od_from_draft()
        self._log_row(
            "SYS", "-", b"",
            "Applied draft → OD (%d objects)" % len(self.session.od_entries))
        plugin_shell.set_status(self, "EDS applied to OD", 2500)
        reply = QMessageBox.question(
            self, "Apply EDS → OD",
            "Draft copied to the live object dictionary.\nOpen Device OD?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.Yes)
        if reply == QMessageBox.StandardButton.Yes:
            self.goto_page("od")

    def _action_profile_insert(self, profile_id: str):
        from _shared.canopen_profiles import objects_for
        entries = objects_for(str(profile_id))
        if not entries:
            plugin_shell.set_status(
                self, "Unknown profile %s" % profile_id, 2500)
            return
        stats = self.session.set_draft_from_library(
            entries, merge=True, overwrite=True)
        self.goto_page("eds_dict")
        msg = "CiA %s → +%d / ~%d / skip %d" % (
            profile_id, stats.get("added", 0), stats.get("updated", 0),
            stats.get("skipped", 0))
        self._log_row("SYS", "-", b"", msg)
        plugin_shell.set_status(self, msg, 4000)

    def _action_device_read_sel(self):
        self.goto_page("od")
        page = self._pages.get("od")
        if page is not None and hasattr(page, "read_selection"):
            page.read_selection()

    def _action_device_read_all(self):
        self.goto_page("od")
        page = self._pages.get("od")
        if page is not None and hasattr(page, "read_all_eds"):
            page.read_all_eds()

    def _action_network_scan(self):
        self.goto_page("network_scan")
        page = self._pages.get("network")
        if page is not None and hasattr(page, "start_scan"):
            page.start_scan()

    def _action_use_node(self, node_id: int):
        self.session.set_node_id(int(node_id))
        self._refresh_doc_chrome()
        self.goto_page("od")
        plugin_shell.set_status(
            self, "Node-ID %d · Object Dictionary" % self.session.node_id, 2500)

    def _action_eds_focus(self, index: int, subindex: int = 0):
        if index:
            self.session.set_focus(index, subindex)
        if not index:
            self.goto_page("eds_dict")
            return
        self.goto_page("eds_dict")
        page = self._pages.get("eds")
        if page is not None and hasattr(page, "focus_object"):
            page.focus_object(index, subindex)
        plugin_shell.set_status(
            self, "EDS 0x%04X:%02X" % (index, subindex), 2000)

    def _action_device_focus_read(self, index: int, subindex: int = 0):
        if index:
            self.session.set_focus(index, subindex)
        if not index:
            self.goto_page("od")
            return
        self.goto_page("od")
        page = self._pages.get("od")
        if page is not None and hasattr(page, "focus_object"):
            page.focus_object(index, subindex)
        if page is not None and hasattr(page, "read_selection"):
            page.read_selection()

    def _action_prefs(self):
        from PyQt6.QtWidgets import QInputDialog
        nid, ok = QInputDialog.getInt(
            self, "Preferences", "Node-ID (1–127):",
            self.session.node_id, 1, 127)
        if ok:
            self.session.set_node_id(nid)
            self._refresh_doc_chrome()

    def _action_project_new(self):
        from PyQt6.QtWidgets import QFileDialog, QInputDialog
        if not self._confirm_discard():
            return
        root = QFileDialog.getExistingDirectory(self, "New project folder")
        if not root:
            return
        name, ok = QInputDialog.getText(
            self, "New Project", "Project name:",
            text=os.path.basename(root) or "Untitled")
        if not ok:
            return
        name = (name or "").strip() or os.path.basename(root) or "Untitled"
        try:
            manifest = can_project.create_project(root, name=name,
                                                  node_id=self.session.node_id)
        except OSError as e:
            QMessageBox.warning(self, "CANopen Suite", str(e))
            return
        self._project_manifest = manifest
        self.session.set_project(root, name)
        self.session.set_node_id(manifest.node_id)
        self._remember_project(root)
        # Seed empty EDS if missing
        eds_path = manifest.abs_eds()
        if not os.path.isfile(eds_path):
            self.session.new_empty()
            self.session.save_eds(eds_path)
            self._remember_eds(eds_path)
        elif self.session.load_eds(eds_path):
            self._remember_eds(eds_path)
        self.goto_page("eds_dict")
        self._log_row("SYS", "-", b"", "Created project %s" % root)

    def _action_project_open(self):
        from PyQt6.QtWidgets import QFileDialog
        if not self._confirm_discard():
            return
        root = QFileDialog.getExistingDirectory(self, "Open project folder")
        if root:
            self._open_project_path(root)

    def _open_project_path(self, root: str):
        if not root:
            return
        if not self._confirm_discard():
            return
        if not can_project.is_project_dir(root):
            QMessageBox.warning(
                self, "CANopen Suite",
                "No %s in:\n%s" % (can_project.MANIFEST_NAME, root))
            return
        try:
            manifest = can_project.load_manifest(root)
        except (OSError, ValueError, TypeError) as e:
            QMessageBox.warning(self, "CANopen Suite", str(e))
            return
        self._project_manifest = manifest
        self.session.set_project(root, manifest.name)
        self.session.set_node_id(manifest.node_id)
        eds = can_project.resolve_eds_path(manifest)
        if eds and self.session.load_eds(eds):
            self._remember_eds(eds)
        self._remember_project(root)
        self.goto_page("eds_dict")
        self._log_row("SYS", "-", b"", "Opened project %s" % root)

    def _action_project_save(self, **kw):
        if not self.session.has_project():
            plugin_shell.set_status(self, "No project open", 2500)
            return
        root = self.session.project_root
        try:
            manifest = self._project_manifest or can_project.load_manifest(root)
        except (OSError, ValueError):
            manifest = can_project.ProjectManifest(
                name=self.session.project_name, root=root)
        if kw.get("name"):
            manifest.name = str(kw["name"])
        if "node_id" in kw:
            manifest.node_id = int(kw["node_id"])
            self.session.set_node_id(manifest.node_id)
        if "notes" in kw:
            manifest.notes = str(kw.get("notes") or "")
        if self.session.eds_path and root:
            try:
                rel = os.path.relpath(self.session.eds_path, root)
                if not rel.startswith(".."):
                    can_project.set_primary_eds(
                        manifest, self.session.eds_path)
            except ValueError:
                pass
        # Sync eds_files from folder scan so multi-EDS stays listed
        listed = can_project.list_project_eds(root, manifest)
        rels = []
        for ap in listed:
            try:
                r = os.path.relpath(ap, root).replace("\\", "/")
                if not r.startswith(".."):
                    rels.append(r)
            except ValueError:
                pass
        if rels:
            manifest.eds_files = rels
        manifest.root = root
        try:
            can_project.save_manifest(manifest)
        except OSError as e:
            QMessageBox.warning(self, "CANopen Suite", str(e))
            return
        self._project_manifest = manifest
        self.session.set_project(root, manifest.name)
        sb = self._sidebars.get("eds")
        refresh = getattr(sb, "refresh_eds", None) or getattr(sb, "rebuild", None)
        if callable(refresh):
            refresh()
        plugin_shell.set_status(self, "Project saved", 2500)

    def _remember_project(self, path: str):
        path = os.path.normpath(path)
        self._recent_projects = [
            p for p in self._recent_projects if os.path.normpath(p) != path]
        self._recent_projects.insert(0, path)
        self._recent_projects = self._recent_projects[:MAX_RECENT]
        self._fill_recent_projects_menu()
        self._fill_project_eds_menu()

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
        keys = []
        seen: dict[int, int] = {}
        for key, page in features:
            pid = id(page)
            if pid in seen:
                keys.append(key)
                continue
            seen[pid] = stack.count()
            stack.addWidget(page)
            keys.append(key)
        self._workspace_stacks[workspace] = stack
        self._workspace_features[workspace] = keys
        fmap = {}
        idx = 0
        placed = {}
        for key, page in features:
            pid = id(page)
            if pid not in placed:
                placed[pid] = idx
                idx += 1
            fmap[key] = placed[pid]
        stack._feature_index = fmap  # type: ignore[attr-defined]
        return stack

    def _wire_live_inner(self):
        """Live workspace: OD / PDO pages + Network Scan/NMT on shared widget."""
        net = self._pages["network"]
        outer = self._workspace_stacks["device"]
        # od=0, pdo=1, network page=2
        outer._feature_index = {
            "od": 0, "device": 0, "pdo": 1,
            "network_scan": 2, "network_nmt": 2, "network": 2,
        }

        def apply(key: str):
            if key == "pdo":
                outer.setCurrentIndex(1)
                return
            if key in ("network_scan", "network_nmt", "network"):
                outer.setCurrentIndex(2)
                if hasattr(net, "select_view"):
                    net.select_view(key)
                return
            outer.setCurrentIndex(0)

        outer._apply_feature = apply  # type: ignore[attr-defined]

    def _wire_eds_inner(self):
        eds = self._pages["eds"]
        lib = self._pages["library"]
        outer = self._workspace_stacks["eds"]
        # eds=0, eds_pdo=1, library/profiles=2 (project/setup → Objects)
        outer._feature_index = {
            "eds_dict": 0, "eds": 0, "eds_device": 0, "eds_check": 0,
            "eds_pdo": 1,
            "lib_301": 2, "lib_302": 2, "lib_401": 2, "lib_402": 2,
            "lib_406": 2, "library": 2, "profiles": 2,
            "project": 0, "setup": 0,
        }

        def apply(key: str):
            if key in ("project", "setup"):
                outer.setCurrentIndex(0)
                if hasattr(eds, "select_view"):
                    eds.select_view("eds_dict")
                return
            if key in _LIB_FEATURES:
                outer.setCurrentIndex(2)
                if hasattr(lib, "select_view"):
                    lib.select_view(key)
                return
            if key == "eds_pdo":
                outer.setCurrentIndex(1)
                return
            outer.setCurrentIndex(0)
            if hasattr(eds, "select_view"):
                eds.select_view(key)

        outer._apply_feature = apply  # type: ignore[attr-defined]

    def _doc_stem(self) -> str:
        p = getattr(self.session, "eds_path", "") or ""
        dirty = "● " if getattr(self.session, "eds_dirty", False) else ""
        if p:
            return dirty + os.path.basename(p)
        if getattr(self.session, "draft_entries", None):
            return dirty + "Untitled.eds"
        return ""

    def _confirm_discard(self) -> bool:
        self.session.refresh_dirty()
        if not self.session.eds_dirty:
            return True
        reply = QMessageBox.question(
            self, "Unsaved EDS",
            "Discard unsaved EDS changes?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No)
        return reply == QMessageBox.StandardButton.Yes

    def new_eds(self):
        if not self._confirm_discard():
            return
        from pages.eds_wizard import run_new_eds_wizard
        if run_new_eds_wizard(self, self.session):
            if self.session.eds_path:
                self._remember_eds(self.session.eds_path)
            self.goto_page("eds_dict")
            sb = self._sidebars.get("eds")
            refresh = getattr(sb, "refresh_eds", None) or getattr(
                sb, "rebuild", None)
            if callable(refresh):
                refresh()

    def open_eds(self):
        if not self._confirm_discard():
            return
        from PyQt6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getOpenFileName(
            self, "Open EDS/DCF", self.session.eds_path or "",
            "EDS/DCF (*.eds *.dcf);;EDS (*.eds);;DCF (*.dcf);;All (*)")
        if path:
            self.open_eds_path(path, confirm=False)

    def open_eds_path(self, path: str, *, confirm: bool = True):
        """Load an EDS by absolute path (File → Recent / Project EDS)."""
        path = os.path.normpath(path or "")
        if not path:
            return
        if not os.path.isfile(path):
            plugin_shell.set_status(self, "File not found: %s" % path, 3000)
            return
        if confirm and not self._confirm_discard():
            return
        # Same file already open — just jump to Dictionary
        cur = os.path.normpath(self.session.eds_path or "")
        if cur and cur == path and self.session.draft_entries:
            self._remember_eds(path)
            self.goto_page("eds_dict")
            return
        if self.session.load_eds(path):
            self._remember_eds(path)
            # If this EDS lives in the open project, mark it primary.
            if self.session.has_project() and self._project_manifest:
                root = self.session.project_root
                try:
                    common = os.path.commonpath(
                        [os.path.normpath(root), os.path.normpath(path)])
                except ValueError:
                    common = ""
                if common and os.path.normpath(common) == os.path.normpath(root):
                    can_project.set_primary_eds(self._project_manifest, path)
                    try:
                        can_project.save_manifest(self._project_manifest)
                    except OSError:
                        pass
            self.goto_page("eds_dict")
            self._refresh_doc_chrome()
            sb = self._sidebars.get("eds")
            refresh = getattr(sb, "refresh_eds", None) or getattr(
                sb, "rebuild", None)
            if callable(refresh):
                refresh()

    def save_eds(self):
        page = self._pages.get("eds")
        if page is not None and hasattr(page, "save_document"):
            page.save_document()
            # Ensure clean after flush/save; refresh tab titles (dirty dot).
            if hasattr(self.session, "_mark_clean"):
                self.session._mark_clean()
            if self.session.eds_path:
                self._remember_eds(self.session.eds_path)
            self._refresh_doc_chrome()
            return
        if not self.session.eds_path:
            self.save_eds_as()
            return
        self.session.save_eds()
        self._remember_eds(self.session.eds_path)
        self._refresh_doc_chrome()

    def save_eds_as(self):
        from PyQt6.QtWidgets import QFileDialog
        start = self.session.eds_path or "Untitled.eds"
        path, _ = QFileDialog.getSaveFileName(
            self, "Save EDS", start,
            "EDS (*.eds);;DCF (*.dcf);;All (*)")
        if not path:
            return
        self.session.save_eds(path)
        self._remember_eds(path)
        self._refresh_doc_chrome()

    def _remember_eds(self, path: str):
        path = os.path.normpath(path or "")
        if not path:
            return
        self._recent_eds = [
            p for p in self._recent_eds if os.path.normpath(p) != path]
        self._recent_eds.insert(0, path)
        self._recent_eds = self._recent_eds[:MAX_RECENT]
        self._fill_recent_eds_menu()
        self._fill_project_eds_menu()
        sb = self._sidebars.get("eds")
        refresh = getattr(sb, "refresh_eds", None) or getattr(sb, "rebuild", None)
        if callable(refresh):
            refresh()
        # Doc banner on EDS sidebar
        refresh_doc = getattr(sb, "refresh_doc", None)
        if callable(refresh_doc):
            refresh_doc()

    def import_profile_dialog(self):
        self.run_action("profile.browse")

    def _tab_title(self, feature: str) -> str:
        base = FEATURE_TITLES.get(feature, feature)
        route = FEATURE_ROUTE.get(feature)
        if not route:
            return base
        workspace, _ = route
        if workspace != "eds" or feature in _LIB_FEATURES:
            return base
        stem = self._doc_stem()
        if not stem:
            return base
        if feature in ("eds_dict", "eds"):
            return stem
        return "%s · %s" % (stem, base)

    def _switch_activity(self, key: str):
        if key not in dict(NAV_PAGES):
            return
        sb = self._sidebars.get(key)
        if sb is not None and self._wb.set_side_bar_widget is not None:
            self._wb.set_side_bar_widget(sb)
        ha = getattr(self._wb, "highlight_activity", None)
        if callable(ha):
            ha(key)
        if self._open_tabs:
            self._mount_tab_bar()
        try:
            from _shared import activity_snapshot
            activity_snapshot.update(
                active_plugin="canopen-suite", active_page=key)
        except Exception:
            pass

    def _on_activity_clicked(self, key: str):
        self._switch_activity(key)
        if not self._open_tabs:
            default = _WORKSPACE_DEFAULT.get(key, "eds_dict")
            self._open_feature_tab(default, activate=True)
        self._persist()

    def _on_workbench_page(self, key: str):
        self._on_activity_clicked(key)

    def _build_output_panel(self):
        self._wb.panel_tools.addStretch(1)
        export_btn = _ui.ghost_btn("Export", "Export OUTPUT as CSV", "export")
        clear_btn = _ui.ghost_btn("Clear", "Clear OUTPUT", "clear")
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)
        table = suite_chrome.make_output_table()
        self._log_table = table
        self._wb.panel_body.addWidget(table, 1)
        export_btn.clicked.connect(self.export_log)
        clear_btn.clicked.connect(self.clear_log)

    def _mount_tab_bar(self):
        opens = list(self._open_tabs)
        bar = QTabBar()
        bar.setObjectName("SuiteEditorTabs")
        bar.setDrawBase(False)
        bar.setExpanding(False)
        bar.setDocumentMode(True)
        bar.setTabsClosable(False)
        bar.setMovable(True)
        for feat in opens:
            idx = bar.addTab(self._tab_title(feat))
            bar.setTabToolTip(idx, FEATURE_TITLES.get(feat, feat))
            bar.setTabData(idx, feat)
            close_btn = QToolButton(bar)
            close_btn.setObjectName("SuiteTabClose")
            close_btn.setAutoRaise(True)
            close_btn.setFixedSize(18, 18)
            close_btn.setIconSize(QSize(12, 12))
            close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            close_btn.setToolTip("Close tab")
            close_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            # VS Code–style close: crisp glyph + soft hover pill (QSS)
            codicons.set_button(
                close_btn, "close", color=vscode_theme.TEXT_DIM, size=12)
            bar.setTabButton(idx, QTabBar.ButtonPosition.RightSide, close_btn)

            def _close_feat(_checked=False, key=feat):
                for j in range(bar.count()):
                    if bar.tabData(j) == key:
                        bar.tabCloseRequested.emit(j)
                        break

            close_btn.clicked.connect(_close_feat)
        if self._active_feature in opens:
            bar.setCurrentIndex(opens.index(self._active_feature))
        elif opens:
            bar.setCurrentIndex(len(opens) - 1)

        def _changed(idx: int):
            if self._tab_guard or idx < 0:
                return
            feat = bar.tabData(idx)
            if feat:
                self._activate_feature(str(feat))

        def _close(idx: int):
            if idx < 0 or idx >= bar.count():
                return
            feat = bar.tabData(idx)
            if not feat:
                return
            self._close_feature_tab(str(feat))

        def _moved(_from: int, _to: int):
            order = []
            for i in range(bar.count()):
                d = bar.tabData(i)
                if d:
                    order.append(str(d))
            if order:
                self._open_tabs = order

        bar.currentChanged.connect(_changed)
        bar.tabCloseRequested.connect(_close)
        bar.tabMoved.connect(_moved)
        self._tab_bar = bar

        # Tab strip only — document path / Save / Node live in Side Bar + status.
        host = QWidget()
        host.setObjectName("SuiteEditorTabHost")
        row = QHBoxLayout(host)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(0)
        row.addWidget(bar, 1)
        self._wb.set_editor_tabs(host)
        self._ensure_status_chrome()
        self._refresh_doc_chrome()

    def _ensure_status_chrome(self):
        """Compact document/node/save chip on the status bar (not the tab row)."""
        bar = self.statusBar()
        if bar is None:
            return
        widgets = (self.dirty_label, self.path_label, self.node_label,
                   *self._doc_btns)
        if getattr(self, "_status_chrome_mounted", False):
            # Keep widgets on the status bar across tab remounts.
            for w in widgets:
                if w.parent() is not bar:
                    bar.addPermanentWidget(w)
            return
        self._status_chrome_mounted = True
        self.path_label.setMaximumWidth(220)
        for w in widgets:
            w.setParent(bar)
            bar.addPermanentWidget(w)

    def _canonical_feature(self, feature: str) -> str:
        return _TAB_CANONICAL.get(feature, feature)

    def _open_feature_tab(self, feature: str, *, activate: bool = True):
        route = FEATURE_ROUTE.get(feature)
        if not route:
            return
        tab_key = self._canonical_feature(feature)
        if tab_key not in FEATURE_ROUTE:
            tab_key = feature
        if tab_key not in self._open_tabs:
            self._open_tabs.append(tab_key)
        if activate:
            self._active_feature = feature
        self._mount_tab_bar()
        self._activate_feature(feature)
        workspace, _ = route
        sb = self._sidebars.get(workspace)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                # Highlight sidebar leaf (eds_check etc.) even if tab is eds_dict
                leaf = feature if feature in (
                    getattr(sb, "feature_keys", None) or []) else tab_key
                sb.select_section(leaf)
            except Exception:
                pass

    def _close_feature_tab(self, feature: str):
        tab_key = self._canonical_feature(feature)
        if tab_key in self._open_tabs:
            self._open_tabs.remove(tab_key)
        if not self._open_tabs:
            self._open_feature_tab("eds_dict", activate=True)
            return
        nxt = (
            self._active_feature
            if self._canonical_feature(self._active_feature) in self._open_tabs
            else self._open_tabs[-1])
        self._mount_tab_bar()
        self._activate_feature(nxt)

    def _activate_feature(self, feature: str):
        route = FEATURE_ROUTE.get(feature)
        if not route:
            return
        workspace, _idx = route
        self._active_feature = feature
        wi = self._workspace_stack_index.get(workspace)
        if wi is not None and self.stack.currentIndex() != wi:
            self.stack.setCurrentIndex(wi)
        outer = self._workspace_stacks.get(workspace)
        if outer is not None:
            apply = getattr(outer, "_apply_feature", None)
            if callable(apply):
                apply(feature)
            else:
                fmap = getattr(outer, "_feature_index", {}) or {}
                si = fmap.get(feature, 0)
                if 0 <= si < outer.count():
                    outer.setCurrentIndex(si)
        tab_key = self._canonical_feature(feature)
        if self._tab_bar is not None:
            self._tab_guard = True
            try:
                for i in range(self._tab_bar.count()):
                    if self._tab_bar.tabData(i) == tab_key:
                        self._tab_bar.setCurrentIndex(i)
                        break
            finally:
                self._tab_guard = False
        sb = self._sidebars.get(workspace)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                keys = getattr(sb, "feature_keys", None) or []
                sb.select_section(feature if feature in keys else tab_key)
            except Exception:
                pass
        plugin_shell.set_status(
            self, FEATURE_TITLES.get(feature, feature), 1200)

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
            if len(pdu) > 48:
                hex_str += " ..."
        else:
            hex_str = str(pdu)
        idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
        colors = {
            "TX": QColor(vscode_theme.TX),
            "RX": QColor(vscode_theme.RX),
            "ERR": QColor(vscode_theme.ERR),
            "SYS": QColor(vscode_theme.TEXT_MUTED),
        }
        c = colors.get(color or direction, QColor(vscode_theme.TEXT_MUTED))
        row = self._log_table.rowCount()
        self._log_table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, hex_str, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            self._log_table.setItem(row, col, item)
        while self._log_table.rowCount() > 2000:
            self._log_table.removeRow(0)
        self._log_table.scrollToBottom()
        if isinstance(can_id, int):
            plugin_shell.set_status(
                self, "%s 0x%X  %s" % (direction, can_id, (note or "")[:48]), 0)
        elif note:
            plugin_shell.set_status(self, str(note)[:64], 0)

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
            "canopen_suite_log.csv")
        if path:
            plugin_shell.set_status(self, "Exported %s" % path, 4000)

    def retranslate(self):
        self.setWindowTitle(i18n.t("CANopen Suite"))
        if self._wb is not None and callable(getattr(self._wb, "retranslate", None)):
            self._wb.retranslate()

    def goto_page(self, key: str):
        if key == "log":
            self._wb.set_panel_visible(True)
            return
        aliases = {
            "eds_editor": "eds_dict",
            "object_dict": "od",
            "eds": "eds_dict",
            "network": "network_scan",
            "device": "od",
            "setup": "eds_dict",
            "project": "eds_dict",
            "library": "profiles",
            "trace": "monitor",
            "code": "eds_codegen",
        }
        key = aliases.get(key, key)
        if key in FEATURE_ROUTE:
            workspace = FEATURE_ROUTE[key][0]
            self._switch_activity(workspace)
            self._open_feature_tab(key, activate=True)
        self._persist()

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        activity = ""
        if self._wb is not None:
            activity = self._wb.current_page() or ""
        state_store.save_state(PLUGIN_ID, {
            "node_id": self.session.node_id,
            "eds_path": self.session.eds_path,
            "nav_page": self._canonical_feature(
                self._active_feature or "eds_dict"),
            "activity": activity,
            "open_tabs": list(self._open_tabs),
            "geometry_hex": geo,
            "sidebar_visible": self._wb.is_sidebar_visible(),
            "panel_visible": self._wb.is_panel_visible(),
            "splitter_v": self._wb.v_splitter.sizes(),
            "last_project": self.session.project_root or "",
            "recent_projects": list(self._recent_projects),
            "recent_eds": list(self._recent_eds),
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            if "node_id" in saved:
                self.session.set_node_id(int(saved["node_id"]))
            self._recent_projects = [
                p for p in (saved.get("recent_projects") or []) if p][:MAX_RECENT]
            self._recent_eds = [
                p for p in (saved.get("recent_eds") or []) if p][:MAX_RECENT]
            last_proj = saved.get("last_project") or ""
            eds = saved.get("eds_path") or ""
            if last_proj and can_project.is_project_dir(last_proj):
                try:
                    manifest = can_project.load_manifest(last_proj)
                    self._project_manifest = manifest
                    self.session.set_project(last_proj, manifest.name)
                    self.session.set_node_id(manifest.node_id)
                    resolved = can_project.resolve_eds_path(manifest)
                    if resolved:
                        self.session.load_eds(resolved)
                        self._remember_eds(resolved)
                    elif eds and os.path.isfile(eds):
                        self.session.load_eds(eds)
                        self._remember_eds(eds)
                except (OSError, ValueError, TypeError):
                    if eds and os.path.isfile(eds):
                        self.session.load_eds(eds)
                        self._remember_eds(eds)
            elif eds and os.path.isfile(eds):
                self.session.load_eds(eds)
                self._remember_eds(eds)
            sizes = saved.get("splitter_v")
            if sizes and isinstance(sizes, list) and len(sizes) == 2:
                if saved.get("panel_visible"):
                    self._wb.v_splitter.setSizes(
                        [int(sizes[0]), int(sizes[1])])
            self._open_tabs = normalize_open_tabs(saved.get("open_tabs"))
            if saved.get("panel_visible"):
                self._wb.set_panel_visible(True)
            if saved.get("sidebar_visible") is False:
                self._wb.set_sidebar_visible(False)
            self._fill_recent_projects_menu()
            self._fill_recent_eds_menu()
            self._fill_project_eds_menu()
            sb = self._sidebars.get("eds")
            refresh = getattr(sb, "refresh_eds", None) or getattr(
                sb, "rebuild", None)
            if callable(refresh):
                refresh()
        except (TypeError, ValueError, OSError):
            pass

    def closeEvent(self, event):
        self._persist()
        super().closeEvent(event)

    def shutdown(self):
        self._persist()
        self.session.shutdown()
