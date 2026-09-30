# -*- coding: utf-8 -*-
"""DBC Studio UI kit — industrial IDE density (VS Code / CANdb++-like).

Chrome recipe (every work page):
  1. ``tool_strip`` or ``panel_header`` — one 32px row
  2. optional ``inline_filter`` — controls at CTRL_H
  3. work surface (tree / table / form) — stretch
Hints go in tooltips, never a second caption row under the chrome.
"""

from __future__ import annotations

from typing import Optional

from PyQt6.QtCore import Qt, QSize
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QFrame,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QSizePolicy,
    QSplitter,
    QTableWidget,
    QToolButton,
    QTreeWidget,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, vscode_theme as T

# VS Code Light type + control scale (one kit for the whole suite)
#   Body / tree / nav ...... 13px
#   Controls / buttons ..... 12px, CTRL_H 28
#   Meta / chrome labels ... 11px muted (never shouty uppercase titles)
CTRL_H = 28
STRIP_PAD_V = 4
STRIP_EDGE = 2   # border-bottom + DPI fudge
TOOL_H = CTRL_H + 2 * STRIP_PAD_V + STRIP_EDGE   # 38
FILTER_H = TOOL_H
TAB_H = 36
CHROME_H = TAB_H + 2                # 38
ICON = 16
ICON_TOOL = 16
ICON_CHIP = 14
ICON_TAB = 12
GAP = 6
PAD_X = 10
PAD_Y = 6
ROW_H = 26
FS_BODY = 13
FS_CTRL = 12
FS_META = 11
ICON_FG = T.TEXT
ICON_MUTED = T.TEXT_DIM

# Golden ratio (φ ≈ 1.618). Master pane gets φ⁻¹ of width when it is the
# primary work surface; catalog / secondary nav gets the complementary share.
PHI = 1.6180339887
GOLDEN_MAJOR = 618  # ≈ 1000 / φ
GOLDEN_MINOR = 382  # ≈ 1000 - GOLDEN_MAJOR
# Default pane floors — keep splitters movable
PANE_MIN = 180
PANE_MIN_PROP = 200  # property / Objects catalog

TREE_STYLE = f"""
QTreeWidget#SuiteMatrix {{
  border: none; background: transparent; outline: 0;
  font-size: {FS_BODY}px;
}}
QTreeWidget#SuiteMatrix::item {{
  padding: 4px 8px; min-height: {ROW_H}px;
}}
QTreeWidget#SuiteMatrix::item:selected {{
  background: {T.ACCENT_SOFT}; color: {T.TEXT};
}}
QTreeWidget#SuiteMatrix::item:hover:!selected {{
  background: {T.SIDEBAR_HOVER};
}}
QTreeWidget#SuiteMatrix::branch {{
  background: transparent;
}}
"""

LIST_STYLE = f"""
QListWidget#SuiteMatrix {{
  border: none; background: transparent; outline: 0;
  font-size: {FS_BODY}px;
}}
QListWidget#SuiteMatrix::item {{
  padding: 5px 10px; min-height: {ROW_H}px;
}}
QListWidget#SuiteMatrix::item:selected {{
  background: {T.ACCENT_SOFT}; color: {T.TEXT};
}}
QListWidget#SuiteMatrix::item:hover:!selected {{
  background: {T.SIDEBAR_HOVER};
}}
"""

TABLE_STYLE = f"""
QTableWidget#SuiteMatrix {{
  border: none; background: transparent; outline: 0;
  font-size: {FS_BODY}px;
  gridline-color: {T.BORDER_SOFT};
}}
QTableWidget#SuiteMatrix::item {{
  padding: 3px 8px;
}}
"""

