# -*- coding: utf-8 -*-
"""Profile library — CiA 301 / 402 stubs for the EDS draft.

Tabs live in the suite chrome row (not inside the page).
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QHeaderView,
    QLineEdit,
    QPushButton,
    QStackedWidget,
    QTabBar,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core.od_cia301 import CIA301_OBJECTS, search_cia301
from core.od_cia402 import CIA402_OBJECTS, search_cia402


def _fill(tree: QTreeWidget, entries):
    tree.clear()
    for e in entries:
        item = QTreeWidgetItem([
            e.display_index(), e.name, e.access_type, e.data_type, e.default_value,
        ])
        item.setData(0, Qt.ItemDataRole.UserRole, e)
        tree.addTopLevelItem(item)


def _make_tree() -> QTreeWidget:
    tree = QTreeWidget()
    tree.setHeaderLabels(["Index", "Name", "Access", "DataType", "Default"])
    tree.setSelectionMode(QTreeWidget.SelectionMode.ExtendedSelection)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    return tree


def build(parent, session, log_fn) -> QWidget:
    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    bar.addTab("CiA 301")
    bar.addTab("CiA 402")
    parent._library_tabs = bar

    stack = QStackedWidget()
    tree301 = _make_tree()
    tree402 = _make_tree()
    stack.addWidget(tree301)
    stack.addWidget(tree402)
    bar.currentChanged.connect(stack.setCurrentIndex)

    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(8, 6, 8, 6)
    layout.setSpacing(6)

    tools = QHBoxLayout()
    search = QLineEdit()
    search.setPlaceholderText("Filter index or name")
    search.setFixedHeight(28)
    search.setMaximumWidth(240)
    search.setToolTip("Filter the active profile list")
    insert = QPushButton("Insert")
    insert.setObjectName("PrimaryButton")
    insert.setFixedHeight(28)
    insert.setCursor(Qt.CursorShape.PointingHandCursor)
    insert.setToolTip("Merge selected objects into the EDS draft")
    insert_all = QPushButton("Insert list")
    insert_all.setObjectName("GhostButton")
    insert_all.setFixedHeight(28)
    insert_all.setCursor(Qt.CursorShape.PointingHandCursor)
    insert_all.setToolTip("Merge every visible row into the EDS draft")
    goto = QPushButton("EDS")
    goto.setObjectName("GhostButton")
    goto.setFixedHeight(28)
    goto.setCursor(Qt.CursorShape.PointingHandCursor)
    goto.setToolTip("Open the EDS Dictionary")
    tools.addWidget(search)
    tools.addStretch(1)
    tools.addWidget(insert)
    tools.addWidget(insert_all)
    tools.addWidget(goto)
    layout.addLayout(tools)
    layout.addWidget(stack, 1)

    def refresh():
        q = search.text()
        _fill(tree301, search_cia301(q))
        _fill(tree402, search_cia402(q))
        bar.setTabText(0, "CiA 301 (%d)" % tree301.topLevelItemCount())
        bar.setTabText(1, "CiA 402 (%d)" % tree402.topLevelItemCount())

    def _active_tree():
        return tree301 if stack.currentIndex() == 0 else tree402

    def _selected():
        out = []
        for item in _active_tree().selectedItems():
            e = item.data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                out.append(e)
        return out

    def _visible():
        tree = _active_tree()
        out = []
        for i in range(tree.topLevelItemCount()):
            e = tree.topLevelItem(i).data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                out.append(e)
        return out

    def on_insert():
        entries = _selected()
        if not entries:
            plugin_shell.set_status(parent, "Select one or more rows", 2500)
            return
        session.set_draft_from_library(entries, merge=True)
        log_fn("RX", "-", b"", "Inserted %d objects into EDS draft" % len(entries))
        plugin_shell.set_status(parent, "Inserted %d" % len(entries), 3000)

    def on_insert_all():
        entries = _visible()
        if not entries:
            return
        session.set_draft_from_library(entries, merge=True)
        log_fn("RX", "-", b"", "Inserted %d visible objects" % len(entries))
        plugin_shell.set_status(parent, "Inserted %d" % len(entries), 3000)

    search.textChanged.connect(lambda _=None: refresh())
    insert.clicked.connect(on_insert)
    insert_all.clicked.connect(on_insert_all)
    goto.clicked.connect(lambda: parent.goto_page("eds") if hasattr(parent, "goto_page") else None)
    bar.setTabText(0, "CiA 301 (%d)" % len(CIA301_OBJECTS))
    bar.setTabText(1, "CiA 402 (%d)" % len(CIA402_OBJECTS))
    refresh()
    return root
