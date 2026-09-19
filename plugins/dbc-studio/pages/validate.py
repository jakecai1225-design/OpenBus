# -*- coding: utf-8 -*-
"""Validate workspace — lint current document."""

from __future__ import annotations

import json
import os

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QCheckBox, QFileDialog, QHBoxLayout, QHeaderView, QLabel,
    QPushButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
    QAbstractItemView, QScrollArea, QFrame,
)

from _shared import plugin_shell, vscode_theme
from core import lint_engine


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    rules = lint_engine.load_rules()
    rule_checks = {}

    rules_card, rules_lay = vscode_theme.block(
        "Rules", "Toggle lint rules, then Run lint on the open document.")
    scroll = QScrollArea()
    scroll.setWidgetResizable(True)
    scroll.setMaximumHeight(140)
    scroll.setFrameShape(QFrame.Shape.NoFrame)
    inner = QWidget()
    inner_l = QVBoxLayout(inner)
    for rid, cfg in rules.items():
        cb = QCheckBox("%s — %s" % (rid, cfg.get("title", rid)))
        cb.setChecked(bool(cfg.get("enabled", True)))
        rule_checks[rid] = cb
        inner_l.addWidget(cb)
    inner_l.addStretch()
    scroll.setWidget(inner)
    rules_lay.addWidget(scroll)
    layout.addWidget(rules_card)

    btns = QHBoxLayout()
    run_btn = QPushButton("Run lint")
    save_rules_btn = QPushButton("Save rule toggles")
    export_csv_btn = QPushButton("Export CSV")
    export_json_btn = QPushButton("Export JSON")
    export_sarif_btn = QPushButton("Export SARIF")
    btns.addWidget(run_btn)
    btns.addWidget(save_rules_btn)
    btns.addStretch()
    btns.addWidget(export_csv_btn)
    btns.addWidget(export_json_btn)
    btns.addWidget(export_sarif_btn)
    layout.addLayout(btns)

    summary = QLabel("Run lint on the current document")
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Severity", "Rule", "Location", "Message"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    findings_cache = []

    def _current_rules():
        out = lint_engine.load_rules()
        for rid, cb in rule_checks.items():
            if rid in out:
                out[rid]["enabled"] = cb.isChecked()
        return out

    def _on_run():
        nonlocal findings_cache
        r = _current_rules()
        findings_cache = lint_engine.lint_dbc(document.db, r)
        tree.clear()
        colors = {
            "error": QColor("#C62828"),
            "warning": QColor("#EF6C00"),
            "info": QColor("#1565C0"),
        }
        n_err = n_warn = n_info = 0
        for f in findings_cache:
            sev = f.get("severity", "info")
            if sev == "error":
                n_err += 1
            elif sev == "warning":
                n_warn += 1
            else:
                n_info += 1
            item = QTreeWidgetItem([
                sev, f.get("rule", ""), f.get("location", ""), f.get("message", ""),
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, f)
            item.setForeground(0, colors.get(sev, QColor("#333")))
            tree.addTopLevelItem(item)
        summary.setText(
            "Findings: %d  (errors=%d warnings=%d info=%d)"
            % (len(findings_cache), n_err, n_warn, n_info))
        log_fn("Validate", "Lint: %d findings" % len(findings_cache))
        plugin_shell.set_status(shell, summary.text(), 4000)

    def _on_save_rules():
        path = lint_engine.save_rules(_current_rules())
        log_fn("Validate", "Rules saved")
        plugin_shell.set_status(shell, "Rules saved (%s)" % path, 3000)

    def _on_dbl(_item, _col):
        item = tree.currentItem()
        if not item:
            return
        f = item.data(0, Qt.ItemDataRole.UserRole) or {}
        shell.goto_editor_target(f.get("can_id"), f.get("signal"))

    def _export_csv():
        rows = [
            [f.get("severity", ""), f.get("rule", ""),
             f.get("location", ""), f.get("message", "")]
            for f in findings_cache
        ]
        path = plugin_shell.export_csv(
            shell, ["severity", "rule", "location", "message"], rows,
            "dbc_lint.csv")
        if path:
            log_fn("Validate", "CSV %s" % path)

    def _export_json():
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export JSON", "dbc_lint.json", "JSON (*.json)")
        if not path:
            return
        lint_engine.write_json_report(
            path, findings_cache, document.path,
            len(document.db.messages))
        log_fn("Validate", "JSON %s" % path)

    def _export_sarif():
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export SARIF", "dbc_lint.sarif", "SARIF (*.sarif *.json)")
        if not path:
            return
        lint_engine.write_sarif_report(path, findings_cache, document.path)
        log_fn("Validate", "SARIF %s" % path)

    run_btn.clicked.connect(_on_run)
    save_rules_btn.clicked.connect(_on_save_rules)
    export_csv_btn.clicked.connect(_export_csv)
    export_json_btn.clicked.connect(_export_json)
    export_sarif_btn.clicked.connect(_export_sarif)
    tree.itemDoubleClicked.connect(_on_dbl)
    document.on_changed(lambda: summary.setText(
        "Document changed — re-run lint (%s)" % document.display_name()))

    return root