DBC_OVERLAY = f"""
/* DBC Studio — one type/control scale over shared vscode_theme */
QMainWindow {{
  background: {T.BG};
  font-size: {FS_BODY}px;
  font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
}}
QWidget#SuiteSideBar,
QWidget#WorkspaceSideBar {{
  background: {T.SIDEBAR};
  border-right: 1px solid {T.BORDER};
  min-width: 200px;
}}
QWidget#SuiteSideBarHeader {{
  background: {T.SIDEBAR};
  border-bottom: 1px solid {T.BORDER_SOFT};
  min-height: {TOOL_H}px;
}}
QWidget#SuiteDocBanner {{
  background: {T.SIDEBAR};
  border-bottom: 1px solid {T.BORDER_SOFT};
  min-height: {TOOL_H}px;
}}
QLabel#SuiteSectionTitle {{
  color: {T.TEXT};
  font-size: {FS_CTRL}px;
  font-weight: 600;
  letter-spacing: 0;
}}
QLabel#SuiteSideBarTitle {{
  color: {T.TEXT};
  font-size: {FS_CTRL}px;
  font-weight: 600;
  letter-spacing: 0;
}}
QLabel#SuiteDocPath {{
  color: {T.TEXT_DIM};
  font-size: {FS_META}px;
  padding: 2px 0;
}}
QWidget#SuiteToolStrip,
QWidget#SuiteToolbar {{
  background: {T.EDITOR};
  border-bottom: 1px solid {T.BORDER_SOFT};
  min-height: {TOOL_H}px;
}}
QWidget#SuiteInlineFilter {{
  background: {T.EDITOR};
  border-bottom: 1px solid {T.BORDER_SOFT};
  min-height: {FILTER_H}px;
}}
QWidget#SuiteInlineFilter QLineEdit,
QWidget#SuiteInlineFilter QComboBox,
QWidget#SuiteInlineFilter QPushButton,
QWidget#SuiteInlineFilter QLabel,
QWidget#SuiteToolbar QLineEdit,
QWidget#SuiteToolbar QComboBox,
QWidget#SuiteToolbar QPushButton,
QWidget#SuiteToolStrip QLineEdit,
QWidget#SuiteToolStrip QComboBox,
QWidget#SuiteToolStrip QPushButton {{
  font-size: {FS_CTRL}px;
  min-height: {CTRL_H}px;
  max-height: {CTRL_H}px;
}}
QWidget#SuiteInlineFilter QLineEdit,
QWidget#SuiteToolbar QLineEdit,
QWidget#SuiteToolStrip QLineEdit {{
  border: 1px solid {T.BORDER};
  border-radius: 3px;
  background: {T.EDITOR};
  padding: 2px 8px;
}}
QWidget#SuitePropPanel {{
  background: {T.EDITOR};
  border-left: 1px solid {T.BORDER};
}}
QLabel#SuitePropTitle {{
  color: {T.TEXT};
  font-size: {FS_CTRL}px;
  font-weight: 600;
  padding: 0;
}}
QLabel#SuiteFieldLabel {{
  color: {T.TEXT_DIM};
  font-size: {FS_CTRL}px;
  font-weight: 500;
}}
QFrame#SuiteHairline {{
  background: {T.BORDER_SOFT};
  max-height: 1px;
  border: none;
}}
QTabBar#SuiteEditorTabs {{
  min-height: {TAB_H}px;
}}
QTabBar#SuiteEditorTabs::tab {{
  padding: 8px 4px 8px 12px;
  min-width: 72px;
  min-height: {TAB_H}px;
  max-height: {TAB_H}px;
  font-size: {FS_BODY}px;
}}
QTabBar#SuiteEditorTabs::tab:selected {{
  padding-top: 6px;
  padding-bottom: 8px;
}}
QToolButton#SuiteTabClose, QPushButton#SuiteTabClose {{
  border: none;
  background: transparent;
  padding: 0;
  margin: 0 6px 0 2px;
  min-width: 18px;
  max-width: 18px;
  min-height: 18px;
  max-height: 18px;
  border-radius: 4px;
  color: {T.TEXT_MUTED};
}}
QToolButton#SuiteTabClose:hover, QPushButton#SuiteTabClose:hover {{
  background: {T.SIDEBAR_HOVER};
  border: none;
}}
QToolButton#SuiteTabClose:pressed, QPushButton#SuiteTabClose:pressed {{
  background: {T.BORDER};
}}
QTreeWidget#SuiteMatrix,
QListWidget#SuiteMatrix,
QTableWidget#SuiteMatrix {{
  border: none;
  background: transparent;
  outline: 0;
  font-size: {FS_BODY}px;
}}
QTableWidget {{
  gridline-color: {T.BORDER_SOFT};
  font-size: {FS_BODY}px;
}}
QHeaderView::section {{
  background: {T.SIDEBAR};
  color: {T.TEXT_DIM};
  font-size: {FS_META}px;
  font-weight: 600;
  padding: 5px 8px;
  border: none;
  border-right: 1px solid {T.BORDER_SOFT};
}}
QSplitter#SuiteHSplitter::handle {{
  background: {T.BORDER_SOFT};
  width: 6px;
  margin: 0;
}}
QSplitter#SuiteHSplitter::handle:hover {{
  background: {T.ACCENT};
}}
QSplitter#SuiteVSplitter::handle {{
  background: {T.BORDER_SOFT};
  height: 6px;
}}
QSplitter#SuiteVSplitter::handle:hover {{
  background: {T.ACCENT};
}}
QWidget#SuitePropPanel QLineEdit,
QWidget#SuitePropPanel QComboBox,
QWidget#SuitePropPanel QSpinBox,
QWidget#SuitePropPanel QWidget#StepSpin {{
  max-width: 360px;
}}
QWidget#SuitePropPanel QLabel {{
  font-size: {FS_CTRL}px;
}}
QWidget#StepSpin {{
  background: {T.EDITOR};
  border: 1px solid #C8C8C8;
  border-radius: 3px;
}}
QWidget#StepSpin:hover {{
  border-color: #A8A8A8;
}}
QWidget#StepSpin QSpinBox#SuiteSpin {{
  border: none;
  background: transparent;
  padding: 2px 6px;
  min-height: {CTRL_H - 2}px;
  max-height: {CTRL_H - 2}px;
  font-size: {FS_CTRL}px;
}}
QWidget#StepSpinButtons {{
  background: transparent;
  border-left: 1px solid {T.BORDER_SOFT};
}}
QToolButton#StepSpinBtn {{
  border: none;
  border-radius: 0;
  background: transparent;
  padding: 0;
  margin: 0;
}}
QToolButton#StepSpinBtn:hover {{
  background: {T.SIDEBAR_HOVER};
}}
QToolButton#StepSpinBtn:pressed {{
  background: {T.BORDER_SOFT};
}}
QSpinBox#SuiteSpin {{
  padding-right: 20px;
  font-size: {FS_CTRL}px;
}}
QSpinBox#SuiteSpin::up-button,
QSpinBox#SuiteSpin::down-button {{
  subcontrol-origin: border;
  width: 18px;
  border: none;
  border-left: 1px solid {T.BORDER_SOFT};
  background: {T.EDITOR};
}}
QSpinBox#SuiteSpin::up-button {{
  subcontrol-position: top right;
  height: 14px;
}}
QSpinBox#SuiteSpin::down-button {{
  subcontrol-position: bottom right;
  height: 14px;
}}
QLabel#SuiteQuiet {{
  color: {T.TEXT_DIM};
  font-size: {FS_CTRL}px;
  padding: 6px {PAD_X}px 4px {PAD_X}px;
}}
QLabel#SuiteCount {{
  color: {T.TEXT_MUTED};
  font-size: {FS_META}px;
  padding: 0 4px;
}}
QLabel#SuiteHint {{
  color: {T.TEXT_MUTED};
  font-size: {FS_CTRL}px;
}}
QLabel#SuiteEmptyTitle {{
  color: {T.TEXT};
  font-size: {FS_BODY}px;
  font-weight: 600;
}}
QLabel#SuiteEmptyHint {{
  color: {T.TEXT_MUTED};
  font-size: {FS_CTRL}px;
}}
QTextEdit#SuiteCode,
QPlainTextEdit#SuiteCode {{
  font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
  font-size: {FS_CTRL}px;
  border: none;
}}
QStatusBar {{
  background: {T.SIDEBAR};
  border-top: 1px solid {T.BORDER};
  color: {T.TEXT_DIM};
  font-size: {FS_META}px;
}}
QWidget#SuiteDropZone {{
  border: 1px dashed {T.BORDER};
  border-radius: 4px;
  background: transparent;
  margin: 4px 8px;
}}
QWidget#SuiteDropZone:hover {{
  border-color: {T.ACCENT};
}}
/* Toolbar actions — same height; Ghost has visible border like Secondary */
QPushButton#PrimaryButton,
QPushButton#GhostButton,
QPushButton#SecondaryButton {{
  font-size: {FS_CTRL}px;
  padding: 0 10px;
  min-height: {CTRL_H}px;
  max-height: {CTRL_H}px;
  border-radius: 3px;
}}
QPushButton#GhostButton {{
  background: {T.EDITOR};
  color: {T.TEXT};
  border: 1px solid #CECECE;
  font-weight: 500;
}}
QPushButton#GhostButton:hover {{
  background: {T.SIDEBAR_HOVER};
  border-color: #B8B8B8;
  color: {T.TEXT};
}}
QPushButton#PrimaryButton {{
  background: {T.ACCENT};
  color: #FFFFFF;
  border: 1px solid {T.ACCENT};
  font-weight: 600;
}}
QToolButton#SuiteIconTool {{
  background: transparent;
  border: 1px solid transparent;
  border-radius: 4px;
  padding: 0;
  margin: 0;
  min-width: {CTRL_H}px;
  max-width: {CTRL_H}px;
  min-height: {CTRL_H}px;
  max-height: {CTRL_H}px;
}}
QToolButton#SuiteIconTool:hover {{
  background: {T.SIDEBAR_HOVER};
  border-color: {T.BORDER_SOFT};
}}
QToolButton#SuiteIconTool:pressed {{
  background: {T.BORDER_SOFT};
}}
QToolButton#SuiteIconTool:disabled {{
  opacity: 0.45;
}}
QLineEdit, QComboBox {{
  font-size: {FS_CTRL}px;
  min-height: {CTRL_H}px;
  max-height: {CTRL_H}px;
}}
"""


