# -*- coding: utf-8 -*-
"""Compare — A (open) vs B ARXML."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QHeaderView,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import arxmlparse, suite_chrome
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    open_b = _ui.ghost_btn("Open B…", "Second ARXML", "browse")
    run_btn = _ui.primary_btn("Compare", "Diff COM models", "search")
    path_lbl = _ui.quiet_label("B: (none)")
    crow.addWidget(open_b)
    crow.addWidget(run_btn)
    crow.addWidget(path_lbl, 1)
    layout.addWidget(chrome)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Kind", "PDU", "Signal", "Detail"])
    _ui.style_tree(tree)
    tree.setRootIsDecorated(False)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    tree.setToolTip("Double-click to jump to Editor (A side)")
    layout.addWidget(tree, 1)

    peer = {"model": None, "path": ""}

    def _open():
        path, _ = QFileDialog.getOpenFileName(
            shell, "Open ARXML B", "", "ARXML (*.arxml *.xml);;All (*)")
        if not path:
            return
        try:
            peer["model"] = arxmlparse.parse_arxml_model(path)
            peer["path"] = path
            document.compare_model = peer["model"]
            document.compare_path = path
            path_lbl.setText("B: %s" % os.path.basename(path))
            _run()
        except OSError as e:
            log_fn("ERR", str(e))

    def _run():
        if peer["model"] is None:
            log_fn("WARN", "Open B first")
            return
        rows = arxmlparse.diff_models(document.model, peer["model"])
        tree.clear()
        colors = {
            "added": QColor("#2E7D32"),
            "removed": QColor("#C62828"),
            "changed": QColor("#EF6C00"),
        }
        for r in rows:
            item = QTreeWidgetItem([
                r.get("kind", ""), r.get("pdu", ""),
                r.get("signal", ""), r.get("detail", ""),
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, r)
            c = colors.get(r.get("kind"), QColor("#546E7A"))
            for col in range(4):
                item.setForeground(col, c)
            tree.addTopLevelItem(item)
        path_lbl.setText("B: %s · %d diff" % (
            os.path.basename(peer["path"] or "?"), len(rows)))
        log_fn("OK", "Compare: %d" % len(rows))

    def _goto(item, _c):
        r = item.data(0, Qt.ItemDataRole.UserRole) or {}
        shell.goto_editor_target(r.get("pdu", ""), r.get("signal", ""))

    open_b.clicked.connect(_open)
    run_btn.clicked.connect(_run)
    tree.itemDoubleClicked.connect(_goto)
    return root
