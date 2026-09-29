# -*- coding: utf-8 -*-
"""Shared EDS Studio UI helpers — quiet chrome, consistent controls."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QHeaderView,
    QLabel,
    QLineEdit,
    QListWidget,
    QPushButton,
    QTreeWidget,
)

from _shared import codicons

TREE_STYLE = (
    "QTreeWidget#SuiteMatrix, QListWidget#SuiteSideList {"
    " border: none; background: transparent; outline: 0; }"
    "QTreeWidget#SuiteMatrix::item:selected,"
    "QListWidget#SuiteSideList::item:selected {"
    " background: #E3F2FD; color: #0D47A1; }"
    "QTreeWidget#SuiteMatrix::item:hover,"
    "QListWidget#SuiteSideList::item:hover {"
    " background: #F5F5F5; }"
)

FORM_MAX_W = 360


def ghost_btn(text: str, tip: str, icon: str = "") -> QPushButton:
    btn = QPushButton(text)
    btn.setObjectName("GhostButton")
    btn.setFixedHeight(26)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    if icon:
        codicons.set_button(btn, icon, size=12)
    return btn


def primary_btn(text: str, tip: str, icon: str = "apply") -> QPushButton:
    btn = QPushButton(text)
    btn.setFixedHeight(26)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    codicons.set_button(btn, icon, size=12, primary=True)
    return btn


def line_edit(tip: str, placeholder: str = "") -> QLineEdit:
    edit = QLineEdit()
    edit.setFixedHeight(28)
    edit.setToolTip(tip)
    if placeholder:
        edit.setPlaceholderText(placeholder)
    return edit


def combo(items, tip: str) -> QComboBox:
    box = QComboBox()
    box.addItems(list(items))
    box.setFixedHeight(28)
    box.setToolTip(tip)
    return box


def spin_hex(lo, hi, value, tip):
    from _shared.widgets import StepSpin
    box = StepSpin(
        value, minimum=lo, maximum=hi, hex_mode=True, width=140)
    box.setToolTip(tip)
    return box


def spin(lo, hi, value, tip="", width=100):
    from _shared.widgets import StepSpin
    box = StepSpin(value, minimum=lo, maximum=hi, width=width)
    if tip:
        box.setToolTip(tip)
    return box


def style_tree(tree: QTreeWidget, stretch_col: int = 1) -> None:
    tree.setObjectName("SuiteMatrix")
    tree.setAlternatingRowColors(True)
    tree.setUniformRowHeights(True)
    tree.setRootIsDecorated(True)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.setStyleSheet(TREE_STYLE)
    hdr = tree.header()
    hdr.setHighlightSections(False)
    hdr.setDefaultAlignment(Qt.AlignmentFlag.AlignLeft)
    for i in range(tree.columnCount()):
        mode = (QHeaderView.ResizeMode.Stretch if i == stretch_col
                else QHeaderView.ResizeMode.ResizeToContents)
        hdr.setSectionResizeMode(i, mode)


def style_list(lst: QListWidget) -> None:
    lst.setObjectName("SuiteSideList")
    lst.setSpacing(1)
    lst.setStyleSheet(TREE_STYLE +
                      "QListWidget#SuiteSideList::item { padding: 6px 8px; }")
    lst.setFocusPolicy(Qt.FocusPolicy.StrongFocus)


def quiet_label(text: str = "") -> QLabel:
    lab = QLabel(text)
    lab.setStyleSheet("color:#90A4AE;font-size:11px;")
    lab.setWordWrap(True)
    return lab


def empty_state(text: str) -> QLabel:
    lab = QLabel(text)
    lab.setAlignment(Qt.AlignmentFlag.AlignCenter)
    lab.setStyleSheet(
        "color:#B0BEC5;font-size:13px;padding:32px 16px;")
    lab.setWordWrap(True)
    return lab


def count_label() -> QLabel:
    lab = QLabel("")
    lab.setStyleSheet("color:#78909C;font-size:11px;padding:0 6px;")
    return lab
