# -*- coding: utf-8 -*-
"""VS Code–inspired theme for domain suite windows.

Flat light surface matching the host window: idle chrome has no gray fills;
selection and hover carry emphasis; hairline dividers separate major panes.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFormLayout,
    QFrame,
    QHBoxLayout,
    QLabel,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

# Soft palette — flat light theme (match main window; selection / hover only tint).
BG = "#FFFFFF"
EDITOR = "#FFFFFF"
SIDEBAR = "#FFFFFF"
SIDEBAR_HOVER = "#F0F0F0"
SIDEBAR_ACTIVE = "#FFFFFF"
BORDER = "#D0D0D0"
BORDER_SOFT = "#E0E0E0"
BLOCK_BORDER = "#D0D0D0"  # kept for compat; sections are borderless now

TEXT = "#3B3B3B"
TEXT_DIM = "#6C6C6C"
TEXT_MUTED = "#8A8A8A"
TEXT_SIDE = "#3B3B3B"

# Type scale (§13.14) — every suite must use these; no local 13/14 “title walls”.
FS_BODY = 12   # work surface: trees, tabs, forms, section titles, buttons
FS_CTRL = 12   # controls (= body)
FS_META = 11   # muted chrome / side-bar titles / status hints
FS_MENU = 12   # menubar labels (same as body)
FS_MONO = 12   # paths / hex / code
ACCENT = "#005FB8"
ACCENT_SOFT = "#CCE8FF"
ACCENT_HOVER = "#1F7AD3"
WARN_BG = "#FFF8E8"
WARN_FG = "#8A6D1D"
WARN_BORDER = "#F0E0A8"
ERR = "#C72E0F"
OK = "#388A34"
TX = "#007ACC"
RX = "#388A34"
FC = "#6A4C93"
RADIUS = "4px"
CTRL_H = "28px"


def v_divider() -> QFrame:
    line = QFrame()
    line.setFrameShape(QFrame.Shape.VLine)
    line.setObjectName("SuiteVDivider")
    line.setFixedWidth(1)
    return line


def h_divider() -> QFrame:
    line = QFrame()
    line.setFrameShape(QFrame.Shape.HLine)
    line.setObjectName("SuiteHDivider")
    line.setFixedHeight(1)
    return line


def caption(text: str) -> QLabel:
    """One-line section hint under a heading."""
    lab = QLabel(text)
    lab.setObjectName("SuiteHint")
    lab.setWordWrap(True)
    return lab


def panel(title: str, hint: str = "", body: QWidget | None = None) -> QWidget:
    """Flat section: title + hint + body. No card border (spacing does the work)."""
    card = QWidget()
    card.setObjectName("SuiteSection")
    lay = QVBoxLayout(card)
    lay.setContentsMargins(0, 4, 0, 12)
    lay.setSpacing(4)
    if title:
        t = QLabel(title)
        t.setObjectName("SuiteSectionTitle")
        lay.addWidget(t)
    if hint:
        lay.addWidget(caption(hint))
    if body is not None:
        lay.addWidget(body, 1)
    return card


def section(title: str, body: QWidget | None = None) -> QWidget:
    return panel(title, "", body)


def block(title: str, hint: str = "", trailing: QWidget | None = None) -> tuple[QWidget, QVBoxLayout]:
    """Flat section — title / hint / body. Grouped by whitespace, not boxes.

    Industry pattern (VS Code / Linear): one continuous surface; borders only on
    interactive controls and the rare pane split.
    """
    card = QWidget()
    card.setObjectName("SuiteBlock")
    outer = QVBoxLayout(card)
    outer.setContentsMargins(0, 2, 0, 10)
    outer.setSpacing(6)

    head = QWidget()
    head.setObjectName("SuiteBlockHead")
    hl = QHBoxLayout(head)
    hl.setContentsMargins(0, 0, 0, 0)
    hl.setSpacing(8)
    titles = QVBoxLayout()
    titles.setContentsMargins(0, 0, 0, 0)
    titles.setSpacing(1)
    t = QLabel(title)
    t.setObjectName("SuiteSectionTitle")
    titles.addWidget(t)
    if hint:
        titles.addWidget(caption(hint))
    hl.addLayout(titles, 1)
    if trailing is not None:
        hl.addWidget(trailing, 0, Qt.AlignmentFlag.AlignVCenter)
    outer.addWidget(head)

    body = QWidget()
    body.setObjectName("SuiteBlockBody")
    bl = QVBoxLayout(body)
    bl.setContentsMargins(0, 0, 0, 0)
    bl.setSpacing(8)
    outer.addWidget(body, 1)
    return card, bl


def field_label(text: str) -> QLabel:
    """Form-row noun — SuiteFieldLabel @ FS_BODY (use instead of addRow string)."""
    lab = QLabel(text)
    lab.setObjectName("SuiteFieldLabel")
    return lab


def value_label(text: str = "—", *, mono: bool = False) -> QLabel:
    """Read-only form value — quiet body or monospace."""
    lab = QLabel(text)
    lab.setObjectName("SuiteMono" if mono else "SuiteQuiet")
    lab.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
    return lab


def polish_form_labels(form: QFormLayout) -> None:
    """Tag bare QFormLayout labels as SuiteFieldLabel (call after addRow)."""
    if form is None:
        return
    for i in range(form.rowCount()):
        item = form.itemAt(i, QFormLayout.ItemRole.LabelRole)
        if item is None:
            continue
        w = item.widget()
        if not isinstance(w, QLabel):
            continue
        name = w.objectName() or ""
        if name.startswith("Suite"):
            continue
        w.setObjectName("SuiteFieldLabel")


def tune_form(form) -> None:
    """Right-aligned labels, even rows, fields grow together."""
    form.setLabelAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
    form.setFormAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
    form.setHorizontalSpacing(12)
    form.setVerticalSpacing(8)
    form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
    polish_form_labels(form)


def stylesheet() -> str:
    import os
    _icons = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")
    check_on = os.path.join(_icons, "check-white.svg").replace("\\", "/")
    return f"""
