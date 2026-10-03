# -*- coding: utf-8 -*-
"""Config Side Bar — VS Code Explorer-style tree.

Layout (matches VS Code Explorer organization):
  CONFIG                          # view title (header)
  Filter modules…                 # inline filter (BSW catalog only)
  ▼ BSW           [folder]        # expandable catalog root
      ▶ System    [folder]        # groups collapsed by default
          Os      [file]          # selectable module leaf
      …
  Editor          [edit]          # flat feature leaf (no twistie)
  Spec / SWC
  ▼ Quality       [checklist]     # tool folder (expand-only)
      Findings / Analysis / …

Rules: containers = folder icon + expand/collapse; leaves = icon + select → Tab.
Do not expand every group on open (VS Code keeps folders collapsed).
"""

from __future__ import annotations

import os

from PyQt6.QtCore import QSize, Qt
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLineEdit,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import arxml_bsw, arxml_ecuc_schema, codicons, vscode_theme as T
from pages import _ui

_ICON = 16

# Flat feature leaves under CONFIG (not nested under BSW).
# (feature_key, label, tip, codicon)
_FEATURE_LEAVES = (
    ("editor", "Editor", "COM / ECUC extract editor", "edit"),
    ("spec", "Spec", "AUTOSAR encyclopedia and BSWMD tips", "info"),
    ("swc", "SWC", "SWC-lite ports and runnables", "flow"),
)

# Nested under Quality folder (validate / deliver tools).
# Keys stay in FEATURE_ROUTE under config workspace.
_QUALITY_LEAVES = (
    ("validate", "Findings", "Lint and consistency findings", "check"),
    ("timing", "Analysis", "Coverage and load analysis", "analyze"),
    ("compare", "Compare", "Diff two ARXML files", "search"),
    ("merge", "Merge", "Combine ARXML files", "sync"),
    ("export", "Export", "Export handoff artifacts", "export"),
)

_QUALITY_KEYS = frozenset(k for k, *_ in _QUALITY_LEAVES)

# Keep export name for tests / callers that scan CONFIG_SECTIONS.
CONFIG_SECTIONS = (
    ("bsw", "BSW", "Schema-driven BSW module configurator"),
) + tuple((k, lab, tip) for k, lab, tip, _ic in _FEATURE_LEAVES) + tuple(
    (k, lab, tip) for k, lab, tip, _ic in _QUALITY_LEAVES
)


def _icon(name: str, *, muted: bool = False):
    color = T.TEXT_DIM if muted else T.TEXT
    return codicons.icon(name, color, _ICON)


def _folder_font(base: QFont) -> QFont:
    f = QFont(base)
    f.setBold(True)
    return f


