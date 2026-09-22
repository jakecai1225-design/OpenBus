# -*- coding: utf-8 -*-
"""Merge workspace — multi-DBC merge into current document."""

from __future__ import annotations

import copy
import os

from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QFileDialog,
    QHeaderView,
    QLabel,
    QListWidget,
    QMessageBox,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, dbc_picker, dbcparse, plugin_shell, state_store, suite_chrome
from core import merge_engine

PLUGIN_ID = "dbc-studio"


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    add_btn = QPushButton("Add file")
    add_btn.setObjectName("GhostButton")
    add_btn.setFixedHeight(28)
    codicons.set_button(add_btn, "add")
    add_ws = QPushButton("Workspace")
    add_ws.setObjectName("GhostButton")
    add_ws.setFixedHeight(28)
    codicons.set_button(add_ws, "database")
    clear_btn = QPushButton("Clear")
    clear_btn.setObjectName("GhostButton")
    clear_btn.setFixedHeight(28)
    codicons.set_button(clear_btn, "clear")
    crow.addWidget(add_btn)
    crow.addWidget(add_ws)
    crow.addWidget(clear_btn)
    crow.addWidget(QLabel("Policy"))
    policy = QComboBox()
    policy.addItems(["Skip (keep first)", "Rename later", "Prefer A (keep first)"])
    policy.setFixedHeight(28)
    policy.setMinimumWidth(160)
    crow.addWidget(policy)
    crow.addStretch(1)
    run_btn = QPushButton("Merge")
    run_btn.setFixedHeight(28)
    codicons.set_button(run_btn, "merge", primary=True)
    apply_btn = QPushButton("Apply to Editor")
    apply_btn.setFixedHeight(28)
    codicons.set_button(apply_btn, "apply", primary=True)
    export_btn = QPushButton("Conflicts CSV")
    export_btn.setObjectName("GhostButton")
    export_btn.setFixedHeight(28)
    codicons.set_button(export_btn, "export")
    crow.addWidget(run_btn)
    crow.addWidget(apply_btn)
    crow.addWidget(export_btn)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl)
    bl.setSpacing(10)

    hint = QLabel(
        "Merge additional DBC files into a working copy, then Apply to replace "
        "the open document.")
    hint.setWordWrap(True)
    hint.setStyleSheet("color:#78909c;font-size:12px;")
    bl.addWidget(hint)

    files_head = QLabel("Input files")
    files_head.setObjectName("SuiteSectionTitle")
    bl.addWidget(files_head)
    files = QListWidget()
    files.setAlternatingRowColors(True)
    files.setMaximumHeight(100)
    files.setStyleSheet(
        "QListWidget { border: 1px solid #EEEEEE; }"
    )
    bl.addWidget(files)

    summary = QLabel("Add one or more DBC files to merge")
    summary.setStyleSheet("color:#90A4AE;font-size:11px;")
    bl.addWidget(summary)

    tree = QTreeWidget()
    tree.setObjectName("SuiteMatrix")
    tree.setHeaderLabels(["CAN ID", "Kept", "Other", "Source", "Action"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    tree.setStyleSheet(
        "QTreeWidget#SuiteMatrix { border: 1px solid #EEEEEE; }"
        "QTreeWidget#SuiteMatrix::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
    )
    bl.addWidget(tree, 1)
    layout.addWidget(body, 1)

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
            "Merged %d file(s) → %d messages · %d conflicts"
            % (len(paths), len(base.messages), len(conflicts)))
        log_fn("Merge", summary.text())
        plugin_shell.set_status(shell, summary.text(), 4000)
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
            "Replace current document with merge result?\n"
            "Unsaved edits will be lost.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if ans != QMessageBox.StandardButton.Yes:
            return
        document.apply_db(state["merged"])
        log_fn("Merge", "Applied merge to Editor (%d messages)" % len(
            document.db.messages))
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