/* ===== Shell — VS Code Light type scale: 12 work / 11 meta ===== */
QMainWindow, QDialog {{
    background: {EDITOR};
    color: {TEXT};
    font-size: {FS_BODY}px;
    font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
}}
/* Bare QLabels (QFormLayout.addRow strings, value text) — force body scale.
   Named Suite* labels override below. Prevents “giant form labels” vs 12px inputs. */
QLabel {{
    color: {TEXT};
    font-size: {FS_BODY}px;
    font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
}}
QStatusBar {{
    background: {EDITOR};
    color: {TEXT_MUTED};
    border-top: 1px solid {BORDER};
    font-size: {FS_BODY}px;
    min-height: 22px;
}}
QStatusBar::item {{ border: none; }}

/* ===== Title bar + layout toggles (VS Code workbench) ===== */
QWidget#SuiteTitleBar {{
    background: {SIDEBAR};
    border-bottom: 1px solid {BORDER};
}}
QLabel#SuiteTitleLabel {{
    color: {TEXT};
    font-size: {FS_BODY}px;
    font-weight: 600;
}}
QLabel#SuiteTitleChip {{
    color: {TEXT_DIM};
    font-size: {FS_BODY}px;
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
    padding: 3px 10px;
    background: {EDITOR};
    border: 1px solid {BORDER};
    border-radius: 12px;
}}
QLabel#SuiteTitleChip:hover {{
    border-color: {ACCENT};
    color: {TEXT};
}}
QToolButton#LayoutToggleBtn {{
    background: transparent;
    border: 1px solid transparent;
    border-radius: 0;
    padding: 0;
    margin: 0;
    min-width: 36px;
    max-width: 36px;
    min-height: 34px;
    max-height: 34px;
}}
QToolButton#LayoutToggleBtn:hover {{
    background: {SIDEBAR_HOVER};
    border-color: transparent;
}}
QToolButton#LayoutToggleBtn:checked {{
    background: {ACCENT_SOFT};
    border-color: transparent;
}}
QToolButton#LayoutToggleBtn:pressed {{
    background: {BORDER};
}}