def apply_dbc_chrome(window: QWidget) -> None:
    """Stack industrial overlay on top of shared vscode_theme."""
    T.apply(window)
    window.setStyleSheet(window.styleSheet() + "\n" + DBC_OVERLAY)


def hairline() -> QFrame:
    line = QFrame()
    line.setObjectName("SuiteHairline")
    line.setFixedHeight(1)
    line.setFrameShape(QFrame.Shape.HLine)
    return line


def tool_strip(*widgets, stretch_at: int | None = None) -> QWidget:
    """Single-row IDE toolbar — tall enough for full CTRL_H field borders."""
    from PyQt6.QtWidgets import QCheckBox, QComboBox, QLineEdit
    host = QWidget()
    host.setObjectName("SuiteToolStrip")
    host.setFixedHeight(TOOL_H)
    host.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
    lay = QHBoxLayout(host)
    lay.setContentsMargins(PAD_X, STRIP_PAD_V, PAD_X, STRIP_PAD_V)
    lay.setSpacing(GAP)
    for i, w in enumerate(widgets):
        if stretch_at is not None and i == stretch_at:
            lay.addStretch(1)
        if w is not None:
            try:
                name = w.objectName() if hasattr(w, "objectName") else ""
                if isinstance(w, QToolButton) and name == "SuiteIconTool":
                    w.setFixedSize(CTRL_H, CTRL_H)
                elif name == "StepSpin" or isinstance(
                        w, (QLineEdit, QComboBox, QPushButton, QCheckBox)):
                    w.setFixedHeight(CTRL_H)
            except Exception:
                pass
            lay.addWidget(w, 0, Qt.AlignmentFlag.AlignVCenter)
    if stretch_at is None:
        lay.addStretch(1)
    return host


