# -*- coding: utf-8 -*-
"""Side Bar — A2L / Live / Measure / Calibrate."""

from __future__ import annotations

from typing import Callable, Optional, Sequence, Tuple

from PyQt6.QtCore import QSize, Qt
from PyQt6.QtWidgets import QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget

from _shared import codicons, vscode_theme as T
from pages import _ui

SectionSpec = Tuple[str, str, str, str]

A2L_SECTIONS = (
    ("a2l_browser", "Symbols", "Browse applied A2L measurements / characteristics", "database"),
)
LIVE_SECTIONS = (
    ("setup", "Setup", "XCP on CAN IDs + Connect", "gear"),
)
MEASURE_SECTIONS = (
    ("measure", "Measure", "Polling / DAQ values + sparkline", "graph"),
    ("record", "Record", "CSV / MDF subset export", "save"),
)
CALIBRATE_SECTIONS = (
    ("calibrate", "Calibrate", "Scalar / MAP write + page switch", "edit"),
)

_LEAF_ICON = 16


def build_section_sidebar(
        shell, title: str, sections: Sequence[SectionSpec],
        *, on_select: Optional[Callable[[str], None]] = None) -> QWidget:
    root = QWidget()
    root.setObjectName("SuiteSideBar")
    root.setMinimumWidth(180)
    root.setMaximumWidth(280)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)
    lay.addWidget(_ui.sidebar_header(title))
    tree = QTreeWidget()
    _ui.style_tree(tree)
    tree.setRootIsDecorated(False)
    tree.setIndentation(0)
    tree.setHeaderHidden(True)
    tree.setIconSize(QSize(_LEAF_ICON, _LEAF_ICON))
    lay.addWidget(tree, 1)
    _keys = [s[0] for s in sections]
    _guard = {"depth": 0}

    def rebuild():
        tree.clear()
        for key, label, tip, icon_name in sections:
            item = QTreeWidgetItem([label])
            item.setData(0, Qt.ItemDataRole.UserRole, key)
            item.setToolTip(0, tip)
            item.setIcon(0, codicons.icon(icon_name, T.TEXT, _LEAF_ICON))
            tree.addTopLevelItem(item)

    def select_section(key: str):
        if key not in _keys:
            return
        _guard["depth"] += 1
        try:
            for i in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(i)
                if item and item.data(0, Qt.ItemDataRole.UserRole) == key:
                    tree.setCurrentItem(item)
                    break
        finally:
            _guard["depth"] -= 1

    def _on_click():
        if _guard["depth"]:
            return
        item = tree.currentItem()
        if not item:
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        if on_select:
            on_select(str(key))
        elif hasattr(shell, "goto_page"):
            shell.goto_page(str(key))

    tree.itemClicked.connect(lambda *_: _on_click())
    rebuild()
    root.select_section = select_section  # type: ignore[attr-defined]
    root.feature_keys = _keys  # type: ignore[attr-defined]
    return root


def build_a2l_sidebar(shell):
    return build_section_sidebar(shell, "A2L", A2L_SECTIONS)


def build_live_sidebar(shell):
    return build_section_sidebar(shell, "Live", LIVE_SECTIONS)


def build_measure_sidebar(shell):
    return build_section_sidebar(shell, "Measure", MEASURE_SECTIONS)


def build_calibrate_sidebar(shell):
    return build_section_sidebar(shell, "Calibrate", CALIBRATE_SECTIONS)