/* ===== Activity bar (VS Code icon rail) ===== */
QWidget#SuiteActivityBar {{
    background: {SIDEBAR};
    border-right: 1px solid {BORDER};
    min-width: 48px;
    max-width: 48px;
}}
QToolButton#ActivityBtn {{
    background: transparent;
    border: none;
    border-left: 2px solid transparent;
    border-radius: 0;
    padding: 0;
    margin: 0;
}}
QToolButton#ActivityBtn:hover {{
    background: {SIDEBAR_HOVER};
}}
QToolButton#ActivityBtn:checked {{
    background: {EDITOR};
    border-left: 2px solid {ACCENT};
}}

/* ===== Activity nav (legacy wide list) ===== */
QListWidget#SuiteNav {{
    background: {SIDEBAR};
    color: {TEXT};
    border: none;
    border-right: 1px solid {BORDER};
    outline: none;
    padding: 0;
    font-size: {FS_BODY}px;
}}
QListWidget#SuiteNav::item {{
    padding: 0 12px;
    margin: 0;
    min-height: 22px;
    max-height: 22px;
    border: none;
    border-radius: 0;
    background: transparent;
}}
QListWidget#SuiteNav::item:hover {{
    background: {SIDEBAR_HOVER};
    border: none;
}}
QListWidget#SuiteNav::item:selected {{
    background: {ACCENT_SOFT};
    color: {TEXT};
    font-weight: 600;
    border: none;
}}

/* ===== Shared connection toolbar ===== */
QWidget#SuiteToolbar {{
    background: {EDITOR};
    border-bottom: 1px solid {BORDER};
}}
QWidget#SuiteToolbar QLabel {{
    color: {TEXT_DIM};
    font-size: {FS_BODY}px;
}}
QWidget#SuiteToolbar QLabel#SuiteToolbarTitle {{
    color: {TEXT_MUTED};
    font-size: {FS_META}px;
    font-weight: 600;
    letter-spacing: 0.4px;
    padding-right: 4px;
}}
QLabel#SuiteToolbarTitle {{
    color: {TEXT_MUTED};
    font-size: {FS_META}px;
    font-weight: 600;
    letter-spacing: 0.4px;
}}

/* ===== Side Bar explorer (VS Code) =====
   Viewlet title = .part > .title (35px) + .title-label h2 (11px uppercase).
   Pane section headers stay 22px elsewhere — do NOT reuse that here. */
QWidget#SuiteSideBar, QWidget#WorkspaceSideBar, QWidget#ConfigSideBar {{
    background: {SIDEBAR};
    border: none;
}}
QWidget#SuiteSideBarHeader {{
    background: {SIDEBAR};
    border: none;
    border-bottom: 1px solid {BORDER_SOFT};
    min-height: 35px;
}}
QLabel#SuiteSideBarTitle {{
    color: {TEXT_MUTED};
    font-size: {FS_META}px;
    font-weight: 700;
    letter-spacing: 0.5px;
    padding: 0 2px;
}}
/* Multi-line document strip above Side Bar explorer (CANopen / suites).
   Must NOT share SuiteSideBarHeader — that header is height-capped. */
