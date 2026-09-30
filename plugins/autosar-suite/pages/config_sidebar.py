# -*- coding: utf-8 -*-
"""Config Side Bar — VS Code Explorer-style sections for Config workspace.

Sections: BSW (with module catalog), Editor, Spec, SWC.
Activity bar stays outside; this panel is the collapsible Side Bar body.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QHeaderView,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import arxml_bsw, arxml_ecuc_schema
from pages import _ui

# (feature_key, section title, tip)
CONFIG_SECTIONS = (
    ("bsw", "BSW", "Schema-driven BSW module configurator"),
    ("editor", "Editor", "COM / ECUC extract editor"),
    ("spec", "Spec", "AUTOSAR encyclopedia and BSWMD tips"),
    ("swc", "SWC", "SWC-lite ports and runnables"),
)


def build_config_sidebar(shell, document) -> QWidget:
    """Build the Config Explorer tree; wires into shell.goto_page / BSW select."""
    root = QWidget()
    root.setObjectName("ConfigSideBar")
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)

    head = QWidget()
    head.setObjectName("SuiteSideBarHeader")
    head.setFixedHeight(28)
    hl = QHBoxLayout(head)
    hl.setContentsMargins(10, 0, 6, 0)
    title = QLabel("CONFIG")
    title.setObjectName("SuiteToolbarTitle")
    hl.addWidget(title, 1)
    lay.addWidget(head)

    filter_ed = QLineEdit()
    filter_ed.setPlaceholderText("Filter modules…")
    filter_ed.setClearButtonEnabled(True)
    filter_ed.setFixedHeight(_ui.CTRL_H)
    filter_ed.setToolTip("Filter BSW catalog (BSW section only)")
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
    tree.setIndentation(14)
    tree.setAnimated(True)
    tree.setExpandsOnDoubleClick(False)
    tree.setUniformRowHeights(True)
    _ui.style_tree(tree)
    lay.addWidget(tree, 1)

    _filter = {"q": ""}
    _guard = {"depth": 0}

    def _section_item(key: str, label: str, tip: str) -> QTreeWidgetItem:
        it = QTreeWidgetItem([label])
        it.setData(0, Qt.ItemDataRole.UserRole, ("section", key))
        it.setToolTip(0, tip)
        it.setFlags(
            Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
        return it

    def _fill_bsw_children(bsw_item: QTreeWidgetItem):
        while bsw_item.childCount():
            bsw_item.removeChild(bsw_item.child(0))
        q = (_filter["q"] or "").strip().lower()
        groups = arxml_bsw.catalog_by_group()
        for group, rows in groups.items():
            gitem = QTreeWidgetItem([group])
            gitem.setData(0, Qt.ItemDataRole.UserRole, ("group", group))
            gitem.setFlags(
                (gitem.flags() | Qt.ItemFlag.ItemIsEnabled)
                & ~Qt.ItemFlag.ItemIsSelectable)
            visible = 0
            for name, summary in rows:
                if q and q not in name.lower() and q not in group.lower():
                    continue
                mit = QTreeWidgetItem([name])
                mit.setData(0, Qt.ItemDataRole.UserRole, ("module", name))
                mit.setToolTip(0, summary)
                status = "stub"
                if name in document.bsw:
                    status = "loaded"
                if document.has_project() and document.manifest:
                    import os
                    p = document.manifest.abs_module(name)
                    if p and os.path.isfile(p):
                        status = "on disk"
                mit.setToolTip(0, "%s · %s" % (summary, status))
                gitem.addChild(mit)
                visible += 1
            if visible:
                bsw_item.addChild(gitem)
        bsw_item.setExpanded(True)
        for i in range(bsw_item.childCount()):
            bsw_item.child(i).setExpanded(True)

    def rebuild():
        _guard["depth"] += 1
        try:
            tree.blockSignals(True)
            tree.clear()
            for key, label, tip in CONFIG_SECTIONS:
                sec = _section_item(key, label, tip)
                tree.addTopLevelItem(sec)
                if key == "bsw":
                    _fill_bsw_children(sec)
                else:
                    # Leaf section — no children (VS Code empty view header).
                    sec.setChildIndicatorPolicy(
                        QTreeWidgetItem.ChildIndicatorPolicy.DontShowIndicator)
            tree.blockSignals(False)
            # Reselect active feature section.
            active = getattr(shell, "_active_feature", "bsw") or "bsw"
            _select_section(active, expand_bsw=True)
        finally:
            _guard["depth"] -= 1

    def _select_section(feature: str, expand_bsw: bool = False):
        for i in range(tree.topLevelItemCount()):
            it = tree.topLevelItem(i)
            data = it.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] == ("section", feature):
                tree.blockSignals(True)
                tree.setCurrentItem(it)
                tree.blockSignals(False)
                if feature == "bsw" and expand_bsw:
                    it.setExpanded(True)
                return

    def select_module_in_tree(name: str):
        for i in range(tree.topLevelItemCount()):
            sec = tree.topLevelItem(i)
            data = sec.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] != ("section", "bsw"):
                continue
            sec.setExpanded(True)
            for g in range(sec.childCount()):
                group = sec.child(g)
                group.setExpanded(True)
                for m in range(group.childCount()):
                    mit = group.child(m)
                    md = mit.data(0, Qt.ItemDataRole.UserRole) or ()
                    if md[:2] == ("module", name):
                        tree.blockSignals(True)
                        tree.setCurrentItem(mit)
                        tree.blockSignals(False)
                        return True
        return False

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
        elif kind == "group":
            item.setExpanded(not item.isExpanded())

    def _on_filter(text: str):
        _filter["q"] = text or ""
        # Rebuild only BSW children.
        for i in range(tree.topLevelItemCount()):
            sec = tree.topLevelItem(i)
            data = sec.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] == ("section", "bsw"):
                _fill_bsw_children(sec)
                break

    def _on_doc_changed():
        if _guard["depth"]:
            return
        # Refresh module status tips under BSW without losing expansion.
        for i in range(tree.topLevelItemCount()):
            sec = tree.topLevelItem(i)
            data = sec.data(0, Qt.ItemDataRole.UserRole) or ()
            if data[:2] == ("section", "bsw"):
                expanded = sec.isExpanded()
                _fill_bsw_children(sec)
                sec.setExpanded(expanded)
                break

    tree.itemClicked.connect(lambda *_: _on_click())
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
