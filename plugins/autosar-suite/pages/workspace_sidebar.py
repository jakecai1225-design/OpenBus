# -*- coding: utf-8 -*-
"""Workspace Side Bars — VS Code Explorer-style section lists.

Each activity workspace mounts one of these into the collapsible Side Bar.
Config keeps a richer BSW catalog in config_sidebar.py.
"""

from __future__ import annotations

from typing import Callable, Optional, Sequence, Tuple

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pages import _ui

# section: (feature_key, label, tip)
# nested children optional: list of (child_key, label, tip)
SectionSpec = Tuple[str, str, str]
NestedSpec = Tuple[str, str, str, Sequence[SectionSpec]]


def _header(title: str) -> QWidget:
    head = QWidget()
    head.setObjectName("SuiteSideBarHeader")
    head.setFixedHeight(28)
    hl = QHBoxLayout(head)
    hl.setContentsMargins(10, 0, 6, 0)
    lab = QLabel(title.upper())
    lab.setObjectName("SuiteToolbarTitle")
    hl.addWidget(lab, 1)
    return head


def build_section_sidebar(
        shell,
        title: str,
        sections: Sequence[SectionSpec],
        *,
        nested: Optional[dict] = None,
        on_select: Optional[Callable[[str], None]] = None,
) -> QWidget:
    """Flat or one-level-nested section tree.

    *nested*: map parent_feature_key → list of (child_key, label, tip).
    Clicking a leaf calls shell.goto_page(feature_key) or *on_select*.
    """
    nested = nested or {}
    root = QWidget()
    root.setObjectName("WorkspaceSideBar")
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)
    lay.addWidget(_header(title))

    tree = QTreeWidget()
    tree.setObjectName("SuiteMatrix")
    tree.setHeaderHidden(True)
    tree.setIndentation(14)
    tree.setAnimated(True)
    tree.setExpandsOnDoubleClick(False)
    tree.setUniformRowHeights(True)
    _ui.style_tree(tree)
    lay.addWidget(tree, 1)

    _guard = {"depth": 0}
    _keys = [s[0] for s in sections]

    def rebuild():
        _guard["depth"] += 1
        try:
            tree.blockSignals(True)
            tree.clear()
            for key, label, tip in sections:
                it = QTreeWidgetItem([label])
                it.setData(0, Qt.ItemDataRole.UserRole, ("section", key))
                it.setToolTip(0, tip)
                it.setFlags(
                    Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
                children = nested.get(key) or ()
                if children:
                    for ck, clabel, ctip in children:
                        ch = QTreeWidgetItem([clabel])
                        ch.setData(
                            0, Qt.ItemDataRole.UserRole, ("child", ck, key))
                        ch.setToolTip(0, ctip)
                        ch.setFlags(
                            Qt.ItemFlag.ItemIsEnabled
                            | Qt.ItemFlag.ItemIsSelectable)
                        it.addChild(ch)
                    it.setExpanded(True)
                else:
                    it.setChildIndicatorPolicy(
                        QTreeWidgetItem.ChildIndicatorPolicy.DontShowIndicator)
                tree.addTopLevelItem(it)
            tree.blockSignals(False)
        finally:
            _guard["depth"] -= 1

    def select_section(feature: str, *, expand: bool = True):
        """Highlight leaf or parent matching *feature* (or nested child)."""
        for i in range(tree.topLevelItemCount()):
            it = tree.topLevelItem(i)
            data = it.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] == ("section", feature):
                tree.blockSignals(True)
                tree.setCurrentItem(it)
                tree.blockSignals(False)
                if expand:
                    it.setExpanded(True)
                return
            for j in range(it.childCount()):
                ch = it.child(j)
                cd = ch.data(0, Qt.ItemDataRole.UserRole) or ()
                if len(cd) >= 2 and cd[1] == feature:
                    tree.blockSignals(True)
                    it.setExpanded(True)
                    tree.setCurrentItem(ch)
                    tree.blockSignals(False)
                    return

    def _activate(feature: str):
        if callable(on_select):
            on_select(feature)
        else:
            shell.goto_page(feature)

    def _on_click():
        if _guard["depth"]:
            return
        item = tree.currentItem()
        if not item:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        kind = data[0]
        if kind == "section":
            key = data[1]
            # Parent with children: still open the parent feature (default child).
            _activate(key)
            if item.childCount():
                item.setExpanded(True)
        elif kind == "child":
            _activate(data[1])

    tree.itemClicked.connect(lambda *_: _on_click())
    rebuild()

    root.rebuild = rebuild
    root.select_section = select_section
    root.feature_keys = _keys
    root.tree = tree
    root._badge_counts = {}

    def set_badges(counts: dict):
        """Update section labels with optional error badges."""
        root._badge_counts = dict(counts or {})
        for i in range(tree.topLevelItemCount()):
            it = tree.topLevelItem(i)
            data = it.data(0, Qt.ItemDataRole.UserRole) or ()
            if len(data) < 2:
                continue
            key = data[1]
            # Restore base label from sections table
            base = key
            for sk, slabel, _tip in sections:
                if sk == key:
                    base = slabel
                    break
            n = int(root._badge_counts.get(key) or 0)
            it.setText(0, "%s (%d)" % (base, n) if n else base)

    root.set_badges = set_badges
    root.setToolTip("%s explorer · Ctrl+B toggles Side Bar" % title)
    return root


# ---- Concrete workspace section tables ----

PROJECT_SECTIONS = (
    ("project", "Workspace", "Project files, derive ECUC, write intermediates"),
    ("library", "Library", "Starters, templates, recent files"),
)

COM_SECTIONS = (
    ("com_layout", "Layout", "I-PDU / signal layout"),
    ("com_live", "Live", "Live decode and observe"),
    ("com_pack", "Pack", "Pack and send COM frames"),
)

BUS_SECTIONS = (
    ("system", "System", "Live COM extract tree, validate, export"),
    ("nm", "NM", "Network management"),
    ("e2e", "E2E", "End-to-end protection"),
    ("secoc", "SecOC", "Secured onboard communication"),
)

BUS_NESTED = {
    "system": (
        ("system_tree", "Tree", "ARXML / COM extract tree"),
        ("system_validate", "Validate", "System consistency checks"),
        ("system_export", "Export", "Export ARXML / DBC"),
    ),
}

VALIDATE_SECTIONS = (
    ("validate", "Findings", "Lint and consistency findings"),
    ("timing", "Analysis", "Coverage and load analysis"),
    ("compare", "Compare", "Diff two ARXML files"),
    ("merge", "Merge", "Combine ARXML files"),
    ("export", "Export", "Export handoff artifacts"),
)

SETUP_SECTIONS = (
    ("setup", "Setup", "Session, TX/RX, timing"),
)
