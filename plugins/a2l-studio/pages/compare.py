# -*- coding: utf-8 -*-
"""Compare — diff two A2L documents."""

from __future__ import annotations

import os

from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import a2lparse, suite_chrome
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    open_b = _ui.ghost_btn("Open B…", "Choose the second A2L", "browse")
    run_btn = _ui.primary_btn("Compare", "Diff A (open) vs B", "search")
    path_lbl = _ui.count_label()
    path_lbl.setText("B: (none)")
    crow.addWidget(open_b)
    crow.addWidget(run_btn)
    crow.addWidget(path_lbl, 1)
    layout.addWidget(chrome)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Change", "Kind", "Name", "Detail"])
    _ui.style_tree(tree)
    tree.setRootIsDecorated(False)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    layout.addWidget(tree, 1)

    peer = {"doc": None, "path": ""}

    def _open_b():
        path, _ = QFileDialog.getOpenFileName(
            shell, "Open A2L B", "",
            "A2L (*.a2l);;All (*)")
        if not path:
            return
        try:
            peer["doc"] = a2lparse.parse_a2l_file(path)
            peer["path"] = path
            document.compare_doc = peer["doc"]
            document.compare_path = path
            path_lbl.setText("B: %s" % os.path.basename(path))
            path_lbl.setToolTip(path)
            log_fn("OK", "-", b"", "Loaded compare peer %s" % path)
            _run()
        except OSError as e:
            log_fn("ERR", "-", b"", "Open B failed: %s" % e)

    def _run():
        if peer["doc"] is None:
            log_fn("SYS", "-", b"", "Open document B first")
            return
        rows = a2lparse.compare_documents(document.doc, peer["doc"])
        tree.clear()
        colors = {
            "added": QColor("#2E7D32"),
            "removed": QColor("#C62828"),
            "address": QColor("#EF6C00"),
        }
        for r in rows:
            detail = ""
            if r.get("change") == "address":
                detail = "%s → %s" % (r.get("a"), r.get("b"))
            item = QTreeWidgetItem([
                r.get("change", ""), r.get("kind", ""),
                r.get("name", ""), detail])
            item.setForeground(0, colors.get(r.get("change"), QColor("#333")))
            tree.addTopLevelItem(item)
        log_fn("SYS", "-", b"", "Compare: %d change(s)" % len(rows))

    open_b.clicked.connect(_open_b)
    run_btn.clicked.connect(_run)
    root.refresh = lambda: None  # type: ignore[attr-defined]
    return root
