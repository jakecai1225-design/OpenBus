# -*- coding: utf-8 -*-
"""Profiles workspace — browse CiA 301 / 402 libraries; insert into EDS draft."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QPushButton,
    QTabWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core.od_cia301 import CIA301_OBJECTS, search_cia301
from core.od_cia402 import CIA402_OBJECTS, search_cia402


def _fill_tree(tree: QTreeWidget, entries):
    tree.clear()
    for e in entries:
        item = QTreeWidgetItem([
            e.display_index(),
            e.name,
            e.access_type,
            e.data_type,
            e.default_value,
        ])
        item.setData(0, Qt.ItemDataRole.UserRole, e)
        tree.addTopLevelItem(item)


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    layout.addWidget(plugin_shell.help_label(
        "Searchable CiA 301 communication and CiA 402 drive profile stubs. "
        "Select rows and Insert into EDS editor draft (does not overwrite "
        "existing index:subindex pairs)."))

    search = QLineEdit()
    search.setPlaceholderText("Search index or name…")
    layout.addWidget(search)

    tabs = QTabWidget()
    tree301 = QTreeWidget()
    tree402 = QTreeWidget()
    for t in (tree301, tree402):
        t.setHeaderLabels(["Index", "Name", "Access", "DataType", "Default"])
        t.setSelectionMode(QTreeWidget.SelectionMode.ExtendedSelection)
        t.setAlternatingRowColors(True)
        t.header().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    tabs.addTab(tree301, "CiA 301 (%d)" % len(CIA301_OBJECTS))
    tabs.addTab(tree402, "CiA 402 (%d)" % len(CIA402_OBJECTS))
    layout.addWidget(tabs, 1)

    btn_row = QHBoxLayout()
    insert_btn = QPushButton("Insert selected into EDS draft")
    insert_all_btn = QPushButton("Insert visible list")
    goto_btn = QPushButton("Open EDS Editor")
    btn_row.addWidget(insert_btn)
    btn_row.addWidget(insert_all_btn)
    btn_row.addWidget(goto_btn)
    btn_row.addStretch()
    layout.addLayout(btn_row)
    layout.addWidget(QLabel(
        "Tip: after insert, switch to EDS Editor to review and export."))

    def refresh():
        q = search.text()
        _fill_tree(tree301, search_cia301(q))
        _fill_tree(tree402, search_cia402(q))
        tabs.setTabText(0, "CiA 301 (%d)" % tree301.topLevelItemCount())
        tabs.setTabText(1, "CiA 402 (%d)" % tree402.topLevelItemCount())

    def _selected_entries():
        tree = tree301 if tabs.currentIndex() == 0 else tree402
        out = []
        for item in tree.selectedItems():
            e = item.data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                out.append(e)
        return out

    def _visible_entries():
        tree = tree301 if tabs.currentIndex() == 0 else tree402
        out = []
        for i in range(tree.topLevelItemCount()):
            e = tree.topLevelItem(i).data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                out.append(e)
        return out

    def on_insert():
        entries = _selected_entries()
        if not entries:
            plugin_shell.set_status(parent, "Select one or more rows", 2500)
            return
        session.set_draft_from_library(entries, merge=True)
        log_fn("RX", "-", b"", "Inserted %d profile objects into EDS draft" % len(entries))
        plugin_shell.set_status(parent, "Inserted %d objects" % len(entries), 3000)

    def on_insert_all():
        entries = _visible_entries()
        if not entries:
            return
        session.set_draft_from_library(entries, merge=True)
        log_fn("RX", "-", b"", "Inserted %d visible profile objects" % len(entries))
        plugin_shell.set_status(parent, "Inserted %d objects" % len(entries), 3000)

    def on_goto():
        if hasattr(parent, "goto_page"):
            parent.goto_page("eds_editor")

    search.textChanged.connect(lambda _=None: refresh())
    insert_btn.clicked.connect(on_insert)
    insert_all_btn.clicked.connect(on_insert_all)
    goto_btn.clicked.connect(on_goto)
    refresh()
    return root