QWidget#SuiteDocBanner {{
    background: {SIDEBAR};
    border: none;
    border-bottom: 1px solid {BORDER_SOFT};
    min-height: 56px;
    max-height: 120px;
}}
QLabel#SuiteDocPath {{
    color: {TEXT_MUTED};
    font-size: {FS_BODY}px;
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
    padding: 0 6px;
}}
QLabel#SuiteDirtyDot {{
    color: {ACCENT};
    font-size: {FS_BODY}px;
    font-weight: 700;
    padding: 0 2px;
}}
QTreeWidget#SuiteMatrix {{
    background: transparent;
    border: none;
    outline: 0;
    font-size: {FS_BODY}px;
}}
QTreeWidget#SuiteMatrix::item {{
    padding: 2px 4px;
    min-height: 22px;
}}
QTreeWidget#SuiteMatrix::item:hover {{
    background: {SIDEBAR_HOVER};
}}
QTreeWidget#SuiteMatrix::item:selected {{
    background: {ACCENT_SOFT};
    color: {TEXT};
}}
QTableWidget#SuiteMatrix {{
    border: none;
    gridline-color: {BORDER_SOFT};
    outline: 0;
}}
QTableWidget#SuiteMatrix::item:selected {{
    background: {ACCENT_SOFT};
    color: {TEXT};
}}
QPlainTextEdit#BswSpecPane {{
    background: {EDITOR};
    color: {TEXT_MUTED};
    font-size: {FS_BODY}px;
    border: none;
    border-top: 1px solid {BORDER_SOFT};
    padding: 6px 8px;
}}
QToolButton#SuitePanelTab {{
    background: transparent;
    color: {TEXT_MUTED};
    border: none;
    border-bottom: 1px solid transparent;
    border-radius: 0;
    padding: 2px 8px;
    font-size: {FS_META}px;
    font-weight: 600;
}}
QToolButton#SuitePanelTab:hover {{
    color: {TEXT};
}}
QToolButton#SuitePanelTab:checked {{
    color: {TEXT};
    border-bottom: 1px solid {ACCENT};
}}
QWidget#SuiteMenuChromeSlot {{
    background: {SIDEBAR};
    border: none;
    margin: 0;
    padding: 0;
}}
QWidget#SuiteMenuChrome {{
    background: {SIDEBAR};
    border: none;
    border-bottom: 1px solid {BORDER};
    min-height: 35px;
    max-height: 35px;
}}
QWidget#SuiteMenuButtons {{
    background: {SIDEBAR};
}}
QFrame#SuiteMenuSep {{
    background: {BORDER_SOFT};
    border: none;
    margin: 0 4px;
    max-width: 1px;
}}
QWidget#SuiteMenubarTrailing {{
    background: transparent;
    min-height: 35px;
    max-height: 35px;
}}
QWidget#SuiteMenubarTrailing QToolButton#LayoutToggleBtn {{
    margin: 0;
    min-width: 36px;
    max-width: 36px;
    min-height: 35px;
    max-height: 35px;
}}
QToolButton#WinMinBtn, QToolButton#WinMaxBtn {{
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 0;
    margin: 0;
    min-width: 46px;
    max-width: 46px;
    min-height: 35px;
    max-height: 35px;
}}
QToolButton#WinMinBtn:hover, QToolButton#WinMaxBtn:hover {{
    background: {SIDEBAR_HOVER};
}}
QToolButton#WinMinBtn:pressed, QToolButton#WinMaxBtn:pressed {{
    background: {BORDER};
}}
QToolButton#WinCloseBtn {{
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 0;
    margin: 0;
    min-width: 46px;
    max-width: 46px;
    min-height: 35px;
    max-height: 35px;
}}
QToolButton#WinCloseBtn:hover {{
    background: #E81123;
}}
QToolButton#WinCloseBtn:pressed {{
    background: #F1707A;
}}
QMainWindow[suiteFrameless="true"] {{
    border: 1px solid {BORDER};
}}
QMenuBar {{
    background: {SIDEBAR};
    color: {TEXT};
    border: none;
    padding: 0 4px;
    spacing: 0;
    min-height: 30px;
}}
QMenuBar::item {{
    background: transparent;
    padding: 4px 8px;
    border-radius: 0;
    color: {TEXT};
    font-size: {FS_MENU}px;
}}
QMenuBar::item:selected {{
    background: {SIDEBAR_HOVER};
}}
QMenuBar::item:pressed {{
    background: {SIDEBAR_HOVER};
}}
QLabel#SessionBadge {{
    color: {ACCENT};
    font-weight: 600;
    font-size: {FS_META}px;
    padding: 2px 10px;
    background: {ACCENT_SOFT};
    border: none;
    border-radius: 10px;
}}
QWidget#SuiteToolbar QSpinBox {{
    background: {EDITOR};
    border: 1px solid #CECECE;
    border-radius: 3px;
    padding: 0px 8px;
    min-height: 26px;
    max-height: 26px;
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
    font-size: {FS_BODY}px;
    color: {TEXT};
}}
QWidget#SuiteToolbar QSpinBox:focus {{
    border: 1px solid {ACCENT};
}}
QFrame#SuiteVDivider {{
    background: {BORDER};
    max-width: 1px;
    margin: 2px 6px;
}}
QFrame#SuiteHDivider {{
    background: {BORDER_SOFT};
    max-height: 1px;
}}