def build_config_sidebar(shell, document) -> QWidget:
    """Build the Config Explorer tree; wires into shell.goto_page / BSW select."""
    root = QWidget()
    root.setObjectName("ConfigSideBar")
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)

    head = _ui.sidebar_header("Config")
    lay.addWidget(head)

    filter_ed = QLineEdit()
    filter_ed.setPlaceholderText("Filter modules…")
    filter_ed.setClearButtonEnabled(True)
    filter_ed.setFixedHeight(_ui.CTRL_H)
    filter_ed.setToolTip("Filter BSW catalog (groups + modules)")
    filter_host = QWidget()
    filter_host.setObjectName("SuiteInlineFilter")
    filter_host.setFixedHeight(_ui.FILTER_H)
    fl = QHBoxLayout(filter_host)
    fl.setContentsMargins(_ui.PAD_X, _ui.STRIP_PAD_V, _ui.PAD_X, _ui.STRIP_PAD_V)
    fl.addWidget(filter_ed, 1)
    lay.addWidget(filter_host)

    tree = QTreeWidget()
    tree.setObjectName("SuiteMatrix")
    tree.setHeaderHidden(True)
    tree.setIndentation(12)
    tree.setAnimated(True)
    tree.setExpandsOnDoubleClick(False)
    tree.setUniformRowHeights(True)
    tree.setIconSize(QSize(_ICON, _ICON))
    tree.setRootIsDecorated(True)
    _ui.style_tree(tree)
    lay.addWidget(tree, 1)

    _filter = {"q": ""}
    _guard = {"depth": 0}
    _base_font = tree.font()
    _folder_f = _folder_font(_base_font)

    def _make_folder(label: str, role: tuple, tip: str) -> QTreeWidgetItem:
        it = QTreeWidgetItem([label])
        it.setData(0, Qt.ItemDataRole.UserRole, role)
        it.setToolTip(0, tip)
        it.setIcon(0, _icon("folder", muted=True))
        it.setFont(0, _folder_f)
        # Expand/collapse only — not a feature leaf.
        it.setFlags(
            (it.flags() | Qt.ItemFlag.ItemIsEnabled)
            & ~Qt.ItemFlag.ItemIsSelectable)
        return it

    def _make_leaf(
            label: str, role: tuple, tip: str, icon_name: str,
    ) -> QTreeWidgetItem:
        it = QTreeWidgetItem([label])
        it.setData(0, Qt.ItemDataRole.UserRole, role)
        it.setToolTip(0, tip)
        it.setIcon(0, _icon(icon_name))
        it.setFlags(
            Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
        it.setChildIndicatorPolicy(
            QTreeWidgetItem.ChildIndicatorPolicy.DontShowIndicator)
        return it

    def _fill_bsw_children(bsw_item: QTreeWidgetItem, *, expand_matches: bool):
        while bsw_item.childCount():
            bsw_item.removeChild(bsw_item.child(0))
        q = (_filter["q"] or "").strip().lower()
        groups = arxml_bsw.catalog_by_group()
        for group, rows in groups.items():
            gitem = _make_folder(group, ("group", group), "%s modules" % group)
            visible = 0
            for name, summary in rows:
                if q and q not in name.lower() and q not in group.lower():
                    continue
                status = "stub"
                if name in document.bsw:
                    status = "loaded"
                if document.has_project() and document.manifest:
                    p = document.manifest.abs_module(name)
                    if p and os.path.isfile(p):
                        status = "on disk"
                tip = "%s · %s" % (summary, status)
                mit = _make_leaf(name, ("module", name), tip, "file")
                gitem.addChild(mit)
                visible += 1
            if visible:
                bsw_item.addChild(gitem)
                # VS Code: keep folders collapsed unless filtering or forced.
                gitem.setExpanded(bool(q) and expand_matches)
        bsw_item.setExpanded(True)

    def rebuild():
        _guard["depth"] += 1
        try:
            tree.blockSignals(True)
            tree.clear()

            bsw = _make_folder(
                "BSW", ("section", "bsw"),
                "Schema-driven BSW module configurator")
            # BSW root is selectable so clicking opens the BSW page.
            bsw.setFlags(
                Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
            tree.addTopLevelItem(bsw)
            _fill_bsw_children(bsw, expand_matches=True)

            for key, label, tip, icon_name in _FEATURE_LEAVES:
                tree.addTopLevelItem(
                    _make_leaf(label, ("section", key), tip, icon_name))

            quality = _make_folder(
                "Quality", ("folder", "quality"),
                "Findings, analysis, compare, merge, export")
            for key, label, tip, icon_name in _QUALITY_LEAVES:
                quality.addChild(
                    _make_leaf(label, ("section", key), tip, icon_name))
            # Collapsed until a Quality leaf is active (select_section expands).
            quality.setExpanded(False)
            tree.addTopLevelItem(quality)

            tree.blockSignals(False)
            active = getattr(shell, "_active_feature", "bsw") or "bsw"
            _select_section(active, expand_bsw=True)
        finally:
            _guard["depth"] -= 1

    def _find_section_item(feature: str):
        for i in range(tree.topLevelItemCount()):
            it = tree.topLevelItem(i)
            data = it.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] == ("section", feature):
                return it
            for j in range(it.childCount()):
                ch = it.child(j)
                cd = ch.data(0, Qt.ItemDataRole.UserRole) or ()
                if cd[:2] == ("section", feature):
                    return ch
        return None

    def _select_section(feature: str, expand_bsw: bool = False):
        it = _find_section_item(feature)
        if it is None:
            return
        tree.blockSignals(True)
        # Expand Quality folder when selecting a nested leaf.
        if feature in _QUALITY_KEYS and it.parent() is not None:
            it.parent().setExpanded(True)
        if feature == "bsw" and expand_bsw:
            it.setExpanded(True)
        tree.setCurrentItem(it)
        tree.blockSignals(False)

    def select_module_in_tree(name: str):
        for i in range(tree.topLevelItemCount()):
            sec = tree.topLevelItem(i)
            data = sec.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] != ("section", "bsw"):
                continue
            sec.setExpanded(True)
            for g in range(sec.childCount()):
                group = sec.child(g)
                for m in range(group.childCount()):
                    mit = group.child(m)
                    md = mit.data(0, Qt.ItemDataRole.UserRole) or ()
                    if md[:2] == ("module", name):
                        group.setExpanded(True)
                        tree.blockSignals(True)
                        tree.setCurrentItem(mit)
                        tree.blockSignals(False)
                        return True
        return False

    def _on_click(item, _column=0):
        if _guard["depth"] or item is None:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        kind = data[0]
        if kind == "section":
            feature = data[1]
            shell.goto_page(feature)
            if feature == "bsw":
                item.setExpanded(True)
        elif kind == "module":
            name = data[1]
            shell.goto_page("bsw")
            bsw = shell._pages.get("bsw")
            if bsw is not None and hasattr(bsw, "select_module"):
                bsw.select_module(name)
            # Keep the leaf selected after goto_page's select_section("bsw").
            select_module_in_tree(name)
        elif kind in ("group", "folder"):
            # VS Code Explorer: click folder row expands (twistie still works).
            # Only expand here — collapsing stays on the twistie to avoid
            # double-toggle when Qt also handles the indicator click.
            if not item.isExpanded():
                item.setExpanded(True)
            return

    def _on_filter(text: str):
        _filter["q"] = text or ""
        for i in range(tree.topLevelItemCount()):
            sec = tree.topLevelItem(i)
            data = sec.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] == ("section", "bsw"):
                _fill_bsw_children(sec, expand_matches=True)
                break

    def _on_doc_changed():
        if _guard["depth"]:
            return
        for i in range(tree.topLevelItemCount()):
            sec = tree.topLevelItem(i)
            data = sec.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] == ("section", "bsw"):
                # Preserve which groups the user already opened.
                open_groups = {
                    sec.child(g).text(0)
                    for g in range(sec.childCount())
                    if sec.child(g).isExpanded()
                }
                expanded = sec.isExpanded()
                _fill_bsw_children(sec, expand_matches=False)
                sec.setExpanded(expanded)
                for g in range(sec.childCount()):
                    child = sec.child(g)
                    if child.text(0) in open_groups:
                        child.setExpanded(True)
                break

    tree.itemClicked.connect(_on_click)
    filter_ed.textChanged.connect(_on_filter)
    document.on_changed(_on_doc_changed)
    rebuild()

    n_sch = len(arxml_ecuc_schema.list_schema_modules())
    root.setToolTip(
        "Config explorer · %d schemas · Ctrl+B toggles this Side Bar"
        % n_sch)

    root.rebuild = rebuild
    root.select_module_in_tree = select_module_in_tree
    root.select_section = _select_section
    root.tree = tree
    return root
