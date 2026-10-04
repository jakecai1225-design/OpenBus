# -*- coding: utf-8 -*-
"""XCP Studio UI — shared suite_ui density."""

from __future__ import annotations

from PyQt6.QtWidgets import QComboBox, QLabel, QLineEdit

from _shared.suite_ui import *  # noqa: F401,F403
from _shared.suite_ui import apply_suite_chrome as apply_xcp_chrome  # noqa: F401
from _shared.suite_ui import CTRL_H, muted_label

FORM_MAX_W = 360


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


def count_label(text: str = "") -> QLabel:
    return muted_label(text)