/* ===== Content host — flat surface, borders only on controls / pane splits ===== */
QWidget#SuiteContent {{
    background: {EDITOR};
}}
QWidget#SuiteSection, QWidget#BusStrip, QWidget#SuiteBlock {{
    background: transparent;
    border: none;
}}
QWidget#SuiteQuickBar {{
    background: {SIDEBAR};
    border-bottom: 1px solid {BORDER};
}}
QWidget#SuiteStarters QPushButton#GhostButton {{
    padding: 0 10px;
    color: {ACCENT};
}}
QWidget#SuiteStarters QPushButton#GhostButton:hover {{
    background: {ACCENT_SOFT};
    color: {ACCENT_HOVER};
}}
QWidget#StepSpin {{
    background: {EDITOR};
    border: 1px solid #C8C8C8;
    border-radius: 3px;
    /* Outer height from setFixedHeight(28) — do not set min/max here
       (Qt content-box min-height + border expands past the strip). */
}}
QWidget#StepSpin:hover {{
    border-color: #A8A8A8;
}}
QWidget#StepSpin QSpinBox#SuiteSpin {{
    background: transparent;
    color: {TEXT};
    border: none;
    border-radius: 0;
    padding: 0px 6px;
    min-height: 26px;
    max-height: 26px;
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
    font-size: {FS_BODY}px;
    selection-background-color: {ACCENT_SOFT};
}}
QWidget#StepSpin QSpinBox#SuiteSpin:focus {{
    border: none;
}}
QWidget#StepSpinButtons {{
    border-left: 1px solid #E0E0E0;
    background: transparent;
}}
QToolButton#StepSpinBtn {{
    border: none;
    background: transparent;
    padding: 0;
    margin: 0;
    border-radius: 0;
}}
QToolButton#StepSpinBtn:hover {{
    background: {SIDEBAR_HOVER};
}}
QSpinBox#SuiteSpin {{
    background: {EDITOR};
    color: {TEXT};
    border: 1px solid #C8C8C8;
    border-radius: 3px;
    padding: 0px 20px 0px 8px;
    min-height: 26px;
    max-height: 26px;
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
    font-size: {FS_BODY}px;
    selection-background-color: {ACCENT_SOFT};
}}
QSpinBox#SuiteSpin:hover {{
    border-color: #A8A8A8;
}}
QSpinBox#SuiteSpin:focus {{
    border: 1px solid {ACCENT};
}}
/* Identical stepper geometry for every SuiteSpin (hex and decimal) */
QSpinBox#SuiteSpin::up-button,
QSpinBox#SuiteSpin::down-button {{
    subcontrol-origin: border;
    width: 18px;
    border: none;
    border-left: 1px solid #E0E0E0;
    background: {EDITOR};
}}
QSpinBox#SuiteSpin::up-button {{
    subcontrol-position: top right;
    height: 14px;
    border-top-right-radius: 3px;
}}
QSpinBox#SuiteSpin::down-button {{
    subcontrol-position: bottom right;
    height: 14px;
    border-bottom-right-radius: 3px;
}}
QSpinBox#SuiteSpin::up-button:hover,
QSpinBox#SuiteSpin::down-button:hover {{
    background: {SIDEBAR_HOVER};
}}
QLabel#SuiteFieldLabel {{
    color: {TEXT_DIM};
    font-size: {FS_BODY}px;
    font-weight: 500;
}}
QLabel#SuiteQuiet {{
    color: {TEXT_DIM};
    font-size: {FS_BODY}px;
}}
QLabel#SuiteMono {{
    color: {TEXT};
    font-size: {FS_MONO}px;
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
}}
QLabel#SuiteCount {{
    color: {TEXT_MUTED};
    font-size: {FS_META}px;
}}
QWidget#SuiteSettingsSection {{
    background: transparent;
    border: none;
    padding: 0;
}}
QWidget#SuiteSettingsPage {{
    background: {EDITOR};
}}
QFrame#SuiteHDivider {{
    background: transparent;
    border: none;
    max-height: 0;
    min-height: 0;
    margin: 0;
}}
QPushButton#SegmentBtn {{
    background: {EDITOR};
    color: {TEXT};
    border: 1px solid #CECECE;
    border-radius: 0;
    padding: 0 14px;
    min-height: 26px;
    max-height: 26px;
    font-size: {FS_BODY}px;
    font-weight: 500;
}}
QPushButton#SegmentBtn[segment="first"] {{
    border-top-left-radius: 3px;
    border-bottom-left-radius: 3px;
}}
QPushButton#SegmentBtn[segment="last"] {{
    border-top-right-radius: 3px;
    border-bottom-right-radius: 3px;
    margin-left: -1px;
}}
QPushButton#SegmentBtn[segment="mid"] {{
    margin-left: -1px;
}}
QPushButton#SegmentBtn:hover {{
    background: {SIDEBAR_HOVER};
    color: {TEXT};
}}
QPushButton#SegmentBtn:checked {{
    background: {ACCENT_SOFT};
    color: {ACCENT};
    border-color: {ACCENT};
    font-weight: 600;
    z-index: 1;
}}
QWidget#SuiteBlockHead {{
    background: transparent;
    border: none;
}}
QWidget#SuiteBlockBody {{
    background: transparent;
    border: none;
}}
QLabel#SuiteSectionTitle {{
    color: {TEXT};
    font-size: {FS_BODY}px;
    font-weight: 600;
    letter-spacing: 0;
}}
QLabel#SuiteHint {{
    color: {TEXT_MUTED};
    font-size: {FS_BODY}px;
}}
QLabel#SuiteWarn {{
    background: {WARN_BG};
    color: {WARN_FG};
    border: none;
    border-radius: {RADIUS};
    padding: 8px 12px;
    font-size: {FS_BODY}px;
}}
QLabel#BusStripLabel {{
    color: {TEXT_DIM};
    font-size: {FS_BODY}px;
    font-weight: 600;
}}
QLabel#PageTitle {{
    color: {TEXT};
    font-size: {FS_BODY}px;
    font-weight: 600;
}}

