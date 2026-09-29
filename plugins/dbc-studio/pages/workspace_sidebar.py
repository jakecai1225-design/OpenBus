# -*- coding: utf-8 -*-
"""Side Bar — flat leaves per activity (DBC Studio).

Activities: Edit / Analyze / Integrate / Deliver.
DBC files live under the File menu.
"""

from __future__ import annotations

from typing import Callable, Optional, Sequence, Tuple

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pages import _ui

SectionSpec = Tuple[str, str, str]

EDIT_SECTIONS = (
    ("editor", "Messages", "Edit network / nodes / messages / signals"),
    ("valuetables", "Value tables", "Named value encodings for signals"),
    ("attributes", "Attributes", "Attribute definitions and values"),
)
ANALYZE_SECTIONS = (
    ("matrix", "Matrix", "Signal × message overview"),
    ("timing", "Timing", "Bus load estimate from cycle times"),
    ("validate", "Validate", "Lint the current DBC"),
)
INTEGRATE_SECTIONS = (
    ("compare", "Compare", "Diff two DBC files"),
    ("merge", "Merge", "Merge multiple DBC files"),
)
DELIVER_SECTIONS = (
    ("export", "Export", "Export matrix / codegen"),
    ("library", "Library", "Recent and pinned DBC files"),
)


def build_section_sidebar(
        shell,
        title: str,
        sections: Sequence[SectionSpec],
        *,
        on_select: Optional[Callable[[str], None]] = None,
) -> QWidget:
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
    tree.setExpandsOnDoubleClick(False)
    lay.addWidget(tree, 1)

    _keys = [s[0] for s in sections]
    _guard = {"depth": 0}

    def rebuild():
        tree.clear()
        for key, label, tip in sections:
            item = QTreeWidgetItem([label])
            item.setData(0, Qt.ItemDataRole.UserRole, key)
            item.setToolTip(0, tip)
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

    def _on_current(_cur, _prev):
        if _guard["depth"]:
            return
        item = tree.currentItem()
        if item is None:
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        if not key:
            return
        if on_select:
            on_select(str(key))
        elif hasattr(shell, "goto_page"):
            shell.goto_page(str(key))

    tree.currentItemChanged.connect(_on_current)
    rebuild()

    root.rebuild = rebuild  # type: ignore[attr-defined]
    root.select_section = select_section  # type: ignore[attr-defined]
    root.feature_keys = _keys  # type: ignore[attr-defined]
    root.tree = tree  # type: ignore[attr-defined]
    root.setToolTip("%s · Ctrl+B" % title)
    return root


def build_edit_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Edit", EDIT_SECTIONS)


def build_analyze_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Analyze", ANALYZE_SECTIONS)


def build_integrate_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Integrate", INTEGRATE_SECTIONS)


def build_deliver_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Deliver", DELIVER_SECTIONS)
