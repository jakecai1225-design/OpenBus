# -*- coding: utf-8 -*-
"""EDS Studio UI — shared suite_ui density + EDS helpers."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QComboBox,
    QLabel,
    QLineEdit,
    QListWidget,
)

from _shared import vscode_theme as T
from _shared.suite_ui import *  # noqa: F401,F403
from _shared.suite_ui import apply_suite_chrome as apply_eds_chrome  # noqa: F401
from _shared.suite_ui import (
    CTRL_H,
    muted_label,
    style_tree as _suite_style_tree,
    configure_columns,
)

FORM_MAX_W = 360
ICON_FG = T.TEXT


def line_edit(tip: str, placeholder: str = "") -> QLineEdit:
    edit = QLineEdit()
    edit.setFixedHeight(CTRL_H)
    edit.setToolTip(tip)
    if placeholder:
        edit.setPlaceholderText(placeholder)
    return edit


def combo(items, tip: str) -> QComboBox:
    box = QComboBox()
    box.addItems(list(items))
    box.setFixedHeight(CTRL_H)
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


def count_label(text: str = "") -> QLabel:
    return muted_label(text)


def style_tree(tree, stretch_col: int = 1, *, header_hidden: bool = False) -> None:
    """EDS pages pass stretch_col; map onto shared helpers."""
    _suite_style_tree(tree, header_hidden=header_hidden)
    try:
        configure_columns(tree, stretch=stretch_col)
    except Exception:
        pass


def style_list(lst: QListWidget) -> None:
    lst.setObjectName("SuiteSideList")
    lst.setSpacing(1)
    lst.setStyleSheet(
        "QListWidget#SuiteSideList { border: none; background: transparent; outline: 0; }"
        "QListWidget#SuiteSideList::item { padding: 6px 8px; }"
        "QListWidget#SuiteSideList::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
        "QListWidget#SuiteSideList::item:hover { background: #F5F5F5; }")
    lst.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
