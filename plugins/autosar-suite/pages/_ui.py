# -*- coding: utf-8 -*-
"""Shared quiet UI helpers for AUTOSAR Studio (VS Code theme tokens)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QLabel, QPushButton, QTreeWidget

from _shared import codicons, vscode_theme

TREE_STYLE = (
    "QTreeWidget#SuiteMatrix { border: none; outline: 0; }"
    "QTreeWidget#SuiteMatrix::item:selected {"
    " background: %s; color: %s; }"
) % (vscode_theme.ACCENT_SOFT, vscode_theme.TEXT)


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
    lab.setObjectName("SuiteHint")
    lab.setStyleSheet(
        "color:%s;font-size:11px;" % vscode_theme.TEXT_MUTED)
    lab.setWordWrap(True)
    return lab


def tip_panel():
    lab = QLabel("")
    lab.setWordWrap(True)
    lab.setStyleSheet(
        "color:%s;font-size:12px;padding:6px 8px;"
        "background:transparent;border-top:1px solid %s;" % (
            vscode_theme.TEXT_MUTED, vscode_theme.BORDER_SOFT))
    lab.setMinimumHeight(48)
    return lab


def style_tree(tree: QTreeWidget):
    tree.setObjectName("SuiteMatrix")
    tree.setAlternatingRowColors(False)
    tree.setUniformRowHeights(True)
    tree.setStyleSheet(TREE_STYLE)
