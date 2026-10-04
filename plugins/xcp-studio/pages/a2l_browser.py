# -*- coding: utf-8 -*-
"""A2L — browse applied symbols (read-only)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget

from pages import _ui


def build(shell, session, log_fn) -> QWidget:
    root = QWidget()
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)

    open_btn = _ui.primary_btn("Open A2L…", "Load description", "file")
    edit_btn = _ui.ghost_btn("Edit in A2L Studio", "File-side workbench", "go-to-file")
    count = _ui.count_label()
    lay.addWidget(_ui.tool_strip(open_btn, edit_btn, count, stretch_at=2))

    tree = QTreeWidget()
    tree.setHeaderLabels(["Name", "Kind", "Address", "Type"])
    _ui.style_tree(tree)
    tree.setRootIsDecorated(False)
    lay.addWidget(tree, 1)

    def refresh():
        tree.clear()
        n = 0
        for sym in session.symbols():
            addr = ("0x%X" % sym.address) if sym.address else ""
            item = QTreeWidgetItem([
                sym.name, sym.kind, addr, sym.datatype or sym.char_type or ""])
            item.setData(0, Qt.ItemDataRole.UserRole, (sym.kind, sym.name))
            tree.addTopLevelItem(item)
            n += 1
        count.setText("%d symbols" % n)
        path = session.a2l_path or "(none)"
        count.setToolTip(path)

    def on_dbl(_item, _col):
        item = tree.currentItem()
        if not item:
            return
        kind, name = item.data(0, Qt.ItemDataRole.UserRole) or ("", "")
        if kind == "MEASUREMENT":
            shell.run_action("xcp.goto", page="measure")
        else:
            shell.run_action("xcp.goto", page="calibrate")

    open_btn.clicked.connect(lambda: shell.run_action("xcp.open_a2l"))
    edit_btn.clicked.connect(lambda: shell.run_action("xcp.edit_a2l"))
    tree.itemDoubleClicked.connect(on_dbl)
    session.on_a2l_changed(refresh)
    session.on_changed(refresh)
    refresh()
    root.refresh = refresh  # type: ignore[attr-defined]
    return root