def ghost_btn(text: str, tip: str, icon: str = "") -> QPushButton:
    """Secondary toolbar action — 28px, visible border (same chrome as Primary)."""
    btn = QPushButton(text)
    btn.setObjectName("GhostButton")
    btn.setFixedHeight(CTRL_H)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    if icon:
        codicons.set_button(btn, icon, color=ICON_FG, size=ICON)
    return btn


def primary_btn(text: str, tip: str, icon: str = "") -> QPushButton:
    """Primary action — white glyph on accent."""
    btn = QPushButton(text)
    btn.setObjectName("PrimaryButton")
    btn.setFixedHeight(CTRL_H)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    if icon:
        codicons.set_button(btn, icon, color="#FFFFFF", size=ICON, primary=True)
    return btn


def icon_tool(icon: str, tip: str, *, size: int = ICON_TOOL) -> QToolButton:
    """Icon-only tool — 28×28 hit target, 16px glyph."""
    btn = QToolButton()
    btn.setObjectName("SuiteIconTool")
    btn.setAutoRaise(True)
    btn.setFixedSize(CTRL_H, CTRL_H)
    btn.setIconSize(QSize(size, size))
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
    btn.setToolTip(tip)
    codicons.set_button(btn, icon, color=ICON_FG, size=size)
    return btn


