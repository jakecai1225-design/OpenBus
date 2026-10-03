# -*- coding: utf-8 -*-
"""Side Bar — flat, short labels (one click = one job).

Activities: EDS / Live / Trace / Code.
EDS files live under File menu. Device info under Dictionary / View.
"""

from __future__ import annotations

import os
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


def _doc_banner(shell) -> QWidget:
    """One row: file name + New / Open / Save / Apply."""
    host = QWidget()
    host.setObjectName("SuiteDocBanner")
    host.setFixedHeight(_ui.TOOL_H)
    host.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
    lay = QHBoxLayout(host)
    lay.setContentsMargins(_ui.PAD_X, _ui.STRIP_PAD_V, 6, _ui.STRIP_PAD_V)
    lay.setSpacing(2)
    path_lab = QLabel("No EDS")
    path_lab.setObjectName("SuiteDocPath")
    path_lab.setToolTip("Current EDS file")
    lay.addWidget(path_lab, 1, Qt.AlignmentFlag.AlignVCenter)
    new_btn = _ui.icon_tool("add", "New EDS (Ctrl+N)")
    open_btn = _ui.icon_tool("folder", "Open EDS (Ctrl+O)")
    save_btn = _ui.icon_tool("save", "Save EDS (Ctrl+S)")
    apply_btn = _ui.icon_tool("apply", "Apply EDS → Live OD (Ctrl+Return)")
    for b in (new_btn, open_btn, save_btn, apply_btn):
        lay.addWidget(b, 0, Qt.AlignmentFlag.AlignVCenter)

    def refresh(_=None):
        session = getattr(shell, "session", None)
        p = getattr(session, "eds_path", "") or ""
        dirty = "● " if getattr(session, "eds_dirty", False) else ""
        nid = getattr(session, "node_id", 1)
        if p:
            path_lab.setText("%s%s" % (dirty, os.path.basename(p)))
            path_lab.setToolTip("%s\nNode-ID %d · Ctrl+S" % (p, nid))
        else:
            n = len(getattr(session, "draft_entries", None) or [])
            if n:
                path_lab.setText("%sUntitled · %d" % (dirty, n))
                path_lab.setToolTip("Unsaved draft · Node-ID %d" % nid)
            else:
                path_lab.setText("No EDS")
                path_lab.setToolTip("New or Open an EDS")

    def _run(name: str):
        if hasattr(shell, "run_action"):
            shell.run_action(name)

    new_btn.clicked.connect(lambda: _run("eds.new"))
    open_btn.clicked.connect(lambda: _run("eds.open"))
    save_btn.clicked.connect(lambda: _run("eds.save"))
    apply_btn.clicked.connect(lambda: _run("eds.apply_od"))
    host.refresh_doc = refresh  # type: ignore[attr-defined]
    refresh()
    if hasattr(shell, "session") and hasattr(shell.session, "on_od_changed"):
        shell.session.on_od_changed(refresh)
    if hasattr(shell, "session") and hasattr(shell.session, "on_node_changed"):
        shell.session.on_node_changed(refresh)
    return host