/* ===== Buttons ===== */
QPushButton {{
    background: {EDITOR};
    color: {TEXT};
    border: 1px solid #CECECE;
    border-radius: 3px;
    padding: 0 12px;
    min-height: 26px;
    max-height: 26px;
    font-size: {FS_BODY}px;
}}
QPushButton:hover {{
    background: {SIDEBAR_HOVER};
    border-color: #B8B8B8;
}}
QPushButton:pressed {{ background: #E8E8E8; }}
QPushButton:disabled {{
    color: #A0A0A0;
    background: {EDITOR};
}}
QPushButton#PrimaryButton {{
    background: {ACCENT};
    color: #FFFFFF;
    border: 1px solid {ACCENT};
    font-weight: 600;
    border-radius: 3px;
}}
QPushButton#PrimaryButton:hover {{
    background: {ACCENT_HOVER};
    border-color: {ACCENT_HOVER};
}}
QPushButton#SecondaryButton {{
    background: {EDITOR};
    color: {TEXT};
    border: 1px solid #CECECE;
    font-weight: 500;
}}
QPushButton#SecondaryButton:hover {{
    background: {SIDEBAR_HOVER};
    border-color: #B8B8B8;
}}
QPushButton#GhostButton {{
    background: transparent;
    color: {TEXT_DIM};
    border: 1px solid transparent;
    font-weight: 500;
}}
QPushButton#GhostButton:hover {{
    background: {SIDEBAR_HOVER};
    color: {TEXT};
}}

/* ===== Checkboxes ===== */
QCheckBox {{
    color: {TEXT};
    font-size: {FS_BODY}px;
    spacing: 8px;
}}
QCheckBox::indicator {{
    width: 14px;
    height: 14px;
    border: 1px solid #CECECE;
    border-radius: 2px;
    background: {EDITOR};
}}
QCheckBox::indicator:hover {{
    border-color: {ACCENT};
}}
QCheckBox::indicator:checked {{
    background: {ACCENT};
    border-color: {ACCENT};
    image: url("{check_on}");
}}

