# -*- coding: utf-8 -*-
"""Shared suite chrome — VS Code–style workbench (minimal vertical chrome).

Vertical layers (keep to two above the editor):
  1. Frameless menubar row — File/Edit/View + actions + layout
     toggles + min/max/close (same row as VS Code custom title bar)
  2. One editor chrome row — page tabs OR page title only (no layout icons)
  3. Editor body
  4. Collapsible OUTPUT (optional)

Horizontal: Activity bar (icon strip) | optional Side Bar | editor column.
When side_bar_enabled: activity stays visible; Ctrl+B toggles the Side Bar.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Optional, Sequence

from PyQt6.QtCore import QEvent, QObject, QPoint, QSize, Qt
from PyQt6.QtGui import QFont, QFontMetrics, QMouseEvent
from PyQt6.QtWidgets import (
    QAbstractButton,
    QButtonGroup,
    QFrame,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMenu,
    QMenuBar,
    QPushButton,
    QSizePolicy,
    QSplitter,
    QStackedWidget,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, plugin_shell, vscode_theme
from _shared import i18n

# Menubar chrome density — match host / VS Code custom title row.
CHROME_BTN_H = 35
LAYOUT_BTN_W = 36
WIN_BTN_W = 46
CHROME_ICON = 16
MENU_ROW_H = 35  # full glyph room; InstantPopup QToolButton clipped at 30



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
    "eds": "EDS — Dictionary / Profiles / Validate (CANeds)",
    "library": "Library — starters, profiles, recent files",
    "device": "Device — live OD and PDO on the bus",
    "network": "Network — Scan, NMT, frame monitor",
    "project": "Project — folder, main EDS, Node-ID",
    "setup": "Setup — Node-ID and preferences",
    "editor": "Editor — object dictionary and device info",
    "matrix": "Matrix — communications Tx/Rx grid",
    "valuetables": "Value Tables — named VAL_TABLE_ library",
    "attributes": "Attributes — BA_DEF_ / BA_ values",
    "validate": "Validate — findings, analysis, compare, merge, export",
    "timing": "Timing — DC cycle, shift and cable delay (EtherCAT) / analysis",
    "objects": "Objects — PDO, CoE and ESI",
    "compare": "Compare — diff two description files",
    "merge": "Merge — combine DBC files",
    "export": "Export — EDS / DCF / HTML / CSV / XDD",
    "pdo": "PDO Map — file-layer RPDO / TPDO mapping",
    # DBC Studio activities
    "edit": "Edit — messages, signals, value tables, attributes",
    "analyze": "Analyze — matrix, timing, validate",
    "integrate": "Integrate — compare and merge DBC files",
    "deliver": "Deliver — export and library",
    # Log Converter (batch tip shared with UDS Suite activity id)
    "convert": "Convert — single-file BLF / ASC / CSV / PCAP / TRC",
    "inspect": "Inspect — probe headers and format support matrix",
    "jobs": "Jobs — conversion history and re-run",
    "a2l": "A2L — applied ASAP2 symbols (read-only)",
    "live": "Live — XCP on CAN connect",
    "measure": "Measure — poll / DAQ and record",
    "calibrate": "Calibrate — scalar / MAP write",
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
    """Park host for layout toggles until attach_layout_toggles_to_menubar."""
    layout_toggle_host: QWidget
    set_sidebar_visible: Callable[[bool], None]
    set_panel_visible: Callable[[bool], None]
    set_maximized: Callable[[bool], None]
    is_sidebar_visible: Callable[[], bool]
    is_panel_visible: Callable[[], bool]
    is_maximized: Callable[[], bool]
    collapse_panel: Callable[[], None]
    expand_panel: Callable[[], None]
    goto_page: Callable[[str], None]
    highlight_activity: Callable[[str], None]
    current_page: Callable[[], str]
    set_editor_tabs: Callable[[QWidget], None]
    set_editor_title: Callable[[str], None]
    editor_layout: QVBoxLayout
    retranslate: Callable[[], None] = field(default=lambda: None)
    # Optional Side Bar (between activity and editor) — VS Code Explorer style.
    side_bar: Optional[QWidget] = None
    set_side_bar_widget: Optional[Callable[[Optional[QWidget]], None]] = None
    set_side_bar_visible: Optional[Callable[[bool], None]] = None
    is_side_bar_visible: Optional[Callable[[], bool]] = None
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
    side_bar_enabled: bool = False,
    side_bar_visible: bool = True,
    side_bar_width: int = 260,
    lock_activity: bool = False,
) -> WorkbenchParts:
    """Attach a VS Code–style workbench — one chrome row, not stacked shells.

    *side_bar_enabled*: insert a collapsible Side Bar between the activity
    strip and the editor (Explorer-style). Ctrl+B / btn_sidebar toggles it.
    *lock_activity*: keep the activity icon strip always visible (never hide).
    """
    vscode_theme.apply(window)
    plugin_shell.attach_status_bar(window, "Ready")

    state = {
        "sidebar": (
            side_bar_visible if side_bar_enabled else sidebar_visible),
        "panel": panel_visible,
        "maximized": False,
        "panel_height": 160,
        "saved_sidebar": (
            side_bar_visible if side_bar_enabled else sidebar_visible),
        "saved_panel": panel_visible,
        "page": nav_pages[0][0] if nav_pages else "",
        "side_bar_width": max(180, int(side_bar_width)),
    }

    central = QWidget()
    window.setCentralWidget(central)
    outer = QVBoxLayout(central)
    outer.setContentsMargins(0, 0, 0, 0)
    outer.setSpacing(0)

    # Slot for SuiteMenuChrome (File/Edit row). Must live in central — NOT
    # QMainWindow.setMenuWidget — because menuBar() deletes that widget.
    menu_chrome_slot = QWidget()
    menu_chrome_slot.setObjectName("SuiteMenuChromeSlot")
    menu_chrome_slot.setFixedHeight(0)
    menu_chrome_slot_l = QVBoxLayout(menu_chrome_slot)
    menu_chrome_slot_l.setContentsMargins(0, 0, 0, 0)
    menu_chrome_slot_l.setSpacing(0)
    outer.addWidget(menu_chrome_slot, 0)
    window._suite_menu_chrome_slot = menu_chrome_slot  # type: ignore[attr-defined]

    body = QWidget()
    body.setObjectName("SuiteWorkbenchBody")
    root = QHBoxLayout(body)
    root.setContentsMargins(0, 0, 0, 0)
    root.setSpacing(0)
    outer.addWidget(body, 1)

    # VS Code activity rail: 48px wide, full-bleed icon buttons, 24px glyphs
    _act_w = max(48, int(nav_width))
    _act_icon = 24
    activity = QWidget()
    activity.setObjectName("SuiteActivityBar")
    activity.setFixedWidth(_act_w)
    act_l = QVBoxLayout(activity)
    act_l.setContentsMargins(0, 6, 0, 6)
    act_l.setSpacing(0)

    page_index = {}
    activity_btns: dict[str, QToolButton] = {}
    btn_group = QButtonGroup(activity)
    btn_group.setExclusive(True)

    stack = QStackedWidget()
    stack.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

    def _select_page(key: str, *, switch_stack: bool = True):
        idx = page_index.get(key)
        if idx is None:
            return
        state["page"] = key
        if switch_stack:
            stack.setCurrentIndex(idx)
        btn = activity_btns.get(key)
        if btn is not None:
            btn.blockSignals(True)
            btn.setChecked(True)
            btn.blockSignals(False)
            for k, b in activity_btns.items():
                color = vscode_theme.ACCENT if k == key else vscode_theme.TEXT
                icon_key = nav_pages[page_index[k]][0]
                codicons.set_button(b, icon_key, color=color, size=_act_icon)
        # Tab-based suites own the chrome slot via set_editor_tabs. Never replace
        # their strip with an activity title (would wipe open tabs on highlight).
        if not callable(getattr(window, "_on_workbench_page", None)):
            title_map = dict(nav_pages)
            tab_pages = {
                "diagnose", "com", "system", "topology", "frames", "esi",
                "network", "eds", "library",
                "edit", "analyze", "integrate", "deliver",
                "a2l", "live", "measure", "calibrate",
            }
            if key not in tab_pages and not _chrome_tabs:
                set_editor_title(title_map.get(key, key))

    def highlight_activity(key: str):
        """Update activity icon strip without switching the editor stack."""
        _select_page(key, switch_stack=False)

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
        b.setFixedSize(_act_w, _act_w)
        b.setIconSize(QSize(_act_icon, _act_icon))
        tip = ACTIVITY_TIPS.get(key, page_title)
        tip = i18n.t(tip, tip)
        tip = "%s  (Ctrl+%d)" % (tip, i + 1)
        b.setToolTip(tip)
        codicons.set_button(b, key, color=vscode_theme.TEXT, size=_act_icon)
        btn_group.addButton(b, i)
        activity_btns[key] = b

        def _on_click(_checked=False, k=key, btn=b):
            if state["page"] == k and btn.isChecked():
                if side_bar_enabled:
                    set_side_bar_visible(not state["sidebar"])
                elif state["panel"]:
                    set_panel_visible(False)
                return
            on_activity = getattr(window, "_on_activity_clicked", None)
            if callable(on_activity):
                on_activity(k)
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
        act_l.addWidget(
            _add_activity_btn(idx, key, page_title),
            0, Qt.AlignmentFlag.AlignHCenter)
    act_l.addStretch(1)
    for key, page_title in foot_pages:
        idx = next(j for j, (k, _) in enumerate(nav_pages) if k == key)
        act_l.addWidget(
            _add_activity_btn(idx, key, page_title),
            0, Qt.AlignmentFlag.AlignHCenter)
    root.addWidget(activity)

    side_bar_host = QWidget()
    side_bar_host.setObjectName("SuiteSideBar")
    side_bar_host.setMinimumWidth(0)
    sb_outer = QVBoxLayout(side_bar_host)
    sb_outer.setContentsMargins(0, 0, 0, 0)
    sb_outer.setSpacing(0)
    side_bar_body = QVBoxLayout()
    side_bar_body.setContentsMargins(0, 0, 0, 0)
    side_bar_body.setSpacing(0)
    sb_body_host = QWidget()
    sb_body_host.setLayout(side_bar_body)
    sb_outer.addWidget(sb_body_host, 1)
    _side_bar_widget: list = [None]
    # Hidden park for reusable Side Bar widgets. setParent(None) on a still-visible
    # QWidget turns it into a top-level OS window (the "tiny OD tab" popup bug).
    _widget_park = QWidget()
    _widget_park.hide()
    _widget_park.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)

    def set_side_bar_widget(widget: Optional[QWidget]):
        while side_bar_body.count():
            item = side_bar_body.takeAt(0)
            w = item.widget()
            if w is not None:
                w.hide()
                w.setParent(_widget_park)
        _side_bar_widget[0] = widget
        if widget is not None:
            side_bar_body.addWidget(widget, 1)
            widget.show()

    h_splitter = None

    right = QWidget()
    right.setObjectName("SuiteContent")
    right_l = QVBoxLayout(right)
    right_l.setContentsMargins(0, 0, 0, 0)
    right_l.setSpacing(0)

    chrome = QWidget()
    chrome.setObjectName("SuiteEditorChrome")
    # Match SuiteEditorTabs (TAB_H) + breathing room for accent / DPI.
    from _shared.suite_ui import CHROME_H
    chrome.setFixedHeight(CHROME_H)
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
                # Tab bars are recreated each mount — hide then delete so they
                # never become orphan top-level windows after setParent(None).
                w.hide()
                w.setParent(None)
                w.deleteLater()

    def set_editor_tabs(tab_bar: QWidget):
        _clear_chrome_slot()
        _chrome_tabs[:] = [tab_bar]
        tab_bar.setParent(chrome_slot_host)
        chrome_slot.addWidget(tab_bar, 1)
        tab_bar.show()

    def set_editor_title(text: str):
        _clear_chrome_slot()
        _chrome_tabs.clear()
        title_lab = QLabel(text)
        title_lab.setObjectName("SuiteEditorTitle")
        chrome_slot.addWidget(title_lab)
        chrome_slot.addStretch(1)

    def _make_toggle(icon_name: str, tip: str) -> QToolButton:
        btn = QToolButton()
        btn.setObjectName("LayoutToggleBtn")
        btn.setCheckable(True)
        btn.setChecked(True)
        btn.setAutoRaise(True)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setFixedSize(LAYOUT_BTN_W, CHROME_BTN_H)
        btn.setIconSize(QSize(CHROME_ICON, CHROME_ICON))
        btn.setToolTip(tip)
        codicons.set_button(
            btn, icon_name, color=vscode_theme.TEXT_DIM, size=CHROME_ICON)
        return btn

    # Layout toggles live on the menubar right corner (VS Code), not this row.
    layout_toggle_host = QWidget()
    layout_toggle_host.setObjectName("SuiteLayoutTogglePark")
    layout_toggle_host.hide()
    park_lay = QHBoxLayout(layout_toggle_host)
    park_lay.setContentsMargins(0, 0, 0, 0)
    park_lay.setSpacing(0)

    if side_bar_enabled or lock_activity:
        btn_sidebar = _make_toggle(
            "layout-sidebar-left", i18n.t("Toggle Side Bar (Ctrl+B)"))
        max_tip = i18n.t("Maximize Editor — hide side bar & panel")
    else:
        btn_sidebar = _make_toggle(
            "layout-sidebar-left", i18n.t("Toggle Activity Bar (Ctrl+B)"))
        max_tip = i18n.t("Maximize Editor — hide activity bar & panel")
    btn_panel = _make_toggle("layout-panel", i18n.t("Toggle Panel (Ctrl+J)"))
    btn_maximize = _make_toggle("layout-maximize", max_tip)
    btn_maximize.setChecked(False)
    for _btn in (btn_sidebar, btn_panel, btn_maximize):
        park_lay.addWidget(_btn)

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
    panel_head.setFixedHeight(24)
    ph = QHBoxLayout(panel_head)
    ph.setContentsMargins(6, 0, 2, 0)
    ph.setSpacing(4)
    panel_title_lab = QLabel(i18n.t(panel_title, panel_title))
    panel_title_lab.setObjectName("SuiteToolbarTitle")
    panel_title_lab.setToolTip(i18n.t("OUTPUT — double-click to collapse (Ctrl+J)"))
    ph.addWidget(panel_title_lab)

    panel_tools_host = QWidget()
    panel_tools = QHBoxLayout(panel_tools_host)
    panel_tools.setContentsMargins(0, 0, 0, 0)
    panel_tools.setSpacing(8)
    ph.addWidget(panel_tools_host, 1)

    collapse_btn = QToolButton()
    collapse_btn.setObjectName("LayoutToggleBtn")
    collapse_btn.setAutoRaise(True)
    collapse_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    collapse_btn.setFixedSize(24, 24)
    collapse_btn.setIconSize(QSize(14, 14))
    collapse_btn.setToolTip("Collapse Panel")
    codicons.set_button(
        collapse_btn, "chevron-down", color=vscode_theme.TEXT_DIM, size=14)
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

    if side_bar_enabled:
        h_splitter = QSplitter(Qt.Orientation.Horizontal)
        h_splitter.setObjectName("SuiteHSplitter")
        h_splitter.setHandleWidth(1)
        h_splitter.setChildrenCollapsible(True)
        h_splitter.addWidget(side_bar_host)
        h_splitter.addWidget(right)
        h_splitter.setStretchFactor(0, 0)
        h_splitter.setStretchFactor(1, 1)
        h_splitter.setSizes([state["side_bar_width"], 1000])
        root.addWidget(h_splitter, 1)
    else:
        side_bar_host.hide()
        root.addWidget(right, 1)

    def _apply_activity(visible: bool):
        if lock_activity or side_bar_enabled:
            activity.setVisible(True)
            return
        activity.setVisible(visible)

    def _apply_side_bar(visible: bool):
        if not side_bar_enabled:
            return
        state["sidebar"] = visible
        if visible:
            side_bar_host.show()
            if h_splitter is not None:
                sizes = h_splitter.sizes()
                total = sum(sizes) or 1200
                w = max(state["side_bar_width"], 200)
                h_splitter.setSizes([w, max(400, total - w)])
        else:
            if h_splitter is not None:
                sizes = h_splitter.sizes()
                if sizes and sizes[0] > 40:
                    state["side_bar_width"] = sizes[0]
                h_splitter.setSizes([0, max(sum(sizes), 1000)])
            side_bar_host.hide()
        btn_sidebar.blockSignals(True)
        btn_sidebar.setChecked(visible)
        btn_sidebar.blockSignals(False)

    def _apply_sidebar(visible: bool):
        if side_bar_enabled:
            _apply_side_bar(visible)
        else:
            state["sidebar"] = visible
            _apply_activity(visible)
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
                collapse_btn, "chevron-down",
                color=vscode_theme.TEXT_DIM, size=12)
            collapse_btn.setToolTip("Collapse Panel")
        else:
            sizes = v_splitter.sizes()
            if sizes[1] > 40:
                state["panel_height"] = sizes[1]
            panel.hide()
            codicons.set_button(
                collapse_btn, "chevron-up",
                color=vscode_theme.TEXT_DIM, size=12)
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
            _apply_activity(
                True if (lock_activity or side_bar_enabled) else False)
        else:
            state["maximized"] = False
            _apply_sidebar(state["saved_sidebar"])
            _apply_panel(state["saved_panel"])
            _apply_activity(True)
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

    def set_side_bar_visible(visible: bool):
        set_sidebar_visible(visible)

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

    activity.setVisible(True)
    if side_bar_enabled:
        _apply_side_bar(side_bar_visible)
    else:
        _apply_sidebar(sidebar_visible)
    _apply_panel(panel_visible)
    if nav_pages:
        _select_page(nav_pages[0][0])

    def retranslate():
        for key, page_title in nav_pages:
            b = activity_btns.get(key)
            if b is None:
                continue
            tip = ACTIVITY_TIPS.get(key, page_title)
            tip = i18n.t(tip, tip)
            idx = page_index.get(key, 0)
            b.setToolTip("%s  (Ctrl+%d)" % (tip, idx + 1))
        btn_sidebar.setToolTip(
            i18n.t("Toggle Side Bar (Ctrl+B)") if (side_bar_enabled or lock_activity)
            else i18n.t("Toggle Activity Bar (Ctrl+B)"))
        btn_panel.setToolTip(i18n.t("Toggle Panel (Ctrl+J)"))
        panel_title_lab.setText(i18n.t(panel_title, panel_title))
        panel_title_lab.setToolTip(
            i18n.t("OUTPUT — double-click to collapse (Ctrl+J)"))

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
        layout_toggle_host=layout_toggle_host,
        set_sidebar_visible=set_sidebar_visible,
        set_panel_visible=set_panel_visible,
        set_maximized=set_maximized,
        is_sidebar_visible=lambda: state["sidebar"],
        is_panel_visible=lambda: state["panel"],
        is_maximized=lambda: state["maximized"],
        collapse_panel=collapse_panel,
        expand_panel=expand_panel,
        goto_page=goto_page,
        highlight_activity=highlight_activity,
        current_page=lambda: state["page"],
        set_editor_tabs=set_editor_tabs,
        set_editor_title=set_editor_title,
        editor_layout=right_l,
        retranslate=retranslate,
        side_bar=side_bar_host if side_bar_enabled else None,
        set_side_bar_widget=set_side_bar_widget if side_bar_enabled else None,
        set_side_bar_visible=set_side_bar_visible if side_bar_enabled else None,
        is_side_bar_visible=(
            (lambda: state["sidebar"]) if side_bar_enabled else None),
        title_bar=chrome,
        title_trailing=chrome_slot,
        nav=activity,
        h_splitter=h_splitter,
        _activity_btns=activity_btns,
    )



WorkbenchHandles = WorkbenchParts  # compat alias for mount helpers


def _normalize_menubar_action(widget: QWidget) -> None:
    """Force menubar trailing controls to CHROME_BTN_H / CHROME_ICON."""
    if isinstance(widget, QAbstractButton):
        widget.setFixedHeight(CHROME_BTN_H)
        if not widget.text():
            widget.setFixedSize(LAYOUT_BTN_W, CHROME_BTN_H)
        widget.setIconSize(QSize(CHROME_ICON, CHROME_ICON))
        name = widget.property("codiconName")
        if name:
            primary = bool(widget.property("codiconPrimary"))
            try:
                codicons.set_button(
                    widget, str(name),
                    color=vscode_theme.TEXT, size=CHROME_ICON,
                    primary=primary)
            except Exception:
                pass
    elif isinstance(widget, QLabel):
        widget.setFixedHeight(CHROME_BTN_H)
        widget.setAlignment(
            Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft)


class _MenubarChromeFilter(QObject):
    """Drag / double-click maximize on empty menu chrome (VS Code)."""

    def __init__(self, window: QMainWindow, bar: QMenuBar):
        super().__init__(window)
        self._window = window
        self._bar = bar

    def eventFilter(self, obj, event):  # noqa: N802
        if obj is None or obj.objectName() != "SuiteMenuChrome":
            return False
        et = event.type()
        if et not in (QEvent.Type.MouseButtonPress, QEvent.Type.MouseButtonDblClick):
            return False
        me = event
        if not (isinstance(me, QMouseEvent)
                and me.button() == Qt.MouseButton.LeftButton):
            return False
        child = obj.childAt(me.pos())
        walk = child
        for _ in range(8):
            if walk is None:
                break
            n = walk.objectName() if hasattr(walk, "objectName") else ""
            # Never steal clicks from menus, tools, or window buttons.
            if n in (
                    "SuiteMenuButtons", "SuiteMenubarTrailing",
                    "SuiteMenuButton", "SuiteMenuLabel",
                    "WinMinBtn", "WinMaxBtn", "WinCloseBtn",
                    "LayoutToggleBtn"):
                return False
            if isinstance(walk, (QToolButton, QAbstractButton, QLabel, QMenuBar)):
                return False
            walk = walk.parentWidget() if hasattr(walk, "parentWidget") else None

        if et == QEvent.Type.MouseButtonPress:
            wh = self._window.windowHandle()
            if wh is not None:
                wh.startSystemMove()
                return True
        else:
            if self._window.isMaximized():
                self._window.showNormal()
            else:
                self._window.showMaximized()
            return True
        return False


class _WindowStateFilter(QObject):
    """Refresh maximize/restore icon when window state changes."""

    def __init__(self, window: QMainWindow, max_btn: QToolButton):
        super().__init__(window)
        self._window = window
        self._max_btn = max_btn

    def eventFilter(self, obj, event):  # noqa: N802
        if obj is self._window and event.type() == QEvent.Type.WindowStateChange:
            _set_win_max_icon(self._max_btn, self._window.isMaximized())
        return False


def _set_win_max_icon(btn: QToolButton, maximized: bool) -> None:
    name = "win-restore" if maximized else "win-maximize"
    codicons.set_button(
        btn, name, color=vscode_theme.TEXT, size=CHROME_ICON)
    btn.setProperty("codiconName", name)


def _make_win_btn(name: str, tip: str, object_name: str) -> QToolButton:
    btn = QToolButton()
    btn.setObjectName(object_name)
    btn.setAutoRaise(True)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setFixedSize(WIN_BTN_W, CHROME_BTN_H)
    btn.setIconSize(QSize(CHROME_ICON, CHROME_ICON))
    btn.setToolTip(tip)
    codicons.set_button(btn, name, color=vscode_theme.TEXT, size=CHROME_ICON)
    btn.setProperty("codiconName", name)
    return btn


def _ensure_frameless(window: QMainWindow) -> None:
    """Drop OS title bar so the QMenuBar is the only top chrome row."""
    if getattr(window, "_suite_frameless", False):
        return
    window.setWindowFlags(
        Qt.WindowType.Window
        | Qt.WindowType.FramelessWindowHint
        | Qt.WindowType.WindowSystemMenuHint
        | Qt.WindowType.WindowMinimizeButtonHint
        | Qt.WindowType.WindowMaximizeButtonHint
    )
    # Thin border so frameless window still reads as a frame on light theme.
    window.setProperty("suiteFrameless", True)
    window.style().unpolish(window)
    window.style().polish(window)
    window._suite_frameless = True  # type: ignore[attr-defined]


def _menu_sep() -> QFrame:
    line = QFrame()
    line.setObjectName("SuiteMenuSep")
    line.setFrameShape(QFrame.Shape.VLine)
    line.setFrameShadow(QFrame.Shadow.Plain)
    line.setFixedWidth(1)
    line.setFixedHeight(max(16, CHROME_BTN_H - 12))
    return line


def _strip_amp(text: str) -> str:
    return (text or "").replace("&", "").strip()


def _harvest_top_menus(bar: QMenuBar) -> list:
    """Return [(label, QMenu), ...] from a populated QMenuBar."""
    out = []
    for act in list(bar.actions()):
        menu = act.menu()
        if menu is None:
            continue
        label = _strip_amp(act.text())
        if not label:
            continue
        out.append((label, menu))
    return out


def _park_popup_menu(menu: QMenu, owner: QWidget) -> None:
    """Own a harvested QMenu as a Popup — never as a visible embedded child.

    ``setParent(label)`` without Popup flags leaves the QMenu visible at (0,0)
    with its full size (200×300), painting over File/Edit until the user clicks
    (which dismisses the phantom panel). That was the 'menubar occluded' bug.
    """
    menu.hide()
    menu.setParent(owner)
    menu.setWindowFlags(Qt.WindowType.Popup)
    menu.hide()


class SuiteMenuButton(QLabel):
    """VS Code top-level menu label — QLabel + Popup QMenu (not QPushButton)."""

    _PAD_X = 24

    def __init__(self, label: str, menu: QMenu, host: Optional[QWidget] = None):
        super().__init__(label)
        self.setObjectName("SuiteMenuLabel")
        self.setAlignment(
            Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignHCenter)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_Hover, True)
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)

        font = QFont(self.font())
        font.setPixelSize(vscode_theme.FS_MENU)
        self.setFont(font)
        self._menu = menu
        self._host = host
        # Owner = host window (or host) so the menu is not an embedded child of
        # this label. Popup flag + hide prevents occlusion of the text.
        owner = host.window() if host is not None else self
        if owner is None:
            owner = self
        _park_popup_menu(menu, owner)
        menu.aboutToHide.connect(menu.hide)
        self._apply_label_width(label)

    def _apply_label_width(self, label: str) -> None:
        fm = QFontMetrics(self.font())
        text_w = fm.horizontalAdvance(label) if label else 0
        w = max(52, text_w + self._PAD_X)
        self.setFixedSize(w, MENU_ROW_H)

    def showEvent(self, event) -> None:  # noqa: N802
        super().showEvent(event)
        self._apply_label_width(self.text())
        m = self._menu
        # If something re-parented the menu onto this label, re-park as Popup.
        if m is not None and m.parentWidget() is self:
            owner = self.window() or self
            _park_popup_menu(m, owner)

    def sizeHint(self) -> QSize:  # noqa: N802
        return QSize(max(self.minimumWidth(), 52), MENU_ROW_H)

    def minimumSizeHint(self) -> QSize:  # noqa: N802
        return self.sizeHint()

    def mousePressEvent(self, event) -> None:  # noqa: N802
        if event.button() == Qt.MouseButton.LeftButton:
            self._popup()
            event.accept()
            return
        super().mousePressEvent(event)

    def _popup(self) -> None:
        host = self._host
        if host is not None:
            host._suite_menu_active = self  # type: ignore[attr-defined]
            for btn in host.findChildren(SuiteMenuButton):
                if btn is not self and btn._menu.isVisible():
                    btn._menu.close()
                    btn._menu.hide()
        self._menu.popup(self.mapToGlobal(QPoint(0, self.height())))

    def enterEvent(self, event) -> None:  # noqa: N802
        host = self._host
        active = getattr(host, "_suite_menu_active", None) if host else None
        if (
            host is not None
            and isinstance(active, SuiteMenuButton)
            and active is not self
            and active._menu.isVisible()
        ):
            self._popup()
        super().enterEvent(event)


def _make_menu_button(label: str, menu: QMenu,
                      host: Optional[QWidget] = None) -> SuiteMenuButton:
    return SuiteMenuButton(label, menu, host=host)


def _embed_menu_chrome(window: QMainWindow, chrome: QWidget) -> None:
    """Place SuiteMenuChrome in the workbench central slot (not setMenuWidget).

    Qt deletes any widget installed via ``setMenuWidget`` when ``menuBar()`` is
    later called — tab mounts used to trigger that and wipe File/Edit.
    Embedding in ``SuiteMenuChromeSlot`` survives ``menuBar()`` / corner
    re-attach mistakes; the official QMenuBar stays height-0 and hidden.
    """
    h = MENU_ROW_H
    chrome.setFixedHeight(h)
    slot = getattr(window, "_suite_menu_chrome_slot", None)
    if slot is None:
        slot = window.findChild(QWidget, "SuiteMenuChromeSlot")
    if slot is not None:
        sl = slot.layout()
        if sl is not None:
            while sl.count():
                item = sl.takeAt(0)
                w = item.widget()
                if w is not None and w is not chrome:
                    w.setParent(None)
            chrome.setParent(None)
            sl.addWidget(chrome)
        slot.setFixedHeight(h)
        slot.show()
        chrome.show()
        return
    # Legacy shell without a slot — last resort (fragile if menuBar() is used).
    window.setMenuWidget(chrome)
    chrome.show()


def reload_live_modules(*extra: str, plugin_dir: str | None = None) -> None:
    """Drop cached shell/chrome modules so live edits under plugins/ apply.

    sin_host keeps a long-lived interpreter — deactivate/reactivate would
    otherwise keep the first-imported ``suite_chrome`` (broken menubar).
    Call from each suite ``activate()`` before importing ``AppShell``.

    When several suites are open, only suite-local tops under *plugin_dir*
    are dropped so peer windows keep their parked modules.
    """
    import importlib
    import inspect
    import os
    import sys

    if plugin_dir is None:
        for fr in inspect.stack()[1:8]:
            f = getattr(fr, "filename", None) or ""
            if not f:
                continue
            # suite main.py / app_shell live directly under plugins/<id>/
            norm = f.replace("\\", "/")
            if "/plugins/" in norm and "/_shared/" not in norm:
                plugin_dir = os.path.dirname(os.path.abspath(f))
                break

    shared_keys = (
        "_shared.suite_chrome",
        "_shared.vscode_theme",
        "_shared.suite_tabs",
        "_shared.suite_ui",
    )
    local_keys = ("app_shell", "session", "document", "pages", *extra)

    def _under(path: str, root: str) -> bool:
        if not path or not root:
            return False
        try:
            return os.path.commonpath(
                [os.path.abspath(path), os.path.abspath(root)]
            ) == os.path.abspath(root)
        except ValueError:
            return False

    for name in list(sys.modules):
        drop = False
        for k in shared_keys:
            if name == k or name.startswith(k + "."):
                drop = True
                break
        if not drop:
            for k in local_keys:
                if name == k or name.startswith(k + "."):
                    mod = sys.modules.get(name)
                    f = getattr(mod, "__file__", None) if mod else None
                    if plugin_dir and f and not _under(f, plugin_dir):
                        continue
                    drop = True
                    break
        if drop:
            sys.modules.pop(name, None)
    try:
        importlib.invalidate_caches()
    except Exception:
        pass


def begin_suite_menubar(window: QMainWindow) -> QMenuBar:
    """Clear and return the suite QMenuBar to populate before attach.

    Prefer the bar cached on ``_suite_menu_bar`` (owned by SuiteMenuChrome)
    so we never leave a visible OS/QMainWindow menubar fighting the chrome.
    """
    bar = getattr(window, "_suite_menu_bar", None)
    if bar is None:
        bar = window.menuBar()
    if bar is None:
        bar = QMenuBar(window)
        window.setMenuBar(bar)
    bar.setNativeMenuBar(False)
    bar.clear()
    window._suite_menu_bar = bar  # type: ignore[attr-defined]
    return bar


def attach_layout_toggles_to_menubar(
        window: QMainWindow,
        wb: WorkbenchParts,
        extra_widgets: Optional[Sequence[QWidget]] = None,
) -> QWidget:
    """Mount one VS Code row: File Edit … | stretch | tools | win.

    Top-level menus become ``SuiteMenuLabel`` (plain text) + Popup ``QMenu``.
    The row lives in central ``SuiteMenuChromeSlot`` — never ``setMenuWidget``.
    No brand/logo left of File (VS Code menu text starts the row).
    """
    _ensure_frameless(window)

    bar = getattr(window, "_suite_menu_bar", None)
    if bar is None:
        bar = window.menuBar()
    if bar is None:
        bar = QMenuBar(window)
        window.setMenuBar(bar)
    bar.setNativeMenuBar(False)
    menus = _harvest_top_menus(bar)
    # Official menubar area stays a zero-height sentinel so later menuBar()
    # calls do not invent a visible empty bar (and never touch our chrome).
    bar.hide()
    bar.setFixedHeight(0)

    # --- trailing: doc tools | layout | window (equal icon size, grouped) ---
    host = QWidget()
    host.setObjectName("SuiteMenubarTrailing")
    host.setFixedHeight(MENU_ROW_H)
    host.setSizePolicy(QSizePolicy.Policy.Maximum, QSizePolicy.Policy.Fixed)
    row = QHBoxLayout(host)
    row.setContentsMargins(6, 0, 0, 0)
    row.setSpacing(2)

    extras = [w for w in list(extra_widgets or ()) if w is not None]
    if extras:
        for w in extras:
            w.setParent(None)
            _normalize_menubar_action(w)
            row.addWidget(w, 0, Qt.AlignmentFlag.AlignVCenter)
        row.addWidget(_menu_sep(), 0, Qt.AlignmentFlag.AlignVCenter)

    for btn in (wb.btn_sidebar, wb.btn_panel, wb.btn_maximize):
        btn.setParent(None)
        btn.setFixedSize(LAYOUT_BTN_W, CHROME_BTN_H)
        btn.setIconSize(QSize(CHROME_ICON, CHROME_ICON))
        try:
            name = btn.property("codiconName")
            if name:
                codicons.set_button(
                    btn, str(name), color=vscode_theme.TEXT_DIM,
                    size=CHROME_ICON)
        except Exception:
            pass
        row.addWidget(btn, 0, Qt.AlignmentFlag.AlignVCenter)

    row.addWidget(_menu_sep(), 0, Qt.AlignmentFlag.AlignVCenter)

    min_btn = _make_win_btn("win-minimize", "Minimize", "WinMinBtn")
    max_btn = _make_win_btn(
        "win-restore" if window.isMaximized() else "win-maximize",
        "Maximize", "WinMaxBtn")
    close_btn = _make_win_btn("close", "Close", "WinCloseBtn")
    _set_win_max_icon(max_btn, window.isMaximized())

    min_btn.clicked.connect(window.showMinimized)
    max_btn.clicked.connect(
        lambda: window.showNormal() if window.isMaximized()
        else window.showMaximized())
    close_btn.clicked.connect(window.close)

    row.addWidget(min_btn)
    row.addWidget(max_btn)
    row.addWidget(close_btn)

    if getattr(window, "_suite_winstate_filter", None) is None:
        wf = _WindowStateFilter(window, max_btn)
        window.installEventFilter(wf)
        window._suite_winstate_filter = wf  # type: ignore[attr-defined]
    else:
        window._suite_winstate_filter._max_btn = max_btn  # type: ignore[attr-defined]

    # --- chrome row ---
    chrome = getattr(window, "_suite_menu_chrome", None)
    if chrome is None:
        chrome = QWidget()
        chrome.setObjectName("SuiteMenuChrome")
        chrome.setFixedHeight(MENU_ROW_H)
        cl = QHBoxLayout(chrome)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)
        chrome._layout = cl  # type: ignore[attr-defined]
        window._suite_menu_chrome = chrome  # type: ignore[attr-defined]
    else:
        chrome.setFixedHeight(MENU_ROW_H)
    cl = chrome._layout  # type: ignore[attr-defined]
    while cl.count():
        item = cl.takeAt(0)
        w = item.widget()
        if w is not None and w is not host and w is not bar:
            # Drop previous SuiteMenuButton instances on rebuild.
            w.setParent(None)
            w.deleteLater()

    brand = getattr(window, "_suite_brand", None)
    if brand is not None:
        brand.hide()
        brand.setParent(None)
        brand.deleteLater()
        window._suite_brand = None  # type: ignore[attr-defined]

    menu_host = QWidget()
    menu_host.setObjectName("SuiteMenuButtons")
    menu_host.setFixedHeight(MENU_ROW_H)
    menu_host.setSizePolicy(
        QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Fixed)
    menu_host._suite_menu_active = None  # type: ignore[attr-defined]
    ml = QHBoxLayout(menu_host)
    ml.setContentsMargins(8, 0, 0, 0)
    ml.setSpacing(0)
    for label, menu in menus:
        btn = _make_menu_button(label, menu, host=menu_host)
        ml.addWidget(btn, 0, Qt.AlignmentFlag.AlignVCenter)
    cl.addWidget(menu_host, 0, Qt.AlignmentFlag.AlignVCenter)
    cl.addStretch(1)

    host.setParent(None)
    host.setFixedHeight(MENU_ROW_H)
    cl.addWidget(host, 0, Qt.AlignmentFlag.AlignVCenter)

    # Keep silent harvested QMenuBar as child so ownership stays valid.
    bar.setParent(chrome)
    bar.hide()
    bar.setFixedHeight(0)

    sentinel = getattr(window, "_suite_menu_sentinel", None)
    if sentinel is None:
        sentinel = QMenuBar(window)
        sentinel.setObjectName("SuiteMenuBarSentinel")
        sentinel.setNativeMenuBar(False)
        window._suite_menu_sentinel = sentinel  # type: ignore[attr-defined]
    sentinel.setMaximumHeight(0)
    sentinel.setFixedHeight(0)
    sentinel.hide()
    window.setMenuBar(sentinel)

    _embed_menu_chrome(window, chrome)
    host.show()
    menu_host.show()

    if getattr(window, "_suite_menubar_filter", None) is None:
        filt = _MenubarChromeFilter(window, bar)
        chrome.installEventFilter(filt)
        window._suite_menubar_filter = filt  # type: ignore[attr-defined]

    window._suite_menu_bar = bar  # type: ignore[attr-defined]
    window._suite_menu_buttons = menu_host  # type: ignore[attr-defined]
    wb.layout_toggle_host = host  # type: ignore[misc]
    window._suite_win_btns = (min_btn, max_btn, close_btn)  # type: ignore[attr-defined]
    return host



def page_margins(layout, *, top: int = 12) -> None:
    layout.setContentsMargins(16, top, 16, 12)
    layout.setSpacing(12)


def bind_nav_shortcuts(window, nav_pages, goto_fn) -> None:
    for i in range(len(nav_pages)):
        plugin_shell.bind_shortcut(
            window, "Ctrl+%d" % (i + 1),
            lambda _=False, idx=i: goto_fn(nav_pages[idx][0]))


def make_toolbar(title: str | None = None):
    """One TOOL_H strip — same density as suite_ui.tool_strip (full field borders)."""
    from _shared.suite_ui import PAD_X, STRIP_PAD_V, TOOL_H

    bar = QWidget()
    # SuiteToolStrip + SuiteToolbar share QSS density (CTRL_H + vertical pad).
    bar.setObjectName("SuiteToolStrip")
    bar.setMinimumHeight(TOOL_H)
    row = QHBoxLayout(bar)
    row.setContentsMargins(PAD_X, STRIP_PAD_V, PAD_X, STRIP_PAD_V)
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
    pause.setFixedSize(24, 24)
    pause.setIconSize(QSize(14, 14))
    pause.setToolTip(i18n.t("Pause"))
    try:
        codicons.set_button(
            pause, "stop", color=vscode_theme.TEXT_DIM, size=14)
    except Exception:
        pause.setText("||")

    clear = QToolButton()
    clear.setObjectName("LayoutToggleBtn")
    clear.setAutoRaise(True)
    clear.setCursor(Qt.CursorShape.PointingHandCursor)
    clear.setFixedSize(24, 24)
    clear.setIconSize(QSize(14, 14))
    clear.setToolTip(i18n.t("Clear"))
    try:
        codicons.set_button(
            clear, "clear", color=vscode_theme.TEXT_DIM, size=14)
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