def chip_btn(text: str, tip: str, icon: str = "") -> QPushButton:
    """Header chip — same height as fields."""
    btn = QPushButton(text)
    btn.setObjectName("GhostButton")
    btn.setFixedHeight(CTRL_H)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    if icon:
        codicons.set_button(btn, icon, color=ICON_FG, size=ICON_CHIP)
    return btn


def suite_spin(
        value: int = 0,
        *,
        minimum: int = 0,
        maximum: int = 99,
        hex_mode: bool = False,
        tip: str = "",
        width: int = 120,
        steppers: bool = True):
    """Unified numeric control (identical steppers, 28px)."""
    from _shared.widgets import StepSpin
    w = StepSpin(
        value, minimum=minimum, maximum=maximum,
        hex_mode=hex_mode, width=width, steppers=steppers)
    if tip:
        w.setToolTip(tip)
    return w


def count_label(text: str = "") -> QLabel:
    """Meta / status text — SuiteCount (11px muted). Prefer over ad-hoc styles."""
    lab = QLabel(text)
    lab.setObjectName("SuiteCount")
    return lab


def quiet_label(text: str) -> QLabel:
    lab = QLabel(text)
    lab.setObjectName("SuiteHint")
    lab.setWordWrap(True)
    return lab


def next_step_bar(label: str, *actions) -> QWidget:
    """One quiet handoff row: hint + 1–2 action widgets."""
    host = QWidget()
    host.setObjectName("SuiteNextStep")
    row = QHBoxLayout(host)
    row.setContentsMargins(PAD_X, 2, PAD_X, 2)
    row.setSpacing(GAP)
    tip = QLabel(label or "")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(False)
    row.addWidget(tip, 1)
    for w in actions:
        if w is not None:
            row.addWidget(w, 0)
    return host


def empty_state(
        title: str,
        hint: str = "",
        actions: Optional[list] = None) -> QWidget:
    """Centered empty placeholder — kit typography, optional CTAs."""
    host = QWidget()
    host.setObjectName("SuiteEmptyState")
    lay = QVBoxLayout(host)
    lay.setContentsMargins(PAD_X * 2, 40, PAD_X * 2, 24)
    lay.addStretch(1)
    t = QLabel(title)
    t.setObjectName("SuiteEmptyTitle")
    t.setAlignment(Qt.AlignmentFlag.AlignHCenter)
    t.setWordWrap(True)
    lay.addWidget(t)
    if hint:
        h = QLabel(hint)
        h.setObjectName("SuiteEmptyHint")
        h.setAlignment(Qt.AlignmentFlag.AlignHCenter)
        h.setWordWrap(True)
        lay.addWidget(h)
    if actions:
        row = QHBoxLayout()
        row.setSpacing(GAP)
        row.addStretch(1)
        for w in actions:
            row.addWidget(w)
        row.addStretch(1)
        lay.addSpacing(12)
        lay.addLayout(row)
    lay.addStretch(2)
    return host


