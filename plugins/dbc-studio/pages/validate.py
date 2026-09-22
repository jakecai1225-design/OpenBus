# -*- coding: utf-8 -*-
"""Validate workspace — lint current document."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QScrollArea,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, plugin_shell, suite_chrome
from core import lint_engine


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    run_btn = QPushButton("Run lint")
    run_btn.setFixedHeight(28)
    codicons.set_button(run_btn, "validate", primary=True)
    save_rules_btn = QPushButton("Save rules")
    save_rules_btn.setObjectName("GhostButton")
    save_rules_btn.setFixedHeight(28)
    codicons.set_button(save_rules_btn, "save")
    crow.addWidget(run_btn)
    crow.addWidget(save_rules_btn)
    crow.addStretch(1)
    for text, tip, slot_name in (
        ("CSV", "Export findings as CSV", "csv"),
        ("JSON", "Export findings as JSON", "json"),
        ("SARIF", "Export SARIF for CI", "sarif"),
    ):
        b = QPushButton(text)
        b.setObjectName("GhostButton")
        b.setFixedHeight(28)
        b.setToolTip(tip)
        codicons.set_button(b, "export")
        crow.addWidget(b)
        if slot_name == "csv":
            export_csv_btn = b
        elif slot_name == "json":
            export_json_btn = b
        else:
            export_sarif_btn = b
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl)
    bl.setSpacing(10)

    hint = QLabel(
        "Consistency check — double-click a finding to jump to Editor "
        "(same job as CANdb++, with CSV / JSON / SARIF for CI).")
    hint.setWordWrap(True)
    hint.setStyleSheet("color:#78909c;font-size:12px;")
    bl.addWidget(hint)

    rules = lint_engine.load_rules()
    rule_checks = {}
    rules_head = QLabel("Rules")
    rules_head.setObjectName("SuiteSectionTitle")
    bl.addWidget(rules_head)
    scroll = QScrollArea()
    scroll.setWidgetResizable(True)
    scroll.setMaximumHeight(120)
    scroll.setFrameShape(QFrame.Shape.NoFrame)
    inner = QWidget()
    inner_l = QVBoxLayout(inner)
    inner_l.setContentsMargins(0, 0, 0, 0)
    inner_l.setSpacing(4)
    for rid, cfg in rules.items():
        cb = QCheckBox("%s — %s" % (rid, cfg.get("title", rid)))
        cb.setChecked(bool(cfg.get("enabled", True)))
        rule_checks[rid] = cb
        inner_l.addWidget(cb)
    inner_l.addStretch()
    scroll.setWidget(inner)
    bl.addWidget(scroll)

    summary = QLabel("Run lint on the current document")
    summary.setStyleSheet("color:#90A4AE;font-size:11px;")
    bl.addWidget(summary)

    tree = QTreeWidget()
    tree.setObjectName("SuiteMatrix")
    tree.setHeaderLabels(["Severity", "Rule", "Location", "Message"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    tree.setStyleSheet(
        "QTreeWidget#SuiteMatrix { border: 1px solid #EEEEEE; }"
        "QTreeWidget#SuiteMatrix::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
    )
    bl.addWidget(tree, 1)
    layout.addWidget(body, 1)

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
                sev, f.get("rule", ""), f.get("location", ""),
                f.get("message", ""),
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, f)
            item.setForeground(0, colors.get(sev, QColor("#333")))
            tree.addTopLevelItem(item)
        summary.setText(
            "%d findings · %d errors · %d warnings · %d info"
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

    root.run_lint = _on_run
    return root