def build_section_sidebar(
        shell,
        title: str,
        sections: Sequence[SectionSpec],
        *,
        nested: Optional[dict] = None,
        on_select: Optional[Callable[[str], None]] = None,
        show_document: bool = False,
) -> QWidget:
    """Flat leaf list — optional compact document row above."""
    nested = nested or {}
    root = QWidget()
    root.setObjectName("SuiteSideBar")
    root.setMinimumWidth(180)
    root.setMaximumWidth(280)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)

    doc = None
    if show_document:
        doc = _doc_banner(shell)
        lay.addWidget(doc)
        lay.addWidget(_ui.hairline())

    lay.addWidget(_ui.sidebar_header(title))

    tree = QTreeWidget()
    _ui.style_tree(tree)
    tree.setExpandsOnDoubleClick(False)
    # Flat list — never show branch chrome unless nested is non-empty
    tree.setRootIsDecorated(bool(any(nested.values())))
    tree.setIndentation(10 if any(nested.values()) else 0)
    lay.addWidget(tree, 1)

    _guard = {"depth": 0}
    _keys = [s[0] for s in sections]
    for kids in (nested or {}).values():
        for ck, *_rest in kids:
            if ck not in _keys:
                _keys.append(ck)

    def rebuild():
        _guard["depth"] += 1
        try:
            tree.blockSignals(True)
            tree.clear()
            for key, label, tip in sections:
                children = nested.get(key) or ()
                if children:
                    it = QTreeWidgetItem([label])
                    it.setData(0, Qt.ItemDataRole.UserRole, ("section", key))
                    it.setToolTip(0, tip)
                    it.setFlags(Qt.ItemFlag.ItemIsEnabled)
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
                    tree.addTopLevelItem(it)
                else:
                    it = QTreeWidgetItem([label])
                    it.setData(0, Qt.ItemDataRole.UserRole, ("leaf", key))
                    it.setToolTip(0, tip)
                    it.setFlags(
                        Qt.ItemFlag.ItemIsEnabled
                        | Qt.ItemFlag.ItemIsSelectable)
                    it.setChildIndicatorPolicy(
                        QTreeWidgetItem.ChildIndicatorPolicy.DontShowIndicator)
                    tree.addTopLevelItem(it)
        finally:
            tree.blockSignals(False)
            _guard["depth"] -= 1

    def select_section(feature: str):
        def walk(item):
            data = item.data(0, Qt.ItemDataRole.UserRole)
            if not data:
                return False
            if data[0] in ("leaf", "section", "child") and data[1] == feature:
                tree.setCurrentItem(item)
                return True
            for i in range(item.childCount()):
                if walk(item.child(i)):
                    return True
            return False
        for i in range(tree.topLevelItemCount()):
            if walk(tree.topLevelItem(i)):
                return

    def _activate(feature: str):
        if on_select:
            on_select(feature)
        elif hasattr(shell, "goto_page"):
            shell.goto_page(feature)

    def _on_click():
        if _guard["depth"]:
            return
        item = tree.currentItem()
        if item is None:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        kind = data[0]
        if kind in ("leaf", "child"):
            _activate(data[1])
        elif kind == "section":
            _activate(data[1])
            if nested.get(data[1]):
                item.setExpanded(True)

    tree.itemClicked.connect(lambda *_: _on_click())
    rebuild()

    root.rebuild = rebuild  # type: ignore[attr-defined]
    root.select_section = select_section  # type: ignore[attr-defined]
    root.feature_keys = _keys  # type: ignore[attr-defined]
    root.tree = tree  # type: ignore[attr-defined]
    root.refresh_doc = getattr(doc, "refresh_doc", None)  # type: ignore[attr-defined]
    root.setToolTip("%s · Ctrl+B" % title)
    return root


# ---- Flat explorers (five-pillar IA) ---------------------------------------

PROJECT_SECTIONS = (
    ("eds_dict", "Objects", "Edit the object dictionary"),
)
PROJECT_NESTED: dict = {}


def build_eds_sidebar(shell) -> QWidget:
    """EDS side bar — edit leaves only (files live under File menu)."""
    return build_section_sidebar(
        shell, "EDS", EDS_SECTIONS, nested=EDS_NESTED, show_document=True)


# Back-compat
build_project_sidebar = build_eds_sidebar


# EDS edit leaves
EDS_SECTIONS = (
    ("eds_dict", "Objects", "Edit the object dictionary in the EDS file"),
    ("profiles", "Profiles", "Insert CiA packs into the draft"),
    ("eds_pdo", "PDO map", "Map objects into RPDO / TPDO in the file"),
    ("eds_check", "Check", "Validate before save / apply"),
)
EDS_NESTED: dict = {}

# Live = control + Drive (CiA 402) + Live PDO viz
LIVE_SECTIONS = (
    ("od", "Live OD", "SDO read / write on the selected Node-ID"),
    ("pdo", "Live PDO", "RPDO/TPDO map + live unpack"),
    ("drive", "Drive", "CiA 402 controlword / statusword"),
    ("network_scan", "Scan", "Find nodes 1-127"),
    ("network_nmt", "NMT", "Start / Stop / Pre-op / Reset"),
    ("network_lss", "LSS", "Configure Node-ID (lite)"),
)
LIVE_NESTED: dict = {}
DEVICE_SECTIONS = LIVE_SECTIONS
DEVICE_NESTED = LIVE_NESTED

# Trace = online + offline analysis (one leaf)
TRACE_SECTIONS = (
    ("monitor", "Trace", "Live decode or Import CSV"),
)
TRACE_NESTED: dict = {}
NETWORK_SECTIONS = TRACE_SECTIONS
NETWORK_NESTED = TRACE_NESTED

# Code = codegen only
CODE_SECTIONS = (
    ("eds_codegen", "Codegen", "Emit OD C/H for firmware"),
)
CODE_NESTED: dict = {}

# Back-compat
SETUP_SECTIONS = PROJECT_SECTIONS
SETUP_NESTED = PROJECT_NESTED
LIBRARY_SECTIONS = EDS_SECTIONS
LIBRARY_NESTED = EDS_NESTED
