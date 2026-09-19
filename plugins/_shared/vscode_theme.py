# -*- coding: utf-8 -*-
"""VS Code–inspired theme for domain suite windows.

Goals: calm contrast, hairline dividers, soft radius, shared chrome look.
Light editor + dark activity bar (classic VS Code desktop).
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

# Soft / eye-friendly palette — one light theme (VS Code Light).
# Sidebar is a slightly darker panel, NOT a black activity bar.
BG = "#F3F3F3"
EDITOR = "#FFFFFF"
SIDEBAR = "#F3F3F3"
SIDEBAR_HOVER = "#E8E8E8"
SIDEBAR_ACTIVE = "#FFFFFF"
BORDER = "#E5E5E5"
BORDER_SOFT = "#EEEEEE"
BLOCK_BORDER = "#E5E5E5"  # kept for compat; sections are borderless now

TEXT = "#333333"
TEXT_DIM = "#616161"
TEXT_MUTED = "#8A8A8A"
TEXT_SIDE = "#333333"
ACCENT = "#007ACC"
ACCENT_SOFT = "#E8F4FC"
ACCENT_HOVER = "#0062A3"
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


def tune_form(form) -> None:
    """Right-aligned labels, even rows, fields grow together."""
    form.setLabelAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
    form.setFormAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
    form.setHorizontalSpacing(12)
    form.setVerticalSpacing(8)
    form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)


def stylesheet() -> str:
    return f"""
/* ===== Shell — VS Code Light type scale: 13 body / 12 secondary / 11 meta ===== */
QMainWindow, QDialog {{
    background: {EDITOR};
    color: {TEXT};
    font-size: 13px;
    font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
}}
QStatusBar {{
    background: {EDITOR};
    color: {TEXT_MUTED};
    border-top: 1px solid {BORDER};
    font-size: 12px;
    min-height: 22px;
}}
QStatusBar::item {{ border: none; }}

/* ===== Activity nav ===== */
QListWidget#SuiteNav {{
    background: {SIDEBAR};
    color: {TEXT};
    border: none;
    border-right: 1px solid {BORDER};
    outline: none;
    padding: 6px 0;
    font-size: 13px;
}}
QListWidget#SuiteNav::item {{
    padding: 7px 10px 7px 10px;
    margin: 2px 6px;
    border: 1px solid transparent;
    border-radius: 3px;
}}
QListWidget#SuiteNav::item:hover {{
    background: {SIDEBAR_HOVER};
    border: 1px solid {BORDER};
}}
QListWidget#SuiteNav::item:selected {{
    background: {EDITOR};
    color: {TEXT};
    font-weight: 600;
    border: 1px solid {BORDER};
    border-left: 2px solid {ACCENT};
}}

/* ===== Shared connection toolbar ===== */
QWidget#SuiteToolbar {{
    background: {EDITOR};
    border-bottom: 1px solid {BORDER};
}}
QWidget#SuiteToolbar QLabel {{
    color: {TEXT_DIM};
    font-size: 12px;
}}
QWidget#SuiteToolbar QLabel#SuiteToolbarTitle {{
    color: {TEXT_MUTED};
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.6px;
    padding-right: 4px;
}}
QLabel#SessionBadge {{
    color: {ACCENT};
    font-weight: 600;
    font-size: 12px;
    padding: 2px 8px;
    background: {ACCENT_SOFT};
    border: none;
    border-radius: 8px;
}}
QWidget#SuiteToolbar QSpinBox {{
    background: {EDITOR};
    border: 1px solid {BORDER};
    border-radius: 4px;
    padding: 2px 6px;
    min-height: {CTRL_H};
    max-height: {CTRL_H};
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
    font-size: 12px;
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
    background: {BORDER};
    max-height: 1px;
}}

/* ===== Content host — flat surface, borders only on controls / pane splits ===== */
QWidget#SuiteContent {{
    background: {EDITOR};
}}
QWidget#SuiteLogHost {{
    background: {SIDEBAR};
    border-top: 1px solid {BORDER};
}}
QWidget#SuiteSection, QWidget#BusStrip, QWidget#SuiteBlock {{
    background: transparent;
    border: none;
}}
QWidget#ConnStatus {{
    background: transparent;
    border: none;
    border-bottom: 1px solid {BORDER};
}}
QWidget#StepSpin QSpinBox {{
    border-top-right-radius: 0;
    border-bottom-right-radius: 0;
}}
QPushButton#StepSpinBtn {{
    background: {EDITOR};
    border: 1px solid #CECECE;
    border-radius: 2px;
    padding: 0;
    min-height: 28px;
    max-height: 28px;
    min-width: 28px;
    max-width: 28px;
}}
QPushButton#StepSpinBtn:hover {{
    background: #E8E8E8;
    border-color: {ACCENT};
}}
QPushButton#StepSpinBtn:pressed {{
    background: #DCDCDC;
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
    font-size: 13px;
    font-weight: 600;
    letter-spacing: 0;
}}
QLabel#SuiteHint {{
    color: {TEXT_MUTED};
    font-size: 12px;
}}
QLabel#SuiteWarn {{
    background: {WARN_BG};
    color: {WARN_FG};
    border: none;
    border-radius: {RADIUS};
    padding: 8px 12px;
    font-size: 12px;
}}
QLabel#BusStripLabel {{
    color: {TEXT_DIM};
    font-size: 12px;
    font-weight: 600;
}}
QLabel#PageTitle {{
    color: {TEXT};
    font-size: 13px;
    font-weight: 700;
}}

