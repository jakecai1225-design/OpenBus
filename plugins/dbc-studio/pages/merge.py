# -*- coding: utf-8 -*-
"""Merge workspace — multi-DBC merge into current document."""

from __future__ import annotations

import copy
import os

from PyQt6.QtWidgets import (
    QComboBox, QFileDialog, QHBoxLayout, QLabel, QListWidget,
    QMessageBox, QPushButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout,
    QWidget, QHeaderView, QAbstractItemView,
)

from _shared import dbcparse, dbc_picker, plugin_shell, state_store
from core import merge_engine

PLUGIN_ID = "dbc-studio"


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)

    layout.addWidget(QLabel(
        "Merge additional DBC files into a working copy. "
        "Apply result replaces the current document."))

    row = QHBoxLayout()
    add_btn = QPushButton("Add file…")
    add_ws = QPushButton("Add workspace…")
    clear_btn = QPushButton("Clear list")
    policy = QComboBox()
    policy.addItems(["Skip (keep first)", "Rename later", "Prefer A (keep first)"])
    run_btn = QPushButton("Merge")
    apply_btn = QPushButton("Apply to Editor")
    export_btn = QPushButton("Conflict CSV")
    for w in (add_btn, add_ws, clear_btn):
        row.addWidget(w)
    row.addWidget(QLabel("Policy:"))
    row.addWidget(policy)
    row.addStretch()
    row.addWidget(run_btn)
    row.addWidget(apply_btn)
    row.addWidget(export_btn)
    layout.addLayout(row)

    files = QListWidget()
    layout.addWidget(files)

    summary = QLabel("Add one or more DBC files to merge")
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["CAN ID", "Kept", "Other", "Source", "Action"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    state = {"merged": None, "conflicts": []}

    def _add_path(path: str):
        if not path:
            return
        for i in range(files.count()):
            if files.item(i).text() == path:
                return
        files.addItem(path)

    def _on_merge():
        paths = [files.item(i).text() for i in range(files.count())]
        if not paths:
            summary.setText("Add files first")
            return
        base = copy.deepcopy(document.db)
        conflicts = []
        pol = policy.currentIndex()
        for p in paths:
            try:
                incoming = dbcparse.parse_file(p)
            except OSError as e:
                log_fn("ERR", "Merge open failed %s: %s" % (p, e))
                continue
            merge_engine.merge_dbc(
                base, incoming, os.path.basename(p), pol, conflicts)
            for n in incoming.nodes:
                if n not in base.nodes:
                    base.nodes.append(n)
        state["merged"] = base
        state["conflicts"] = conflicts
        tree.clear()
        for cid, kept, other, src, action in conflicts:
            tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid, kept, other, src, action,
            ]))
        summary.setText(
            "Merged %d file(s) → %d messages, %d conflicts"
            % (len(paths), len(base.messages), len(conflicts)))
        log_fn("Merge", summary.text())
        state_store.save_state(PLUGIN_ID, {
            "merge_policy": pol,
            "merge_inputs": paths,
        }, "merge.json")

    def _on_apply():
        if state["merged"] is None:
            QMessageBox.information(shell, "Merge", "Run Merge first")
            return
        ans = QMessageBox.question(
            shell, "Apply merge",
            "Replace current document with merge result?\nUnsaved edits will be lost.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if ans != QMessageBox.StandardButton.Yes:
            return
        document.set_db(state["merged"], path=document.path, dirty=True)
        log_fn("Merge", "Applied merge to Editor (%d messages)" % len(document.db.messages))
        shell.goto_page("editor")

    def _on_csv():
        rows = [
            ["0x%X" % c[0], c[1], c[2], c[3], c[4]]
            for c in state["conflicts"]
        ]
        path = plugin_shell.export_csv(
            shell, ["can_id", "kept", "other", "source", "action"],
            rows, "dbc_merge_conflicts.csv")
        if path:
            log_fn("Merge", "CSV %s" % path)

    add_btn.clicked.connect(lambda: _add_path(
        QFileDialog.getOpenFileName(
            shell, "Add DBC", "", "DBC (*.dbc)")[0]))
    add_ws.clicked.connect(lambda: _add_path(
        dbc_picker.pick_dbc(shell, "Workspace DBC") or ""))
    clear_btn.clicked.connect(files.clear)
    run_btn.clicked.connect(_on_merge)
    apply_btn.clicked.connect(_on_apply)
    export_btn.clicked.connect(_on_csv)

    saved = state_store.load_state(PLUGIN_ID, "merge.json") or {}
    if isinstance(saved.get("merge_policy"), int):
        policy.setCurrentIndex(max(0, min(2, saved["merge_policy"])))
    for p in saved.get("merge_inputs") or []:
        if os.path.isfile(p):
            _add_path(p)

    return root
