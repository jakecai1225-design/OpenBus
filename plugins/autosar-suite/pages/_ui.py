# -*- coding: utf-8 -*-
"""Shared quiet UI helpers for AUTOSAR Studio."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QLabel, QPushButton, QTreeWidget

from _shared import codicons

TREE_STYLE = (
    "QTreeWidget#SuiteMatrix { border: none; outline: 0; }"
    "QTreeWidget#SuiteMatrix::item:selected {"
    " background: #E3F2FD; color: #0D47A1; }"
)


def ghost_btn(text, tip, icon=""):
    btn = QPushButton(text)
    btn.setObjectName("GhostButton")
    btn.setFixedHeight(26)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    if icon:
        codicons.set_button(btn, icon, size=12)
    return btn


def primary_btn(text, tip, icon="apply"):
    btn = QPushButton(text)
    btn.setFixedHeight(26)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    codicons.set_button(btn, icon, size=12, primary=True)
    return btn


def quiet_label(text=""):
    lab = QLabel(text)
    lab.setStyleSheet("color:#90A4AE;font-size:11px;")
    lab.setWordWrap(True)
    return lab


def tip_panel():
    lab = QLabel("")
    lab.setWordWrap(True)
    lab.setStyleSheet(
        "color:#546E7A;font-size:12px;padding:8px;"
        "background:#FAFAFA;border-left:3px solid #90CAF9;")
    lab.setMinimumHeight(72)
    return lab


def style_tree(tree: QTreeWidget):
    tree.setObjectName("SuiteMatrix")
    tree.setAlternatingRowColors(True)
    tree.setUniformRowHeights(True)
    tree.setStyleSheet(TREE_STYLE)