def section_title(text: str) -> QLabel:
    """Side Bar section title — sentence case, 12px/600 (VS Code explorer)."""
    lab = QLabel(text)
    lab.setObjectName("SuiteSectionTitle")
    return lab


def prop_panel_title(text: str) -> QLabel:
    lab = QLabel(text)
    lab.setObjectName("SuitePropTitle")
    return lab


def field_label(text: str) -> QLabel:
    lab = QLabel(text)
    lab.setObjectName("SuiteFieldLabel")
    return lab


def strip_field(label: str, widget: QWidget, *, tip: str = "") -> QWidget:
    """Label + control for tool strips — never leave a bare value floating."""
    host = QWidget()
    host.setObjectName("SuiteStripField")
    row = QHBoxLayout(host)
    row.setContentsMargins(0, 0, 0, 0)
    row.setSpacing(4)
    lab = field_label(label)
    if tip:
        lab.setToolTip(tip)
        try:
            widget.setToolTip(tip)
        except Exception:
            pass
    row.addWidget(lab, 0, Qt.AlignmentFlag.AlignVCenter)
    row.addWidget(widget, 0, Qt.AlignmentFlag.AlignVCenter)
    return host


def panel_header(title: str, *trailing) -> QWidget:
    """Property-pane title row — same TOOL_H as sidebar_header."""
    head = QWidget()
    head.setFixedHeight(TOOL_H)
    lay = QHBoxLayout(head)
    lay.setContentsMargins(PAD_X, 0, 6, 0)
    lay.setSpacing(4)
    lab = QLabel(title)
    lab.setObjectName("SuitePropTitle")
    lay.addWidget(lab, 1, Qt.AlignmentFlag.AlignVCenter)
    for w in trailing:
        if w is not None:
            lay.addWidget(w, 0, Qt.AlignmentFlag.AlignVCenter)
    return head


def inline_filter(*widgets) -> QWidget:
    """Filter / multi-control row — FILTER_H so CTRL_H widgets never clip.

    Pass one or more widgets; the first QLineEdit stretches.
    """
    from PyQt6.QtWidgets import QLineEdit
    row = QWidget()
    row.setObjectName("SuiteInlineFilter")
    row.setFixedHeight(FILTER_H)
    lay = QHBoxLayout(row)
    lay.setContentsMargins(PAD_X, STRIP_PAD_V, PAD_X, STRIP_PAD_V)
    lay.setSpacing(GAP)
    align = Qt.AlignmentFlag.AlignVCenter
    for w in widgets:
        if w is None:
            continue
        if isinstance(w, QLineEdit):
            w.setFixedHeight(CTRL_H)
            lay.addWidget(w, 1, align)
        else:
            try:
                w.setFixedHeight(CTRL_H)
            except Exception:
                pass
            lay.addWidget(w, 0, align)
    return row


def style_tree(tree: QTreeWidget, *, header_hidden: bool = True) -> None:
    tree.setObjectName("SuiteMatrix")
    tree.setStyleSheet(TREE_STYLE)
    tree.setRootIsDecorated(True)
    tree.setUniformRowHeights(True)
    tree.setAnimated(True)
    tree.setIndentation(12)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
    tree.setHeaderHidden(header_hidden)
    tree.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    tree.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    tree.setVerticalScrollMode(QAbstractItemView.ScrollMode.ScrollPerPixel)
    tree.setTextElideMode(Qt.TextElideMode.ElideRight)
    if not header_hidden:
        configure_columns(tree, stretch=0 if tree.columnCount() == 1 else None)


