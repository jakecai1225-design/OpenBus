# -*- coding: utf-8 -*-
"""Workspace Side Bars — VS Code Explorer-style section lists for CANopen."""

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

SectionSpec = Tuple[str, str, str]


def _header(title: str) -> QWidget:
    head = QWidget()
    head.setObjectName("SuiteSideBarHeader")
    head.setFixedHeight(28)
    hl = QHBoxLayout(head)
    hl.setContentsMargins(10, 0, 6, 0)
    lab = QLabel(title.upper())
    lab.setObjectName("SuiteToolbarTitle")
    lab.setStyleSheet(
        "font-size:11px;font-weight:600;letter-spacing:0.6px;color:#546E7A;")
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
    """Section tree: parents fold; leaves open editor tabs via shell.goto_page."""
    nested = nested or {}
    root = QWidget()
    root.setObjectName("WorkspaceSideBar")
    root.setMinimumWidth(180)
    root.setMaximumWidth(320)
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
    tree.setStyleSheet(
        tree.styleSheet()
        + "QTreeWidget#SuiteMatrix::item { padding: 2px 4px; }"
        + "QTreeWidget#SuiteMatrix::item:has-children {"
        + " font-weight: 600; color: #455A64; }")
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
    root.setToolTip("%s explorer · Ctrl+B toggles Side Bar" % title)
    return root


# Activity workspace → Side Bar sections (foldable groups + leaves)

NETWORK_SECTIONS = (
    ("network", "Network", "Scan and NMT"),
    ("monitor", "Monitor", "COB / frame monitor"),
)

NETWORK_NESTED = {
    "network": (
        ("network_scan", "Scan", "Probe nodes 1–127"),
        ("network_nmt", "NMT", "Start / Stop / Pre-op / Reset"),
    ),
}

DEVICE_SECTIONS = (
    ("od", "Object Dictionary", "Live OD tree and SDO"),
    ("pdo", "PDO", "PDO mapping"),
)

EDS_SECTIONS = (
    ("eds", "EDS", "CANeds-style editor"),
)

EDS_NESTED = {
    "eds": (
        ("eds_dict", "Dictionary", "Object tree and definitions"),
        ("eds_device", "Device", "FileInfo / DeviceInfo"),
        ("eds_check", "Check", "EDS validation findings"),
    ),
}

LIBRARY_SECTIONS = (
    ("library", "Profiles", "CiA profile stubs"),
)

LIBRARY_NESTED = {
    "library": (
        ("lib_301", "CiA 301", "Communication profile objects"),
        ("lib_402", "CiA 402", "Drive / motion profile objects"),
    ),
}

SETUP_SECTIONS = (
    ("setup", "Setup", "Node-ID, EDS path, session"),
)
