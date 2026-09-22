# -*- coding: utf-8 -*-
"""Shared suite chrome — VS Code–style workbench (minimal vertical chrome).

Vertical layers (keep to two above the editor):
  1. One editor chrome row — page tabs OR page title + layout toggles
  2. Editor body
  3. Collapsible OUTPUT (optional)

Horizontal: Activity bar (hideable) | editor column.
No separate title bar — tabs own the top of the main window.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Optional

from PyQt6.QtCore import Qt, QSize
from PyQt6.QtWidgets import (
    QButtonGroup,
    QHBoxLayout,
    QLabel,
    QSizePolicy,
    QSplitter,
    QStackedWidget,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, plugin_shell, vscode_theme


# Activity-bar tooltips (Ctrl+N appended when buttons are built).
ACTIVITY_TIPS = {
    "diagnose": "Diagnose — send UDS services, DID, DTC, flash",
    "scan": "Scan — discover responding ECUs on the bus",
    "batch": "Batch — run a scripted request sequence",
    "security": "Security — observational SecurityAccess audit",
    "profiles": "Profiles — save / load ECU connection presets",
    "setup": "Setup — TX/RX IDs, timing, identify",
    "com": "COM — I-PDU layout, live decode, pack and send",
    "project": "Project — workspace files, library starters",
    "config": "Config — BSW modules, editor, spec, SWC",
    "bus": "Bus — System extract, NM, E2E, SecOC",
    "system": "System — ARXML editor: tree, validate, export DBC",
    "secoc": "SecOC — freshness value and truncated MAC",
    "topology": "Topology — slaves and AL state",
    "coe": "CoE — object dictionary and SDO mailbox",
    "esi": "ESI — EtherCAT slave description editor",
    "dc": "DC — cycle, shift and cable delay",
    "frames": "Frames — EtherCAT datagram and mailbox decode",
    "od": "OD — live object dictionary and SDO",
    "eds": "EDS — Dictionary / Device / Check (CANeds)",
    "library": "Library — starters, profiles, recent files",
    "editor": "Editor — object dictionary and device info",
    "matrix": "Matrix — communications Tx/Rx grid",
    "valuetables": "Value Tables — named VAL_TABLE_ library",
    "attributes": "Attributes — BA_DEF_ / BA_ values",
    "validate": "Validate — findings, analysis, compare, merge, export",
    "timing": "Analysis — coverage and PDO load estimate",
    "compare": "Compare — diff two description files",
    "merge": "Merge — combine DBC files",
    "export": "Export — EDS / DCF / HTML / CSV / XDD",
    "pdo": "PDO Map — file-layer RPDO / TPDO mapping",
}


@dataclass
class WorkbenchParts:
    """Handles returned by build_workbench()."""

    activity: QWidget
    stack: QStackedWidget
    page_index: dict
    chrome: QWidget
    chrome_slot: QHBoxLayout
    panel: QWidget
    panel_body: QVBoxLayout
    panel_tools: QHBoxLayout
    v_splitter: QSplitter
    btn_sidebar: QToolButton
    btn_panel: QToolButton
    btn_maximize: QToolButton
    set_sidebar_visible: Callable[[bool], None]
    set_panel_visible: Callable[[bool], None]
    set_maximized: Callable[[bool], None]
    is_sidebar_visible: Callable[[], bool]
    is_panel_visible: Callable[[], bool]
    is_maximized: Callable[[], bool]
    collapse_panel: Callable[[], None]
    expand_panel: Callable[[], None]
    goto_page: Callable[[str], None]
    current_page: Callable[[], str]
    set_editor_tabs: Callable[[QWidget], None]
    set_editor_title: Callable[[str], None]
    editor_layout: QVBoxLayout
    # Compat
    title_bar: Optional[QWidget] = None
    title_trailing: Optional[QHBoxLayout] = None
    nav: Optional[QWidget] = None
    h_splitter: Optional[QSplitter] = None
    _activity_btns: dict = field(default_factory=dict, repr=False)


def build_shell(window, nav_pages: list[tuple[str, str]], *, nav_width: int = 148):
    """Legacy: wide text nav + stack. Prefer build_workbench."""
    from PyQt6.QtWidgets import QListWidget, QListWidgetItem

    vscode_theme.apply(window)
    plugin_shell.attach_status_bar(window, "Ready")

    central = QWidget()
    window.setCentralWidget(central)
    root = QHBoxLayout(central)
    root.setContentsMargins(0, 0, 0, 0)
    root.setSpacing(0)

    nav = QListWidget()
    nav.setObjectName("SuiteNav")
    nav.setFixedWidth(nav_width)
    nav.setIconSize(QSize(16, 16))
    nav.setFocusPolicy(Qt.FocusPolicy.NoFocus)
    nav.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
    page_index = {}
    for i, (key, title) in enumerate(nav_pages):
        item = QListWidgetItem(title)
        item.setData(Qt.ItemDataRole.UserRole, key)
        codicons.set_nav_item(item, key, vscode_theme.TEXT)
        nav.addItem(item)
        page_index[key] = i
    root.addWidget(nav)

    content = QWidget()
    content.setObjectName("SuiteContent")
    cl = QVBoxLayout(content)
    cl.setContentsMargins(0, 0, 0, 0)
    cl.setSpacing(0)
    stack = QStackedWidget()
    cl.addWidget(stack, 1)
    root.addWidget(content, 1)

    return nav, stack, page_index


def build_workbench(
    window,
    nav_pages: list[tuple[str, str]],
    *,
    nav_width: int = 48,
    title: Optional[str] = None,
    panel_title: str = "OUTPUT",
    panel_visible: bool = True,
    sidebar_visible: bool = True,
) -> WorkbenchParts:
    """Attach a VS Code–style workbench — one chrome row, not stacked shells."""
    vscode_theme.apply(window)
    plugin_shell.attach_status_bar(window, "Ready")

    state = {
        "sidebar": sidebar_visible,
        "panel": panel_visible,
        "maximized": False,
        "panel_height": 160,
        "saved_sidebar": sidebar_visible,
        "saved_panel": panel_visible,
        "page": nav_pages[0][0] if nav_pages else "",
    }

    central = QWidget()
    window.setCentralWidget(central)
    root = QHBoxLayout(central)
    root.setContentsMargins(0, 0, 0, 0)
    root.setSpacing(0)

    # ---- Activity bar ----
    activity = QWidget()
    activity.setObjectName("SuiteActivityBar")
    activity.setFixedWidth(max(40, int(nav_width)))
    act_l = QVBoxLayout(activity)
    act_l.setContentsMargins(0, 4, 0, 4)
    act_l.setSpacing(2)

    page_index = {}
    activity_btns: dict[str, QToolButton] = {}
    btn_group = QButtonGroup(activity)
    btn_group.setExclusive(True)

    stack = QStackedWidget()
    stack.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

    def _select_page(key: str):
        idx = page_index.get(key)
        if idx is None:
            return
        state["page"] = key
        stack.setCurrentIndex(idx)
        btn = activity_btns.get(key)
        if btn is not None:
            btn.blockSignals(True)
            btn.setChecked(True)
            btn.blockSignals(False)
            for k, b in activity_btns.items():
                color = vscode_theme.ACCENT if k == key else vscode_theme.TEXT_DIM
                icon_key = nav_pages[page_index[k]][0]
                codicons.set_button(b, icon_key, color=color, size=20)
        # Default chrome title when page has no custom tabs.
        # Window._on_workbench_page remounts SuiteEditorTabs when the page owns them.
        title_map = dict(nav_pages)
        tab_pages = {
            "diagnose", "com", "system", "topology", "frames", "esi",
            "network", "eds", "library",
        }
        if key not in tab_pages:
            set_editor_title(title_map.get(key, key))

    footer_keys = {"setup", "settings"}
    main_pages = [(k, t) for k, t in nav_pages if k not in footer_keys]
    foot_pages = [(k, t) for k, t in nav_pages if k in footer_keys]

    def _add_activity_btn(i: int, key: str, page_title: str):
        page_index[key] = i
        b = QToolButton()
        b.setObjectName("ActivityBtn")
        b.setCheckable(True)
        b.setAutoRaise(True)
        b.setCursor(Qt.CursorShape.PointingHandCursor)
        b.setFixedSize(40, 40)
        b.setIconSize(QSize(20, 20))
        tip = ACTIVITY_TIPS.get(key, page_title)
        tip = "%s  (Ctrl+%d)" % (tip, i + 1)
        b.setToolTip(tip)
        codicons.set_button(b, key, color=vscode_theme.TEXT_DIM, size=20)
        btn_group.addButton(b, i)
        activity_btns[key] = b

        def _on_click(_checked=False, k=key, btn=b):
            if state["page"] == k and btn.isChecked():
                if state["panel"]:
                    set_panel_visible(False)
                return
            goto = getattr(window, "goto_page", None)
            if callable(goto):
                goto(k)
            else:
                _select_page(k)

        b.clicked.connect(_on_click)
        return b

    for key, page_title in main_pages:
        idx = next(j for j, (k, _) in enumerate(nav_pages) if k == key)
        act_l.addWidget(_add_activity_btn(idx, key, page_title), 0, Qt.AlignmentFlag.AlignHCenter)
    act_l.addStretch(1)
    for key, page_title in foot_pages:
        idx = next(j for j, (k, _) in enumerate(nav_pages) if k == key)
        act_l.addWidget(_add_activity_btn(idx, key, page_title), 0, Qt.AlignmentFlag.AlignHCenter)
    root.addWidget(activity)

    # ---- Editor column: ONE chrome row + body/panel ----
    right = QWidget()
    right.setObjectName("SuiteContent")
    right_l = QVBoxLayout(right)
    right_l.setContentsMargins(0, 0, 0, 0)
    right_l.setSpacing(0)

    chrome = QWidget()
    chrome.setObjectName("SuiteEditorChrome")
    chrome.setFixedHeight(35)
    ch = QHBoxLayout(chrome)
    ch.setContentsMargins(0, 0, 4, 0)
    ch.setSpacing(0)

    chrome_slot_host = QWidget()
    chrome_slot_host.setObjectName("SuiteEditorTabHost")
    chrome_slot = QHBoxLayout(chrome_slot_host)
    chrome_slot.setContentsMargins(0, 0, 0, 0)
    chrome_slot.setSpacing(0)
    ch.addWidget(chrome_slot_host, 1)

    _chrome_title = QLabel(title or "")
    _chrome_title.setObjectName("SuiteEditorTitle")
    chrome_slot.addWidget(_chrome_title)
    _chrome_tabs: list[QWidget] = []

    def _clear_chrome_slot():
        while chrome_slot.count():
            item = chrome_slot.takeAt(0)
            w = item.widget()
            if w is not None:
                w.setParent(None)

    def set_editor_tabs(tab_bar: QWidget):
        """Mount a QTabBar into the single top chrome row (Diagnose etc.)."""
        _clear_chrome_slot()
        _chrome_tabs[:] = [tab_bar]
        chrome_slot.addWidget(tab_bar, 1)

    def set_editor_title(text: str):
        """Show a plain page title when the page has no editor tabs."""
        _clear_chrome_slot()
        _chrome_tabs.clear()
        _chrome_title = QLabel(text)
        _chrome_title.setObjectName("SuiteEditorTitle")
        chrome_slot.addWidget(_chrome_title)
        chrome_slot.addStretch(1)

    def _make_toggle(icon_name: str, tip: str) -> QToolButton:
        btn = QToolButton()
        btn.setObjectName("LayoutToggleBtn")
        btn.setCheckable(True)
        btn.setChecked(True)
        btn.setAutoRaise(True)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setFixedSize(28, 28)
        btn.setIconSize(QSize(16, 16))
        btn.setToolTip(tip)
        codicons.set_button(btn, icon_name, color=vscode_theme.TEXT_DIM, size=16)
        ch.addWidget(btn, 0, Qt.AlignmentFlag.AlignVCenter)
        return btn

    btn_sidebar = _make_toggle(
        "layout-sidebar-left", "Toggle Activity Bar (Ctrl+B)")
    btn_panel = _make_toggle("layout-panel", "Toggle Panel (Ctrl+J)")
    btn_maximize = _make_toggle(
        "layout-maximize", "Maximize Editor — hide activity bar & panel")
    btn_maximize.setChecked(False)

    right_l.addWidget(chrome)

    v_splitter = QSplitter(Qt.Orientation.Vertical)
    v_splitter.setObjectName("SuiteVSplitter")
    v_splitter.setHandleWidth(1)
    v_splitter.setChildrenCollapsible(True)
    v_splitter.addWidget(stack)

    panel = QWidget()
    panel.setObjectName("SuiteLogHost")
    panel.setMinimumHeight(0)
    pv = QVBoxLayout(panel)
    pv.setContentsMargins(0, 0, 0, 0)
    pv.setSpacing(0)

    panel_head = QWidget()
    panel_head.setObjectName("SuiteLogHeader")
    panel_head.setFixedHeight(26)
    ph = QHBoxLayout(panel_head)
    ph.setContentsMargins(6, 0, 2, 0)
    ph.setSpacing(4)
    panel_title_lab = QLabel(panel_title)
    panel_title_lab.setObjectName("SuiteToolbarTitle")
    panel_title_lab.setToolTip("OUTPUT — double-click to collapse (Ctrl+J)")
    ph.addWidget(panel_title_lab)

    # Single-row tools slot (ISO-TP / Export / Clear …) — keeps list tall
    panel_tools_host = QWidget()
    panel_tools = QHBoxLayout(panel_tools_host)
    panel_tools.setContentsMargins(0, 0, 0, 0)
    panel_tools.setSpacing(8)
    ph.addWidget(panel_tools_host, 1)

    collapse_btn = QToolButton()
    collapse_btn.setObjectName("LayoutToggleBtn")
    collapse_btn.setAutoRaise(True)
    collapse_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    collapse_btn.setFixedSize(22, 22)
    collapse_btn.setIconSize(QSize(12, 12))
    collapse_btn.setToolTip("Collapse Panel")
    codicons.set_button(collapse_btn, "chevron-down", color=vscode_theme.TEXT_DIM, size=12)
    ph.addWidget(collapse_btn)
    pv.addWidget(panel_head)

    panel_body_host = QWidget()
    panel_body = QVBoxLayout(panel_body_host)
    panel_body.setContentsMargins(0, 0, 0, 0)
    panel_body.setSpacing(0)
    pv.addWidget(panel_body_host, 1)

    v_splitter.addWidget(panel)
    v_splitter.setSizes([720, 160])
    v_splitter.setStretchFactor(0, 5)
    v_splitter.setStretchFactor(1, 1)
    right_l.addWidget(v_splitter, 1)
    root.addWidget(right, 1)

    # ---- Visibility ----
    def _apply_sidebar(visible: bool):
        state["sidebar"] = visible
        activity.setVisible(visible)
        btn_sidebar.blockSignals(True)
        btn_sidebar.setChecked(visible)
        btn_sidebar.blockSignals(False)

    def _apply_panel(visible: bool):
        state["panel"] = visible
        if visible:
            panel.show()
            sizes = v_splitter.sizes()
            if sum(sizes) > 0 and sizes[1] < 40:
                total = sum(sizes) or 800
                h = state["panel_height"]
                v_splitter.setSizes([max(200, total - h), h])
            codicons.set_button(
                collapse_btn, "chevron-down", color=vscode_theme.TEXT_DIM, size=12)
            collapse_btn.setToolTip("Collapse Panel")
        else:
            sizes = v_splitter.sizes()
            if sizes[1] > 40:
                state["panel_height"] = sizes[1]
            panel.hide()
            codicons.set_button(
                collapse_btn, "chevron-up", color=vscode_theme.TEXT_DIM, size=12)
            collapse_btn.setToolTip("Expand Panel")
        btn_panel.blockSignals(True)
        btn_panel.setChecked(visible)
        btn_panel.blockSignals(False)

    def _apply_maximize(maximized: bool):
        if maximized:
            if not state["maximized"]:
                state["saved_sidebar"] = state["sidebar"]
                state["saved_panel"] = state["panel"]
            state["maximized"] = True
            _apply_sidebar(False)
            _apply_panel(False)
        else:
            state["maximized"] = False
            _apply_sidebar(state["saved_sidebar"])
            _apply_panel(state["saved_panel"])
        btn_maximize.blockSignals(True)
        btn_maximize.setChecked(maximized)
        btn_maximize.blockSignals(False)

    def set_sidebar_visible(visible: bool):
        if state["maximized"] and visible:
            _apply_maximize(False)
            return
        _apply_sidebar(visible)
        if visible:
            state["maximized"] = False
            btn_maximize.blockSignals(True)
            btn_maximize.setChecked(False)
            btn_maximize.blockSignals(False)

    def set_panel_visible(visible: bool):
        if state["maximized"] and visible:
            _apply_maximize(False)
            return
        _apply_panel(visible)
        if visible:
            state["maximized"] = False
            btn_maximize.blockSignals(True)
            btn_maximize.setChecked(False)
            btn_maximize.blockSignals(False)

    def set_maximized(maximized: bool):
        _apply_maximize(maximized)

    def collapse_panel():
        set_panel_visible(False)

    def expand_panel():
        set_panel_visible(True)

    def goto_page(key: str):
        if key == "log":
            expand_panel()
            return
        _select_page(key)
        on_page_changed = getattr(window, "_on_workbench_page", None)
        if callable(on_page_changed):
            on_page_changed(key)

    btn_sidebar.toggled.connect(set_sidebar_visible)
    btn_panel.toggled.connect(set_panel_visible)
    btn_maximize.toggled.connect(set_maximized)
    collapse_btn.clicked.connect(lambda: set_panel_visible(not state["panel"]))
    panel_title_lab.mouseDoubleClickEvent = (  # type: ignore[method-assign]
        lambda _e: set_panel_visible(not state["panel"]))

    _apply_sidebar(sidebar_visible)
    _apply_panel(panel_visible)
    if nav_pages:
        _select_page(nav_pages[0][0])

    return WorkbenchParts(
        activity=activity,
        stack=stack,
        page_index=page_index,
        chrome=chrome,
        chrome_slot=chrome_slot,
        panel=panel,
        panel_body=panel_body,
        panel_tools=panel_tools,
        v_splitter=v_splitter,
        btn_sidebar=btn_sidebar,
        btn_panel=btn_panel,
        btn_maximize=btn_maximize,
        set_sidebar_visible=set_sidebar_visible,
        set_panel_visible=set_panel_visible,
        set_maximized=set_maximized,
        is_sidebar_visible=lambda: state["sidebar"],
        is_panel_visible=lambda: state["panel"],
        is_maximized=lambda: state["maximized"],
        collapse_panel=collapse_panel,
        expand_panel=expand_panel,
        goto_page=goto_page,
        current_page=lambda: state["page"],
        set_editor_tabs=set_editor_tabs,
        set_editor_title=set_editor_title,
        editor_layout=right_l,
        title_bar=chrome,
        title_trailing=chrome_slot,
        nav=activity,
        h_splitter=None,
        _activity_btns=activity_btns,
    )


def page_margins(layout, *, top: int = 12) -> None:
    layout.setContentsMargins(16, top, 16, 12)
    layout.setSpacing(12)


def bind_nav_shortcuts(window, nav_pages, goto_fn) -> None:
    for i in range(len(nav_pages)):
        plugin_shell.bind_shortcut(
            window, "Ctrl+%d" % (i + 1),
            lambda _=False, idx=i: goto_fn(nav_pages[idx][0]))


def make_toolbar(title: str | None = None):
    bar = QWidget()
    bar.setObjectName("SuiteToolbar")
    row = QHBoxLayout(bar)
    row.setContentsMargins(12, 6, 12, 6)
    row.setSpacing(8)
    if title:
        lab = QLabel(title.upper())
        lab.setObjectName("SuiteToolbarTitle")
        row.addWidget(lab)
    return bar, row


def make_log_host(title: str = "OUTPUT"):
    host = QWidget()
    host.setObjectName("SuiteLogHost")
    v = QVBoxLayout(host)
    v.setContentsMargins(8, 6, 8, 8)
    v.setSpacing(6)
    title_lab = QLabel(title)
    title_lab.setObjectName("SuiteToolbarTitle")
    v.addWidget(title_lab)
    return host, v


def make_output_table(parent=None):
    from PyQt6.QtWidgets import (
        QAbstractItemView,
        QHeaderView,
        QTableWidget,
    )

    table = QTableWidget(0, 5, parent)
    table.setObjectName("OutputTable")
    table.setHorizontalHeaderLabels(["Time", "Dir", "CAN ID", "PDU", "Note"])
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.setShowGrid(False)
    table.setAlternatingRowColors(True)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    table.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    hdr = table.horizontalHeader()
    hdr.setHighlightSections(False)
    hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    hdr.setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
    table.verticalHeader().setDefaultSectionSize(22)
    return table


def mount_compact_output(wb: WorkbenchHandles):
    """Icon-only Pause/Clear + plain-text terminal — maximize log space.

    Returns (pause_btn, clear_btn, terminal) where pause_btn.isChecked()
    means hold new lines.
    """
    from PyQt6.QtGui import QFont
    from PyQt6.QtWidgets import QFrame, QPlainTextEdit

    pause = QToolButton()
    pause.setObjectName("LayoutToggleBtn")
    pause.setCheckable(True)
    pause.setAutoRaise(True)
    pause.setCursor(Qt.CursorShape.PointingHandCursor)
    pause.setFixedSize(22, 22)
    pause.setIconSize(QSize(12, 12))
    pause.setToolTip("Pause log")
    try:
        codicons.set_button(
            pause, "stop", color=vscode_theme.TEXT_DIM, size=12)
    except Exception:
        pause.setText("||")

    clear = QToolButton()
    clear.setObjectName("LayoutToggleBtn")
    clear.setAutoRaise(True)
    clear.setCursor(Qt.CursorShape.PointingHandCursor)
    clear.setFixedSize(22, 22)
    clear.setIconSize(QSize(12, 12))
    clear.setToolTip("Clear")
    try:
        codicons.set_button(
            clear, "clear", color=vscode_theme.TEXT_DIM, size=12)
    except Exception:
        clear.setText("×")

    wb.panel_tools.addStretch(1)
    wb.panel_tools.addWidget(pause)
    wb.panel_tools.addWidget(clear)

    term = QPlainTextEdit()
    term.setObjectName("SuiteOutputTerminal")
    term.setReadOnly(True)
    term.setUndoRedoEnabled(False)
    term.setFrameShape(QFrame.Shape.NoFrame)
    term.setLineWrapMode(QPlainTextEdit.LineWrapMode.WidgetWidth)
    term.setMaximumBlockCount(8000)
    term.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    term.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    font = QFont("Consolas", 10)
    if not font.exactMatch():
        font = QFont("Courier New", 10)
    term.setFont(font)
    term.document().setDocumentMargin(4)
    wb.panel_body.addWidget(term, 1)
    return pause, clear, term