/* ===== Editor tabs (VS Code main window tab strip) ===== */
QWidget#SuiteEditorChrome {{
    background: {SIDEBAR};
    border-bottom: 1px solid {BORDER};
}}
QLabel#SuiteEditorTitle {{
    color: {TEXT};
    font-size: {FS_BODY}px;
    font-weight: 600;
    padding: 0 10px;
}}
QWidget#SuiteEditorTabHost {{
    background: {SIDEBAR};
}}
QTabBar#SuiteEditorTabs {{
    background: {SIDEBAR};
    border: none;
    min-height: 36px;
}}
QTabBar#SuiteEditorTabs::tab {{
    background: {SIDEBAR};
    color: {TEXT_DIM};
    border: none;
    border-right: 1px solid {BORDER};
    border-radius: 0;
    padding: 8px 4px 8px 12px;
    margin: 0;
    min-height: 36px;
    max-height: 36px;
    font-size: {FS_BODY}px;
}}
QTabBar#SuiteEditorTabs::tab:selected {{
    background: {EDITOR};
    color: {TEXT};
    font-weight: 600;
    border-top: 2px solid {ACCENT};
    padding-top: 6px;
    padding-bottom: 8px;
}}
QTabBar#SuiteEditorTabs::tab:hover:!selected {{
    background: {SIDEBAR_HOVER};
    color: {TEXT};
}}
QToolButton#SuiteTabClose {{
    border: none;
    background: transparent;
    padding: 0;
    margin: 0 6px 0 2px;
    min-width: 18px;
    max-width: 18px;
    min-height: 18px;
    max-height: 18px;
    border-radius: 4px;
}}
QToolButton#SuiteTabClose:hover {{
    background: {SIDEBAR_HOVER};
}}
QToolButton#SuiteTabClose:pressed {{
    background: {BORDER};
}}
QWidget#SuiteLogHeader {{
    background: {EDITOR};
    border: none;
}}
QWidget#SuiteLogHeader QLabel#SuiteToolbarTitle {{
    font-weight: 600;
    letter-spacing: 0.2px;
}}
QWidget#SuiteEditorStack {{
    background: {EDITOR};
}}

/* Legacy underline tabs (kept for older pages) ===== */
QTabWidget::pane {{
    border: none;
    border-top: 1px solid {BORDER};
    background: {EDITOR};
    top: -1px;
    padding: 8px 0 0 0;
}}
QTabBar#SuiteTopTabs {{
    background: transparent;
    margin: 0;
    padding-top: 2px;
    min-height: 32px;
}}
QTabBar#SuiteTopTabs::tab {{
    background: transparent;
    color: {TEXT_DIM};
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    /* Extra bottom pad so underline never kisses glyph baselines. */
    padding: 8px 14px 10px 14px;
    margin-right: 0;
    min-height: 28px;
}}
QTabBar#SuiteTopTabs::tab:selected {{
    color: {TEXT};
    background: transparent;
    border-bottom: 2px solid {ACCENT};
    font-weight: 600;
}}
QTabBar#SuiteTopTabs::tab:hover:!selected {{
    background: transparent;
    color: {TEXT};
}}

/* ===== Lists / trees / tables — soft edge or none ===== */
QTreeWidget, QListWidget, QTableWidget, QTextEdit, QPlainTextEdit {{
    background: {EDITOR};
    border: none;
    border-radius: 0;
    outline: none;
    color: {TEXT};
    selection-background-color: {ACCENT_SOFT};
    selection-color: {TEXT};
}}
QTreeWidget, QListWidget {{
    border: 1px solid {BORDER_SOFT};
    border-radius: 3px;
}}
QTableWidget {{
    gridline-color: transparent;
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
    font-size: {FS_BODY}px;
    alternate-background-color: {EDITOR};
}}
QTableWidget#OutputTable {{
    border: none;
    alternate-background-color: {EDITOR};
}}
QHeaderView::section {{
    background: {EDITOR};
    color: {TEXT_MUTED};
    border: none;
    border-bottom: 1px solid {BORDER};
    border-right: none;
    padding: 6px 8px;
    font-size: {FS_META}px;
    font-weight: 700;
    letter-spacing: 0.3px;
}}
QTreeWidget::item, QListWidget::item {{
    padding: 5px 8px;
    min-height: 26px;
}}
QTreeWidget::item:selected, QListWidget::item:selected {{
    background: {ACCENT_SOFT};
    color: {TEXT};
}}

/* ===== Inputs — outer height CTRL_H=28 via setFixedHeight;
   QSS min-height is content-box (pad-y 0 + border 1+1 → use 26). ===== */