/* ===== Buttons ===== */
QPushButton {{
    background: {EDITOR};
    color: {TEXT};
    border: 1px solid #CECECE;
    border-radius: 2px;
    padding: 0 10px;
    min-height: 28px;
    max-height: 28px;
    font-size: 13px;
}}
QPushButton:hover {{
    background: #E8E8E8;
}}
QPushButton:pressed {{ background: #DCDCDC; }}
QPushButton:disabled {{
    color: #A0A0A0;
    background: {BG};
}}
QPushButton#PrimaryButton {{
    background: {ACCENT};
    color: #FFFFFF;
    border: 1px solid {ACCENT};
    font-weight: 600;
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
    background: #E8E8E8;
}}
QPushButton#GhostButton {{
    background: {EDITOR};
    color: {TEXT};
    border: 1px solid #CECECE;
    font-weight: 500;
}}
QPushButton#GhostButton:hover {{
    background: #E8E8E8;
    border-color: #B0B0B0;
    color: {TEXT};
}}

/* ===== Tabs — underline style (VS Code), no tab boxes ===== */
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
}}
QTabBar::tab {{
    background: transparent;
    color: {TEXT_DIM};
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    padding: 7px 14px;
    margin-right: 0;
    min-height: 22px;
}}
QTabBar::tab:selected {{
    color: {TEXT};
    background: transparent;
    border-bottom: 2px solid {ACCENT};
    font-weight: 600;
}}
QTabBar::tab:hover:!selected {{
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
    selection-background-color: #CDE8F6;
    selection-color: {TEXT};
}}
QTreeWidget, QListWidget {{
    border: 1px solid {BORDER_SOFT};
    border-radius: 3px;
}}
QTableWidget {{
    gridline-color: transparent;
    font-family: Consolas, "Cascadia Mono", "Courier New", monospace;
    font-size: 12px;
    alternate-background-color: #FAFAFA;
}}
QTableWidget#OutputTable {{
    border: none;
    alternate-background-color: {EDITOR};
}}
QHeaderView::section {{
    background: transparent;
    color: {TEXT_MUTED};
    border: none;
    border-bottom: 1px solid {BORDER};
    border-right: none;
    padding: 6px 8px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.3px;
}}
QTreeWidget::item, QListWidget::item {{
    padding: 4px 6px;
    min-height: 22px;
}}
QTreeWidget::item:selected, QListWidget::item:selected {{
    background: #CDE8F6;
    color: {TEXT};
}}

/* ===== Inputs ===== */
QLineEdit, QSpinBox, QComboBox {{
    background: {EDITOR};
    border: 1px solid {BORDER};
    border-radius: 4px;
    padding: 3px 8px;
    min-height: {CTRL_H};
    color: {TEXT};
    selection-background-color: #CDE8F6;
}}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {{
    border: 1px solid {ACCENT};
}}
QComboBox::drop-down {{ border: none; width: 18px; }}
QCheckBox {{
    spacing: 6px;
    color: {TEXT};
}}
QCheckBox::indicator {{
    width: 14px; height: 14px;
    border: 1px solid #9A9A9A;
    border-radius: 2px;
    background: {EDITOR};
}}
QCheckBox::indicator:hover {{
    border-color: {ACCENT};
}}
QCheckBox::indicator:checked {{
    background: {ACCENT};
    border-color: {ACCENT};
}}

/* ===== GroupBox — title only, no box ===== */
QGroupBox {{
    background: transparent;
    border: none;
    margin-top: 12px;
    padding: 8px 0 4px 0;
    font-weight: 600;
    color: {TEXT};
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 0;
    padding: 0;
    color: {TEXT};
    font-size: 13px;
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
QWidget#SuiteLogHeader {{
    background: transparent;
    border: none;
    border-top: 1px solid {BORDER};
}}
QProgressBar {{
    border: none;
    border-radius: 2px;
    background: {BG};
    text-align: center;
    height: 4px;
    max-height: 4px;
    color: transparent;
}}
QProgressBar::chunk {{
    background: {ACCENT};
    border-radius: 2px;
}}
"""


def apply(widget: QWidget) -> None:
    widget.setStyleSheet(stylesheet())