def style_list(list_widget) -> None:
    """Catalog / nav lists — full glyph height, no clipped rows."""
    from PyQt6.QtWidgets import QListWidget
    if not isinstance(list_widget, QListWidget):
        return
    list_widget.setObjectName("SuiteMatrix")
    list_widget.setStyleSheet(LIST_STYLE)
    list_widget.setUniformItemSizes(True)
    list_widget.setSpacing(1)
    list_widget.setHorizontalScrollBarPolicy(
        Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
    list_widget.setVerticalScrollBarPolicy(
        Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    list_widget.setVerticalScrollMode(
        QAbstractItemView.ScrollMode.ScrollPerPixel)
    list_widget.setTextElideMode(Qt.TextElideMode.ElideRight)


def style_table(table: QTableWidget) -> None:
    """Trace / matrix tables — 12px body, soft grid."""
    if table is None:
        return
    table.setObjectName("SuiteMatrix")
    table.setStyleSheet(TABLE_STYLE)
    table.setAlternatingRowColors(True)
    table.setShowGrid(False)
    if table.verticalHeader() is not None:
        table.verticalHeader().setVisible(False)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)


def golden_sizes(*, master_left: bool = True, total: int = 1000) -> list:
    """Return [left, right] sizes at the golden ratio.

    ``master_left=True`` — left is the primary work surface (tree / matrix).
    ``master_left=False`` — left is a secondary catalog / nav strip.
    """
    major = int(round(total / PHI))           # ≈ 0.618 of total
    minor = max(1, total - major)             # ≈ 0.382 of total
    if master_left:
        return [major, minor]
    return [minor, major]


def configure_columns(
        view,
        *,
        stretch: Optional[int] = None,
        min_section: int = 56,
        mins: Optional[dict] = None) -> None:
    """Make every column Interactive (draggable) with content-first defaults.

    ``stretch`` — column index that absorbs leftover viewport width after a fit.
    ``mins`` — optional ``{col: min_px}`` so narrow columns (Access) never clip.
    Call ``fit_columns(view, stretch=…)`` again after the model is rebuilt.
    """
    hdr = _header_of(view)
    if hdr is None:
        return
    count = hdr.count()
    if count <= 0 and hasattr(view, "columnCount"):
        count = int(view.columnCount())
    hdr.setStretchLastSection(False)
    hdr.setMinimumSectionSize(min_section)
    hdr.setDefaultSectionSize(max(min_section, 96))
    hdr.setSectionsMovable(False)
    hdr.setSectionsClickable(True)
    Interactive = QHeaderView.ResizeMode.Interactive
    for i in range(count):
        hdr.setSectionResizeMode(i, Interactive)
    if mins:
        view.setProperty("suiteColMins", dict(mins))
    if stretch is not None and 0 <= stretch < count:
        # Keep Interactive so the user can still drag; leftover filled in fit_columns.
        view.setProperty("suiteStretchCol", stretch)
    fit_columns(view, stretch=stretch)


def fit_columns(view, *, stretch: Optional[int] = None) -> None:
    """Resize columns to contents, then give leftover space to ``stretch``."""
    hdr = _header_of(view)
    if hdr is None:
        return
    count = hdr.count()
    if count <= 0:
        return
    if stretch is None:
        prop = view.property("suiteStretchCol")
        if prop is not None:
            try:
                stretch = int(prop)
            except (TypeError, ValueError):
                stretch = None
    col_mins = view.property("suiteColMins") or {}
    if not isinstance(col_mins, dict):
        col_mins = {}
    resize = getattr(view, "resizeColumnToContents", None)
    if resize is None:
        return
    for i in range(count):
        resize(i)
        floor = int(col_mins.get(i, hdr.minimumSectionSize()))
        if hdr.sectionSize(i) < floor:
            hdr.resizeSection(i, floor)
    if stretch is None or not (0 <= stretch < count):
        return
    # Defer leftover expansion until the viewport has a real width.
    def _expand():
        used = sum(hdr.sectionSize(i) for i in range(count))
        vp = view.viewport().width() if view.viewport() is not None else 0
        # Leave room for vertical scrollbar so the last column is never clipped.
        sb = view.verticalScrollBar()
        sb_w = sb.sizeHint().width() if sb is not None else 14
        extra = max(0, vp - used - sb_w - 4)
        if extra > 0:
            hdr.resizeSection(stretch, hdr.sectionSize(stretch) + extra)

    from PyQt6.QtCore import QTimer
    QTimer.singleShot(0, _expand)
    QTimer.singleShot(50, _expand)


def configure_splitter(
        split: QSplitter,
        *,
        horizontal: bool = True,
        sizes: Optional[list] = None,
        stretch: Optional[tuple] = None,
        master_left: bool = True,
        golden: bool = False,
        handle_width: int = 6) -> QSplitter:
    """Draggable pane divider — live opaque resize (both sides change width).

    Never rubber-band / overlay: ``setOpaqueResize(True)``. Children get
    Preferred size policies and cleared max caps so drag reallocates pixels
    between panes instead of one painting over the other.
    """
    from PyQt6.QtWidgets import QSizePolicy
    split.setObjectName("SuiteHSplitter" if horizontal else "SuiteVSplitter")
    split.setHandleWidth(max(4, int(handle_width)))
    split.setChildrenCollapsible(False)
    split.setOpaqueResize(True)
    policy_h = (
        QSizePolicy.Policy.Preferred if horizontal
        else QSizePolicy.Policy.Expanding)
    policy_v = (
        QSizePolicy.Policy.Expanding if horizontal
        else QSizePolicy.Policy.Preferred)
    for i in range(split.count()):
        w = split.widget(i)
        if w is None:
            continue
        w.setMaximumWidth(16777215)
        w.setMaximumHeight(16777215)
        w.setSizePolicy(policy_h, policy_v)
        if horizontal:
            w.setMinimumWidth(max(120, min(200, w.minimumWidth() or 120)))
        else:
            w.setMinimumHeight(max(80, w.minimumHeight() or 0))
    if stretch is None and golden:
        # Equal stretch so window resize and drag share space fairly.
        stretch = (1, 1)
    if stretch:
        for i, factor in enumerate(stretch):
            if i < split.count():
                split.setStretchFactor(i, int(factor))
    if sizes is None and golden:
        sizes = golden_sizes(master_left=master_left)
    if sizes:
        split.setSizes([int(s) for s in sizes])
    return split


def _header_of(view):
    if isinstance(view, QTreeWidget):
        return view.header()
    if isinstance(view, QTableWidget):
        return view.horizontalHeader()
    hdr = getattr(view, "header", None)
    if callable(hdr):
        return hdr()
    hh = getattr(view, "horizontalHeader", None)
    if callable(hh):
        return hh()
    return None


def sidebar_header(title: str, *trailing) -> QWidget:
    head = QWidget()
    head.setObjectName("SuiteSideBarHeader")
    head.setFixedHeight(TOOL_H)
    hl = QHBoxLayout(head)
    hl.setContentsMargins(PAD_X, 0, 6, 0)
    hl.setSpacing(4)
    hl.addWidget(section_title(title), 1)
    for w in trailing:
        if w is not None:
            hl.addWidget(w, 0, Qt.AlignmentFlag.AlignVCenter)
    return head


def split_editor(left: QWidget, right: QWidget, *, left_ratio: int = 7) -> QSplitter:
    """Classic IDE split: master list | property pane at golden ratio."""
    del left_ratio  # kept for call-site compat; golden ratio is authoritative
    split = QSplitter(Qt.Orientation.Horizontal)
    left.setMinimumWidth(PANE_MIN)
    right.setObjectName("SuitePropPanel")
    right.setMinimumWidth(PANE_MIN_PROP)
    split.addWidget(left)
    split.addWidget(right)
    return configure_splitter(split, golden=True, master_left=True)
