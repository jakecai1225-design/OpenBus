# -*- coding: utf-8 -*-
"""Compare workspace — A/B DBC diff."""

from __future__ import annotations

import os

from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pages import _ui
from _shared import dbc_picker, dbcparse, plugin_shell
from core import diff_engine


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    path_a = QLineEdit()
    path_a.setPlaceholderText("A — empty uses current document")
    path_a.setFixedHeight(_ui.CTRL_H)
    path_a.setClearButtonEnabled(True)
    path_a.setToolTip("Diff two DBC files — leave A empty to use the open document")
    path_b = QLineEdit()
    path_b.setPlaceholderText("B — pick a DBC to compare")
    path_b.setFixedHeight(_ui.CTRL_H)
    path_b.setClearButtonEnabled(True)
    use_doc_btn = _ui.ghost_btn(
        "Use current", "Clear A so Compare uses the open document", "")
    pick_a = _ui.ghost_btn("Browse A", "Pick file A", "browse")
    pick_b = _ui.ghost_btn("Browse B", "Pick file B", "browse")
    ws_b = _ui.ghost_btn("Workspace B", "Pick B from workspace", "database")
    run_btn = _ui.primary_btn("Compare", "Run A/B diff", "compare")
    layout.addWidget(_ui.tool_strip(
        _ui.field_label("A"), path_a,
        _ui.field_label("B"), path_b,
        use_doc_btn, pick_a, pick_b, ws_b, run_btn, stretch_at=3))

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    bl.setContentsMargins(0, 0, 0, 0)
    bl.setSpacing(0)

    summary = QLabel("Select file B to compare")
    summary.setObjectName("SuiteCount")
    summary.setContentsMargins(_ui.PAD_X, 2, _ui.PAD_X, 2)
    export_csv = _ui.ghost_btn("CSV", "Export diff as CSV", "export")
    export_html = _ui.ghost_btn("HTML", "Export diff as HTML", "export")
    bl.addWidget(_ui.tool_strip(summary, export_csv, export_html, stretch_at=1))

    tree = QTreeWidget()
    tree.setHeaderLabels(["Kind", "ID", "Name", "Detail"])
    tree.setAlternatingRowColors(True)
    _ui.style_tree(tree, header_hidden=False)
    _ui.configure_columns(tree, stretch=3)
    bl.addWidget(tree, 1)
    layout.addWidget(body, 1)

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
            "msg +%d / −%d / ~%d   ·   sig +%d / −%d / ~%d"
            % (stats["msg_new"], stats["msg_del"], stats["msg_mod"],
               stats["sig_new"], stats["sig_del"], stats["sig_mod"]))
        log_fn("Compare", "%s vs %s — %s" % (
            os.path.basename(pa), os.path.basename(pb), summary.text()))
        document.compare_db = db_b
        document.compare_path = pb
        plugin_shell.set_status(shell, summary.text(), 4000)

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
