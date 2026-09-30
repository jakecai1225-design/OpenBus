# -*- coding: utf-8 -*-
"""Compare — A (open) vs B ARXML; optional BSW module value diff."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
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
    mode = QComboBox()
    mode.addItems(["COM models", "BSW modules"])
    mode.setFixedHeight(_ui.CTRL_H)
    mode.setToolTip("COM PDU/signal diff or BSW parameter value diff")
    open_b = _ui.ghost_btn("Open B…", "Second ARXML or BSW folder/project", "browse")
    run_btn = _ui.primary_btn("Compare", "Diff A vs B", "search")
    path_lbl = _ui.quiet_label("B: (none)")
    crow.addWidget(mode)
    crow.addWidget(open_b)
    crow.addWidget(run_btn)
    crow.addWidget(path_lbl, 1)
    layout.addWidget(chrome)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Kind", "Module/PDU", "Param/Signal", "Detail"])
    _ui.style_tree(tree)
    tree.setRootIsDecorated(False)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    tree.setToolTip("Double-click to jump to Editor or BSW (A side)")
    layout.addWidget(tree, 1)

    peer = {"model": None, "bsw": None, "path": ""}

    def _open():
        path, _ = QFileDialog.getOpenFileName(
            shell, "Open ARXML B", "", "ARXML (*.arxml *.xml);;All (*)")
        if not path:
            return
        try:
            if mode.currentIndex() == 1:
                # Treat as single-module ECUC-lite or COM; try ECUC first.
                try:
                    peer["bsw"] = {
                        os.path.splitext(os.path.basename(path))[0]:
                        arxmlparse.parse_ecuc_lite(path)}
                    peer["model"] = None
                except Exception:
                    peer["model"] = arxmlparse.parse_arxml_model(path)
                    peer["bsw"] = None
            else:
                peer["model"] = arxmlparse.parse_arxml_model(path)
                peer["bsw"] = None
            peer["path"] = path
            document.compare_model = peer["model"]
            document.compare_path = path
            path_lbl.setText("B: %s" % os.path.basename(path))
            _run()
        except OSError as e:
            log_fn("ERR", str(e))

    def _run():
        tree.clear()
        colors = {
            "added": QColor("#2E7D32"),
            "removed": QColor("#C62828"),
            "changed": QColor("#EF6C00"),
        }
        if mode.currentIndex() == 1:
            if not peer["bsw"]:
                log_fn("WARN", "Open a BSW module ARXML as B first")
                return
            names = sorted(set(document.bsw) | set(peer["bsw"]))
            rows = document.diff_bsw_modules(peer["bsw"], names=names)
            for r in rows:
                item = QTreeWidgetItem([
                    r.get("kind", ""), r.get("module", ""),
                    "%s.%s" % (r.get("container", ""), r.get("param", "")),
                    r.get("detail", ""),
                ])
                item.setData(0, Qt.ItemDataRole.UserRole, r)
                c = colors.get(r.get("kind"), QColor("#546E7A"))
                for col in range(4):
                    item.setForeground(col, c)
                tree.addTopLevelItem(item)
            path_lbl.setText("B: %s · %d BSW diff" % (
                os.path.basename(peer["path"] or "?"), len(rows)))
            log_fn("OK", "BSW compare: %d" % len(rows))
            return
        if peer["model"] is None:
            log_fn("WARN", "Open B first")
            return
        rows = arxmlparse.diff_models(document.model, peer["model"])
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
        if r.get("module"):
            shell.goto_page("bsw")
            page = getattr(shell, "_pages", {}).get("bsw")
            if page is not None and hasattr(page, "select_module"):
                page.select_module(r.get("module"))
            return
        shell.goto_editor_target(r.get("pdu", ""), r.get("signal", ""))

    open_b.clicked.connect(_open)
    run_btn.clicked.connect(_run)
    tree.itemDoubleClicked.connect(_goto)
    return root
