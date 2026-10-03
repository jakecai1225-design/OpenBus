# -*- coding: utf-8 -*-
"""AppShell — DBC Studio (four pillars + File menu + Context Next).

Activities: Edit / Analyze / Integrate / Deliver.
DBC files live under File; sidebar leaves open as closable editor tabs
on the main chrome row (VS Code style), same contract as CANopen.
"""

from __future__ import annotations

import os
import time
from typing import Optional

from PyQt6.QtCore import QSize, Qt
from PyQt6.QtGui import QColor, QFontMetrics, QKeySequence
from PyQt6.QtWidgets import (
    QCheckBox,
    QFileDialog,
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

from _shared import (
    codicons, dbc_picker, plugin_shell, state_store, suite_chrome,
    vscode_theme,
)
from document import DbcDocument
from pages import _ui

PLUGIN_ID = "dbc-studio"
MAX_RECENT = 12

NAV_PAGES = [
    ("edit", "Edit"),
    ("analyze", "Analyze"),
    ("integrate", "Integrate"),
    ("deliver", "Deliver"),
]

FEATURE_ROUTE = {
    "editor": ("edit", 0),
    "valuetables": ("edit", 1),
    "attributes": ("edit", 2),
    "matrix": ("analyze", 0),
    "timing": ("analyze", 1),
    "validate": ("analyze", 2),
    "compare": ("integrate", 0),
    "merge": ("integrate", 1),
    "export": ("deliver", 0),
    "library": ("deliver", 1),
    # Activity keys → default leaf
    "edit": ("edit", 0),
    "analyze": ("analyze", 0),
    "integrate": ("integrate", 0),
    "deliver": ("deliver", 0),
}

FEATURE_TITLES = {
    "editor": "Messages",
    "valuetables": "Value tables",
    "attributes": "Attributes",
    "matrix": "Matrix",
    "timing": "Timing",
    "validate": "Validate",
    "compare": "Compare",
    "merge": "Merge",
    "export": "Export",
    "library": "Library",
}

_WORKSPACE_DEFAULT = {
    "edit": "editor",
    "analyze": "matrix",
    "integrate": "compare",
    "deliver": "export",
}

_PAGE_ALIASES = {
    "value_tables": "valuetables",
    "value-tables": "valuetables",
    "attr": "attributes",
    "lint": "validate",
    "diff": "compare",
    "codegen": "export",
}

_ACTIVITY_KEYS = frozenset(k for k, _ in NAV_PAGES)
_LEAF_KEYS = frozenset(FEATURE_TITLES.keys())


def _is_leaf_feature(feature: str) -> bool:
    return feature in _LEAF_KEYS and feature in FEATURE_ROUTE


def normalize_open_tabs(ot) -> list[str]:
    """Accept a list (or legacy junk) and keep known leaf feature keys only."""
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
        key = _PAGE_ALIASES.get(x, x)
        if not _is_leaf_feature(key):
            continue
        if key not in restored:
            restored.append(key)
    return restored


class AppShell(QMainWindow):
    def __init__(self, context, start_page: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("DBC Studio")
        self.setMinimumSize(1100, 720)
        self.resize(1200, 800)

        self._context = context
        self.document = DbcDocument()
        self._log_buffer: list = []
        self._recent: list = []
        self._favorites: list = []
        self._pages = {}
        self._workspace_stacks: dict = {}
        self._workspace_features: dict = {}
        self._workspace_stack_index: dict[str, int] = {}
        self._sidebars: dict = {}
        self._active_feature = "editor"
        self._open_tabs: list[str] = []
        self._tab_bar: Optional[QTabBar] = None
        self._tab_guard = False
        self._lint_before_save = True
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
            self, NAV_PAGES, title="DBC Studio", panel_title="OUTPUT",
            panel_visible=False, sidebar_visible=True,
            side_bar_enabled=True, side_bar_visible=True, lock_activity=True,
            side_bar_width=220)
        _ui.apply_dbc_chrome(self)
        self.stack = self._wb.stack

        self._init_document_controls()
        self._build_menubar()
        self._build_output_panel()

        from pages import (
            attributes, compare, editor, export, library, matrix, merge,
            timing, validate, value_tables, workspace_sidebar,
        )

        self._pages["editor"] = editor.build(self, self.document, self.log)
        self._pages["valuetables"] = value_tables.build(
            self, self.document, self.log)
        self._pages["attributes"] = attributes.build(
            self, self.document, self.log)
        self._pages["matrix"] = matrix.build(self, self.document, self.log)
        self._pages["timing"] = timing.build(self, self.document, self.log)
        self._pages["validate"] = validate.build(self, self.document, self.log)
        self._pages["compare"] = compare.build(self, self.document, self.log)
        self._pages["merge"] = merge.build(self, self.document, self.log)
        self._pages["export"] = export.build(self, self.document, self.log)
        self._pages["library"] = library.build(self, self.document, self.log)

        self._sidebars["edit"] = workspace_sidebar.build_edit_sidebar(self)
        self._sidebars["analyze"] = workspace_sidebar.build_analyze_sidebar(self)
        self._sidebars["integrate"] = workspace_sidebar.build_integrate_sidebar(
            self)
        self._sidebars["deliver"] = workspace_sidebar.build_deliver_sidebar(self)

        self._add_workspace("edit", [
            ("editor", self._pages["editor"]),
            ("valuetables", self._pages["valuetables"]),
            ("attributes", self._pages["attributes"]),
        ])
        self._add_workspace("analyze", [
            ("matrix", self._pages["matrix"]),
            ("timing", self._pages["timing"]),
            ("validate", self._pages["validate"]),
        ])
        self._add_workspace("integrate", [
            ("compare", self._pages["compare"]),
            ("merge", self._pages["merge"]),
        ])
        self._add_workspace("deliver", [
            ("export", self._pages["export"]),
            ("library", self._pages["library"]),
        ])

        self.document.on_changed(self._on_document_changed)

        saved = state_store.load_state(PLUGIN_ID, default={}) or {}
        self._restore_state(saved)
        goto = state_store.load_state(PLUGIN_ID, "goto.json") or {}
        page = start_page or goto.get("start_page") or saved.get("nav_page")
        if goto:
            state_store.clear_state(PLUGIN_ID, "goto.json")
        page = _PAGE_ALIASES.get(page or "", page or "editor")
        if page in _ACTIVITY_KEYS and page not in _LEAF_KEYS:
            page = _WORKSPACE_DEFAULT.get(page, "editor")
        if not _is_leaf_feature(page):
            page = "editor"
        activity = FEATURE_ROUTE[page][0]
        self._switch_activity(activity)
        self.goto_page(page)

        self._wb.set_sidebar_visible(True)
        self._wb.set_panel_visible(False)

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
            self, "Ctrl+O", lambda: self.run_action("dbc.open"))
        plugin_shell.bind_shortcut(
            self, "Ctrl+S", lambda: self.run_action("dbc.save"))
        plugin_shell.bind_shortcut(
            self, "Ctrl+Shift+S", lambda: self.run_action("dbc.save_as"))
        plugin_shell.bind_shortcut(
            self, "Ctrl+N", lambda: self.run_action("dbc.new"))
        plugin_shell.bind_shortcut(self, "Ctrl+Z", self.undo_edit)
        plugin_shell.bind_shortcut(self, "Ctrl+Y", self.redo_edit)
        plugin_shell.bind_shortcut(self, "Ctrl+Shift+Z", self.redo_edit)

        self.log("SYS", "DBC Studio ready")
        self._on_document_changed()
        self._ensure_status_chrome()
        self._sync_next_hint()

    # ------------------------------------------------------------------
    def _init_document_controls(self):
        self.path_label = QLabel("(unsaved)")
        self.path_label.setObjectName("SuiteDocPath")
        self.path_label.setMinimumWidth(80)
        self.path_label.setMaximumWidth(220)
        self.path_label.setToolTip("Active DBC path")

        self.dirty_label = QLabel("")
        self.dirty_label.setObjectName("SuiteDirtyDot")
        self.dirty_label.setToolTip("Unsaved changes")

        self._doc_btns = []
        save_btn = _ui.icon_tool("save", "Save DBC (Ctrl+S)")
        save_btn.clicked.connect(lambda: self.run_action("dbc.save"))
        self._doc_btns.append(save_btn)

        self.next_btn = _ui.ghost_btn(
            "Next", "Suggested next step", "arrow-right")
        self.next_btn.setMaximumWidth(128)
        self.next_btn.clicked.connect(self._run_next_hint)
        self._doc_btns.append(self.next_btn)

        self.lint_gate_cb = QCheckBox("Lint on save")
        self.lint_gate_cb.setChecked(True)
        self.lint_gate_cb.setToolTip(
            "Ask before saving when lint reports errors")
        self.lint_gate_cb.toggled.connect(self._on_lint_gate_toggled)

    def _ensure_status_chrome(self):
        bar = self.statusBar()
        if bar is None:
            return
        widgets = (self.dirty_label, self.path_label, *self._doc_btns)
        if self._status_chrome_mounted:
            for w in widgets:
                if w.parent() is not bar:
                    bar.addPermanentWidget(w)
            return
        self._status_chrome_mounted = True
        for w in widgets:
            w.setParent(bar)
            bar.addPermanentWidget(w)

    def _tab_title(self, feature: str) -> str:
        return FEATURE_TITLES.get(feature, feature)

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
            close_btn.setFixedSize(20, 20)
            close_btn.setIconSize(QSize(14, 14))
            close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            close_btn.setToolTip("Close tab")
            close_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            codicons.set_button(
                close_btn, "close", color=vscode_theme.TEXT_DIM, size=14)
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

        host = QWidget()
        host.setObjectName("SuiteEditorTabHost")
        row = QHBoxLayout(host)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(0)
        row.addWidget(bar, 1)
        self._wb.set_editor_tabs(host)
        self._ensure_status_chrome()

    def _open_feature_tab(self, feature: str, *, activate: bool = True):
        if not _is_leaf_feature(feature):
            return
        route = FEATURE_ROUTE[feature]
        if feature not in self._open_tabs:
            self._open_tabs.append(feature)
        if activate:
            self._active_feature = feature
        self._mount_tab_bar()
        self._activate_feature(feature)
        workspace, _ = route
        sb = self._sidebars.get(workspace)
        if sb is not None and hasattr(sb, "select_section"):
            try:
                sb.select_section(feature)
            except Exception:
                pass

    def _close_feature_tab(self, feature: str):
        if feature in self._open_tabs:
            self._open_tabs.remove(feature)
        if not self._open_tabs:
            self._open_feature_tab("editor", activate=True)
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
        _act(m_file, "&New DBC…",
             lambda: self.run_action("dbc.new"), "Ctrl+N")
        _act(m_file, "&Open DBC…",
             lambda: self.run_action("dbc.open"), "Ctrl+O")
        _act(m_file, "Open from &Workspace…",
             lambda: self.run_action("dbc.open_workspace"))
        _act(m_file, "&Save",
             lambda: self.run_action("dbc.save"), "Ctrl+S")
        _act(m_file, "Save &As…",
             lambda: self.run_action("dbc.save_as"), "Ctrl+Shift+S")
        _act(m_file, "&Reload",
             lambda: self.run_action("dbc.reload"))
        self._recent_menu = m_file.addMenu("Recent &DBCs")
        self._fill_recent_menu()
        m_file.addSeparator()
        _act(m_file, "E&xit", self.close)

        m_edit = bar.addMenu("&Edit")
        _act(m_edit, "&Undo", self.undo_edit, "Ctrl+Z")
        _act(m_edit, "&Redo", self.redo_edit, "Ctrl+Y")
        m_edit.addSeparator()
        act_lint = m_edit.addAction("Lint on &save")
        act_lint.setCheckable(True)
        act_lint.setChecked(self._lint_before_save)
        act_lint.toggled.connect(self._on_lint_gate_toggled)
        self._lint_menu_act = act_lint

        m_view = bar.addMenu("&View")
        for key, title in NAV_PAGES:
            _act(m_view, title,
                 lambda _c=False, k=key: self._on_activity_clicked(k))
        m_view.addSeparator()
        for feat, title in (
                ("editor", "&Messages"),
                ("valuetables", "&Value tables"),
                ("attributes", "&Attributes"),
                ("validate", "&Validate"),
                ("export", "&Export"),
                ("library", "&Library")):
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
        _act(m_help, "&About DBC Studio", self._menu_about)

        self._menubar_trailing = suite_chrome.attach_layout_toggles_to_menubar(
            self, self._wb)

    def _fill_recent_menu(self):
        menu = getattr(self, "_recent_menu", None)
        if menu is None:
            return
        menu.clear()
        shown = 0
        for path in self._recent:
            if not path or not os.path.isfile(path):
                continue
            a = menu.addAction(os.path.basename(path))
            a.setToolTip(path)
            a.triggered.connect(
                lambda _c=False, p=path: self._load_path(p))
            shown += 1
        if not shown:
            a = menu.addAction("(empty)")
            a.setEnabled(False)

    def _menu_about(self):
        QMessageBox.information(
            self, "DBC Studio",
            "DBC Studio — Edit / Analyze / Integrate / Deliver\n"
            "CANdb++-style workbench inside OpenBus.")

    def run_action(self, name: str, **kw):
        handlers = {
            "dbc.new": self.new_dbc,
            "dbc.open": self.open_dbc,
            "dbc.open_workspace": self.open_workspace_dbc,
            "dbc.save": self.save_dbc,
            "dbc.save_as": self.save_dbc_as,
            "dbc.reload": self.reload_dbc,
            "view.editor": lambda: self.goto_page("editor"),
            "view.validate": lambda: self.goto_page("validate"),
            "view.export": lambda: self.goto_page("export"),
            "view.library": lambda: self.goto_page("library"),
            "view.matrix": lambda: self.goto_page("matrix"),
            "view.valuetables": lambda: self.goto_value_tables(
                kw.get("table")),
            "view.attributes": lambda: self.goto_attributes(
                kw.get("can_id"), kw.get("signal")),
            "editor.focus": lambda: self.goto_editor_target(
                kw.get("can_id"), kw.get("signal")),
        }
        fn = handlers.get(name)
        if fn is None:
            plugin_shell.set_status(self, "Unknown action: %s" % name, 2000)
            return
        fn()

    def _run_next_hint(self):
        label, action, kw = self._next_action
        if not action:
            return
        self.run_action(action, **(kw or {}))
        if action in ("view.export", "view.library", "view.validate"):
            if action != "view.validate":
                self.document.advance_next_hint()
        self._sync_next_hint()
        plugin_shell.set_status(self, label or action, 2000)

    def _sync_next_hint(self):
        btn = getattr(self, "next_btn", None)
        if btn is None:
            return
        label, action, kw = self.document.next_hint()
        self._next_action = (label, action, kw or {})
        btn.setText(label or "Next")
        btn.setEnabled(bool(action))
        btn.setToolTip("Next: %s" % (label or "(none)"))

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
            idx = fmap.get(key, 0)
            stack.setCurrentIndex(idx)

        stack._apply_feature = apply  # type: ignore[attr-defined]
        return stack

    def _switch_activity(self, key: str):
        if key not in dict(NAV_PAGES):
            return
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
        if not self._open_tabs:
            default = _WORKSPACE_DEFAULT.get(key, "editor")
            self._open_feature_tab(default, activate=True)
        self._sync_next_hint()
        self._persist()

    def _on_workbench_page(self, key: str):
        # Presence also tells suite_chrome not to set_editor_title on highlight
        # (that would wipe the editor tab strip).
        self._on_activity_clicked(key)

    def _activate_feature(self, feature: str):
        if not _is_leaf_feature(feature):
            return
        route = FEATURE_ROUTE[feature]
        workspace, _ = route
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
                idx = getattr(outer, "_feature_index", {}).get(feature, 0)
                outer.setCurrentIndex(idx)
        if self._tab_bar is not None:
            self._tab_guard = True
            try:
                for i in range(self._tab_bar.count()):
                    if self._tab_bar.tabData(i) == feature:
                        self._tab_bar.setCurrentIndex(i)
                        break
            finally:
                self._tab_guard = False
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
        plugin_shell.set_status(
            self, FEATURE_TITLES.get(feature, feature), 1200)
        # Re-sync leaf UIs with document focus (Messages ↔ Value tables / Attributes).
        page = self._pages.get(feature)
        refresh = getattr(page, "refresh", None) if page is not None else None
        if callable(refresh):
            try:
                refresh()
            except Exception:
                pass

    def goto_page(self, key: str):
        key = _PAGE_ALIASES.get(key, key)
        if key in _ACTIVITY_KEYS and key not in _LEAF_KEYS:
            key = _WORKSPACE_DEFAULT.get(key, "editor")
        if not _is_leaf_feature(key):
            key = "editor"
        workspace = FEATURE_ROUTE[key][0]
        self._switch_activity(workspace)
        self._open_feature_tab(key, activate=True)
        self._sync_next_hint()
        self._persist()

    def goto_editor_target(self, can_id=None, signal=None):
        self.document.set_focus(can_id, signal or "")
        self.goto_page("editor")
        api = self._pages.get("editor")
        if api is not None and hasattr(api, "select_target"):
            api.select_target(can_id, signal)

    def goto_value_tables(self, table_name: Optional[str] = None):
        self.goto_page("valuetables")
        api = self._pages.get("valuetables")
        if api is not None and table_name and hasattr(api, "select_table"):
            api.select_table(table_name)

    def goto_attributes(self, can_id=None, signal=None):
        if can_id is not None:
            self.document.set_focus(can_id, signal or "")
        self.goto_page("attributes")
        api = self._pages.get("attributes")
        refresh = getattr(api, "refresh", None) if api is not None else None
        if callable(refresh):
            refresh()

    # ------------------------------------------------------------------
    def _build_output_panel(self):
        pause = QCheckBox("Pause")
        pause.setToolTip("Hold new log lines until unchecked")
        pause.setFixedHeight(_ui.CTRL_H)
        self.log_pause = pause
        self._wb.panel_tools.addWidget(pause)
        self._wb.panel_tools.addStretch(1)

        export_btn = _ui.ghost_btn("Export", "Export OUTPUT as CSV", "export")
        clear_btn = _ui.ghost_btn("Clear", "Clear OUTPUT", "clear")
        self._wb.panel_tools.addWidget(export_btn)
        self._wb.panel_tools.addWidget(clear_btn)

        table = suite_chrome.make_output_table()
        self.log_table = table
        self._wb.panel_body.addWidget(table, 1)
        export_btn.clicked.connect(self._export_log)
        clear_btn.clicked.connect(self.clear_log)

    def log(self, source: str, message: str, color: str | None = None):
        plugin_shell.set_status(
            self, "%s  %s" % (source, (message or "")[:72]), 4000)
        if getattr(self, "log_pause", None) is not None and self.log_pause.isChecked():
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
        width = max(120, self.path_label.width() or 200)
        if fm.horizontalAdvance(text) <= width:
            return text
        base = os.path.basename(text) if text != "(unsaved)" else text
        short = "…/" + base if base and base != text else text
        return fm.elidedText(short, Qt.TextElideMode.ElideLeft, width)

    def _on_document_changed(self):
        label = self.document.strip_label()
        self.path_label.setText(self._elide_path(label))
        self.path_label.setToolTip(label)
        self.dirty_label.setText("●" if self.document.dirty else "")
        title = "DBC Studio — %s" % self.document.display_name()
        if self.document.dirty:
            title += " *"
        self.setWindowTitle(title)
        self._sync_next_hint()
        self._persist()

    def _remember_path(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self._recent = [p for p in self._recent if os.path.normpath(p) != path]
        self._recent.insert(0, path)
        self._recent = self._recent[:MAX_RECENT]
        self._fill_recent_menu()
        self._persist()

    def new_dbc(self):
        if not self._confirm_discard():
            return
        self.document.new()
        self.log("SYS", "New empty DBC")
        plugin_shell.set_status(self, "New document", 2000)
        self.goto_page("editor")

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
        self.goto_page("editor")
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
        self._sync_next_hint()
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
        self._sync_next_hint()
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
        if hasattr(self, "lint_gate_cb"):
            self.lint_gate_cb.blockSignals(True)
            self.lint_gate_cb.setChecked(self._lint_before_save)
            self.lint_gate_cb.blockSignals(False)
        act = getattr(self, "_lint_menu_act", None)
        if act is not None:
            act.blockSignals(True)
            act.setChecked(self._lint_before_save)
            act.blockSignals(False)
        self._persist()

    def _warn_lint_before_save(self) -> bool:
        if not self._lint_before_save:
            return True
        try:
            from core.lint_engine import lint_dbc, load_rules
            findings = lint_dbc(self.document.db, load_rules())
            n_err = sum(1 for f in findings if f["severity"] == "error")
            self.document.mark_validated(n_err == 0, error_count=n_err)
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
        self._fill_recent_menu()
        self._persist()

    def _persist(self):
        geo = self.saveGeometry().toHex().data().decode("ascii")
        page = self._active_feature or "editor"
        state_store.save_state(PLUGIN_ID, {
            "last_path": self.document.path,
            "recent": list(self._recent),
            "favorites": list(self._favorites),
            "nav_page": page,
            "open_tabs": list(self._open_tabs),
            "geometry_hex": geo,
            "lint_before_save": bool(self._lint_before_save),
            "sidebar": self._wb.is_sidebar_visible() if self._wb else True,
            "panel": False,
        })

    def _restore_state(self, saved: dict):
        if not saved:
            return
        try:
            self._recent = [
                p for p in (saved.get("recent") or []) if p][:MAX_RECENT]
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
            self._wb.set_panel_visible(False)
            self._open_tabs = normalize_open_tabs(saved.get("open_tabs"))
            self._fill_recent_menu()
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
