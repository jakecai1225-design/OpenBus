# -*- coding: utf-8 -*-
"""Shared CANopen Suite UI helpers — quiet chrome."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QHeaderView,
    QLabel,
    QPushButton,
    QTreeWidget,
)

from _shared import codicons

TREE_STYLE = (
    "QTreeWidget#SuiteMatrix {"
    " border: none; background: transparent; outline: 0; }"
    "QTreeWidget#SuiteMatrix::item:selected {"
    " background: #E3F2FD; color: #0D47A1; }"
    "QTreeWidget#SuiteMatrix::item:hover { background: #F5F5F5; }"
)


def ghost_btn(text: str, tip: str, icon: str = "") -> QPushButton:
    btn = QPushButton(text)
    btn.setObjectName("GhostButton")
    btn.setFixedHeight(26)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    if icon:
        codicons.set_button(btn, icon, size=12)
    return btn


def quiet_label(text: str) -> QLabel:
    lab = QLabel(text)
    lab.setObjectName("SuiteHint")
    lab.setStyleSheet("color:#546E7A;font-size:12px;")
    return lab


def style_tree(tree: QTreeWidget) -> None:
    tree.setObjectName("SuiteMatrix")
    tree.setStyleSheet(TREE_STYLE)
    tree.setRootIsDecorated(True)
    tree.setUniformRowHeights(True)
    tree.setSelectionBehavior(
        QAbstractItemView.SelectionBehavior.SelectRows)
    tree.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
    tree.setHeaderHidden(True)
    hdr = tree.header()
    if hdr is not None:
        hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