QLineEdit, QSpinBox, QComboBox {{
    background: {EDITOR};
    border: 1px solid #C8C8C8;
    border-radius: 3px;
    padding: 0px 8px;
    min-height: 26px;
    max-height: 26px;
    color: {TEXT};
    font-size: {FS_BODY}px;
    selection-background-color: {ACCENT_SOFT};
}}
QLineEdit:hover, QSpinBox:hover, QComboBox:hover {{
    border-color: #A8A8A8;
}}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {{
    border: 1px solid {ACCENT};
}}
QComboBox::drop-down {{ border: none; width: 18px; }}

/* ===== GroupBox — title only, no box ===== */
QGroupBox {{
    background: transparent;
    border: none;
    margin-top: 12px;
    padding: 8px 0 4px 0;
    font-size: {FS_BODY}px;
    font-weight: 600;
    color: {TEXT};
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 0;
    padding: 0;
    color: {TEXT};
    font-size: {FS_BODY}px;
    font-weight: 600;
    letter-spacing: 0;
}}

/* ===== Splitter / scroll ===== */
QSplitter::handle {{
    background: {BORDER};
}}
QSplitter::handle:horizontal {{ width: 1px; }}
QSplitter::handle:vertical {{ height: 1px; }}
QScrollBar:vertical {{
    background: transparent;
    width: 10px;
    margin: 0;
}}
QScrollBar::handle:vertical {{
    background: #C8C8C8;
    border-radius: 4px;
    min-height: 28px;
}}
QScrollBar::handle:vertical:hover {{ background: #B0B0B0; }}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}
QScrollBar:horizontal {{
    background: transparent;
    height: 10px;
}}
QScrollBar::handle:horizontal {{
    background: #C8C8C8;
    border-radius: 4px;
    min-width: 28px;
}}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {{ width: 0; }}

QWidget#SuiteWorkbench {{
    background: transparent;
    border: none;
}}
QWidget#SuiteLogPanel {{
    background: transparent;
    border: none;
}}
QWidget#SuiteLogHost {{
    background: {EDITOR};
    border-top: 1px solid {BORDER_SOFT};
}}
QPlainTextEdit#SuiteOutputTerminal {{
    background: {EDITOR};
    color: {TEXT};
    border: none;
    padding: 2px 6px;
    selection-background-color: {ACCENT_SOFT};
}}
QHeaderView::section {{
    background: {EDITOR};
    color: {TEXT_MUTED};
    border: none;
    border-bottom: 1px solid {BORDER_SOFT};
    border-right: none;
    padding: 4px 8px;
    font-size: {FS_META}px;
    font-weight: 600;
    letter-spacing: 0.2px;
}}
QProgressBar {{
    border: none;
    border-radius: 2px;
    background: {EDITOR};
    text-align: center;
    height: 4px;
    max-height: 4px;
    color: transparent;
}}
QProgressBar::chunk {{
    background: {ACCENT};
    border-radius: 2px;
}}

/* ===== Suite menubar LAST — QLabel labels (never QPushButton) ===== */
QLabel#SuiteMenuLabel {{
    background: transparent;
    color: {TEXT};
    border: none;
    padding: 0 12px;
    margin: 0;
    min-height: 35px;
    max-height: 35px;
    font-size: {FS_MENU}px;
}}
QLabel#SuiteMenuLabel:hover {{
    background: {SIDEBAR_HOVER};
}}
QPushButton#SuiteMenuButton {{
    background: transparent;
    color: {TEXT};
    border: none;
    border-radius: 0;
    padding: 0 10px;
    margin: 0;
    min-height: 35px;
    max-height: 35px;
    min-width: 44px;
    max-width: 220px;
    font-size: {FS_MENU}px;
    text-align: center;
}}
QPushButton#SuiteMenuButton:hover {{
    background: {SIDEBAR_HOVER};
    border: none;
}}
QPushButton#SuiteMenuButton:pressed {{
    background: {SIDEBAR_HOVER};
    border: none;
}}
QPushButton#SuiteMenuButton:disabled {{
    color: {TEXT_MUTED};
    background: transparent;
    border: none;
}}
"""


def apply(widget: QWidget) -> None:
    widget.setStyleSheet(stylesheet())
