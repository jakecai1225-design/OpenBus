# -*- coding: utf-8 -*-
"""Side Bar — flat leaves per activity (UDS Suite).

Diagnose owns Session / Services / DID / DTC / SecAccess / Flash / Profiles.
Scan / Batch / Security are single-leaf workspaces (sidebar still lists the leaf).
"""

from __future__ import annotations

from typing import Callable, Optional, Sequence, Tuple

from PyQt6.QtCore import QSize, Qt
from PyQt6.QtWidgets import (
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, vscode_theme as T
from pages import _ui

SectionSpec = Tuple[str, str, str, str]

DIAGNOSE_SECTIONS = (
    ("session", "Session", "TX/RX IDs, diagnostic session, keep-alive", "settings"),
    ("services", "Services", "UDS service catalog and request builder", "beaker"),
    ("did", "DID", "Read / write data by identifier", "list"),
    ("dtc", "DTC", "Read and clear diagnostic trouble codes", "check"),
    ("sec_access", "SecAccess", "SecurityAccess seed/key", "lock"),
    ("flash", "Flash", "Download / transfer helper", "deliver"),
    ("profiles", "Profiles", "ECU profile and sequence presets", "account"),
)
SCAN_SECTIONS = (
    ("scan", "Scan", "Discover responding request IDs", "search"),
)
BATCH_SECTIONS = (
    ("batch", "Batch", "Run a scripted request sequence", "checklist"),
)
SECURITY_SECTIONS = (
    ("security", "Security", "Observational SecurityAccess audit", "lock"),
)
SETUP_SECTIONS = (
    ("setup", "Setup", "Bus IDs and timing preferences", "gear"),
)

_LEAF_ICON = 16


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
    tree.setIconSize(QSize(_LEAF_ICON, _LEAF_ICON))
    tree.setExpandsOnDoubleClick(False)
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


def build_diagnose_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Diagnose", DIAGNOSE_SECTIONS)


def build_scan_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Scan", SCAN_SECTIONS)


def build_batch_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Batch", BATCH_SECTIONS)


def build_security_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Security", SECURITY_SECTIONS)


def build_setup_sidebar(shell) -> QWidget:
    return build_section_sidebar(shell, "Setup", SETUP_SECTIONS)
