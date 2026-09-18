# -*- coding: utf-8 -*-
"""Compare workspace — A/B DBC diff."""

from __future__ import annotations

import os

from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QFileDialog, QFormLayout, QGroupBox, QHBoxLayout, QLabel,
    QLineEdit, QPushButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
    QHeaderView,
)

from _shared import dbcparse, dbc_picker, plugin_shell
from core import diff_engine


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)

    cfg = QGroupBox("Compare")
    form = QFormLayout(cfg)
    path_a = QLineEdit()
    path_a.setPlaceholderText("Current document or pick file A")
    path_b = QLineEdit()
    path_b.setPlaceholderText("Pick file B")
    form.addRow("A:", path_a)
    form.addRow("B:", path_b)
    layout.addWidget(cfg)

    row = QHBoxLayout()
    use_doc_btn = QPushButton("A = current document")
    pick_a = QPushButton("Browse A…")
    pick_b = QPushButton("Browse B…")
    ws_b = QPushButton("Workspace B…")
    run_btn = QPushButton("Compare")
    export_csv = QPushButton("Export CSV")
    export_html = QPushButton("Export HTML")
    for w in (use_doc_btn, pick_a, pick_b, ws_b, run_btn):
        row.addWidget(w)
    row.addStretch()
    row.addWidget(export_csv)
    row.addWidget(export_html)
    layout.addLayout(row)

    summary = QLabel("Select two DBC files to compare")
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Kind", "ID", "Name", "Detail"])
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    last = {"rows": [], "stats": {}, "a": "", "b": ""}

    def _load_side(edit: QLineEdit, title: str) -> str | None:
        path, _ = QFileDialog.getOpenFileName(
            shell, title, edit.text() or document.path or "",
            "DBC (*.dbc);;All (*)")
        if path:
            edit.setText(path)
        return path

    def _db_a():
        p = path_a.text().strip()
        if not p:
            return document.db, document.path or "(current)"
        return dbcparse.parse_file(p), p

    def _db_b():
        p = path_b.text().strip()
        if not p:
            raise ValueError("Pick file B")
        return dbcparse.parse_file(p), p

    def _on_run():
        try:
            db_a, pa = _db_a()
            db_b, pb = _db_b()
        except Exception as e:
            summary.setText("Compare failed: %s" % e)
            log_fn("ERR", "Compare: %s" % e)
            return
        rows, stats = diff_engine.diff_dbc(db_a, db_b)
        last["rows"], last["stats"], last["a"], last["b"] = rows, stats, pa, pb
        tree.clear()
        colors = {
            "added": QColor("#2E7D32"),
            "removed": QColor("#C62828"),
            "changed": QColor("#EF6C00"),
        }
        for kind, cid, name, detail, children in rows:
            item = QTreeWidgetItem([kind, cid, name, detail])
            item.setForeground(0, colors.get(kind, QColor("#333")))
            for ck, cn, cd in children:
                child = QTreeWidgetItem([ck, "", cn, cd])
                child.setForeground(0, colors.get(ck, QColor("#333")))
                item.addChild(child)
            tree.addTopLevelItem(item)
        summary.setText(
            "msg +%d/-%d/~%d · sig +%d/-%d/~%d"
            % (stats["msg_new"], stats["msg_del"], stats["msg_mod"],
               stats["sig_new"], stats["sig_del"], stats["sig_mod"]))
        log_fn("Compare", "%s vs %s — %s" % (
            os.path.basename(pa), os.path.basename(pb), summary.text()))
        document.compare_db = db_b
        document.compare_path = pb

    def _on_csv():
        flat = diff_engine.flatten_rows(last["rows"])
        path = plugin_shell.export_csv(
            shell, ["kind", "id", "name", "detail"], flat, "dbc_diff.csv")
        if path:
            log_fn("Compare", "CSV %s" % path)

    def _on_html():
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export HTML", "dbc_diff.html", "HTML (*.html)")
        if not path:
            return
        diff_engine.export_html(
            path, last["a"], last["b"], last["rows"], last["stats"])
        log_fn("Compare", "HTML %s" % path)

    use_doc_btn.clicked.connect(lambda: path_a.setText(""))
    pick_a.clicked.connect(lambda: _load_side(path_a, "Compare file A"))
    pick_b.clicked.connect(lambda: _load_side(path_b, "Compare file B"))
    ws_b.clicked.connect(lambda: (
        (lambda p: path_b.setText(p) if p else None)(
            dbc_picker.pick_dbc(shell, "Workspace DBC B"))))
    run_btn.clicked.connect(_on_run)
    export_csv.clicked.connect(_on_csv)
    export_html.clicked.connect(_on_html)

    if document.path:
        path_a.setPlaceholderText("empty = current: %s" % document.path)

    return root
