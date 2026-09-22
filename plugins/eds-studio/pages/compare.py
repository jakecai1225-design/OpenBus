# -*- coding: utf-8 -*-
"""Compare — two EDS/DCF documents diff."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import edsparse, suite_chrome
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    open_b = _ui.ghost_btn("Open B…", "Choose the second EDS/DCF", "browse")
    run_btn = _ui.primary_btn("Compare", "Diff A (open) vs B", "search")
    path_lbl = _ui.count_label()
    path_lbl.setText("B: (none)")
    merge_btn = _ui.ghost_btn(
        "Merge added", "Copy objects that exist only in B into A", "add")
    crow.addWidget(open_b)
    crow.addWidget(run_btn)
    crow.addWidget(path_lbl, 1)
    crow.addWidget(merge_btn)
    layout.addWidget(chrome)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Kind", "Index", "Name", "Detail"])
    _ui.style_tree(tree, stretch_col=3)
    tree.setRootIsDecorated(False)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.setToolTip("A = open document · Double-click to jump to Editor")
    layout.addWidget(tree, 1)

    peer = {"eds": None, "path": ""}

    def _open_b():
        path, _ = QFileDialog.getOpenFileName(
            shell, "Open EDS/DCF B", "",
            "EDS/DCF (*.eds *.dcf);;All (*)")
        if not path:
            return
        try:
            peer["eds"] = edsparse.parse_eds_file_document(path)
            peer["path"] = path
            document.compare_eds = peer["eds"]
            document.compare_path = path
            path_lbl.setText("B: %s" % os.path.basename(path))
            path_lbl.setToolTip(path)
            log_fn("OK", "Loaded compare peer %s" % path)
            _run()
        except OSError as e:
            log_fn("ERR", "Open B failed: %s" % e)

    def _run():
        if peer["eds"] is None:
            log_fn("WARN", "Open document B first")
            return
        rows = edsparse.diff_documents(document.eds, peer["eds"])
        tree.clear()
        colors = {
            "added": QColor("#2E7D32"),
            "removed": QColor("#C62828"),
            "changed": QColor("#EF6C00"),
        }
        for r in rows:
            item = QTreeWidgetItem([
                r.get("kind", ""), r.get("index", ""),
                r.get("name", ""), r.get("detail", ""),
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, r)
            c = colors.get(r.get("kind"), QColor("#546E7A"))
            for col in range(4):
                item.setForeground(col, c)
            tree.addTopLevelItem(item)
        path_lbl.setText("B: %s · %d diff" % (
            os.path.basename(peer["path"]) if peer["path"] else "?",
            len(rows)))
        log_fn("OK", "Compare: %d difference(s)" % len(rows))

    def _merge_added():
        if peer["eds"] is None:
            return
        a_keys = {(e.index, e.subindex) for e in document.eds.entries}
        extra = [
            e for e in peer["eds"].entries
            if (e.index, e.subindex) not in a_keys]
        if not extra:
            log_fn("SYS", "Nothing to merge from B")
            return
        document.upsert_entries(extra, merge=True)
        log_fn("OK", "Merged %d object(s) from B" % len(extra))
        _run()

    def _goto(item, _col):
        r = item.data(0, Qt.ItemDataRole.UserRole) or {}
        idx_text = r.get("index") or ""
        try:
            if ":" in idx_text:
                a, b = idx_text.split(":", 1)
                shell.goto_editor_object(int(a, 16), int(b, 16))
            else:
                shell.goto_editor_object(int(idx_text, 16), 0)
        except ValueError:
            pass

    open_b.clicked.connect(_open_b)
    run_btn.clicked.connect(_run)
    merge_btn.clicked.connect(_merge_added)
    tree.itemDoubleClicked.connect(_goto)
    return root
