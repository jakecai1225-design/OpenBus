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
    QHeaderView,
    QLabel,
    QScrollArea,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core import lint_engine
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    run_btn = _ui.primary_btn(
        "Run lint",
        "Consistency check — double-click a finding to jump to Messages",
        "validate")
    save_rules_btn = _ui.ghost_btn("Save rules", "Persist rule enable flags", "save")
    export_csv_btn = _ui.ghost_btn("CSV", "Export findings as CSV", "export")
    export_json_btn = _ui.ghost_btn("JSON", "Export findings as JSON", "export")
    export_sarif_btn = _ui.ghost_btn("SARIF", "Export SARIF for CI", "export")
    layout.addWidget(_ui.tool_strip(
        run_btn, save_rules_btn,
        export_csv_btn, export_json_btn, export_sarif_btn, stretch_at=2))

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    bl.setContentsMargins(0, 0, 0, 0)
    bl.setSpacing(0)

    rules = lint_engine.load_rules()
    rule_checks = {}
    bl.addWidget(_ui.panel_header("Rules"))
    scroll = QScrollArea()
    scroll.setWidgetResizable(True)
    scroll.setMaximumHeight(120)
    scroll.setFrameShape(QFrame.Shape.NoFrame)
    inner = QWidget()
    inner_l = QVBoxLayout(inner)
    inner_l.setContentsMargins(_ui.PAD_X, 4, _ui.PAD_X, 4)
    inner_l.setSpacing(4)
    for rid, cfg in rules.items():
        cb = QCheckBox("%s — %s" % (rid, cfg.get("title", rid)))
        cb.setChecked(bool(cfg.get("enabled", True)))
        cb.setToolTip(cfg.get("description", ""))
        rule_checks[rid] = cb
        inner_l.addWidget(cb)
    inner_l.addStretch()
    scroll.setWidget(inner)
    bl.addWidget(scroll)

    summary = QLabel("Run lint on the current document")
    summary.setObjectName("SuiteCount")
    summary.setContentsMargins(_ui.PAD_X, 2, _ui.PAD_X, 2)
    bl.addWidget(summary)

    save_next = _ui.primary_btn(
        "Save", "Save the DBC after a clean lint", "save")
    export_next = _ui.ghost_btn(
        "Export…", "Open Export / codegen", "export")
    next_bar = _ui.next_step_bar(
        "Lint clean — next:", save_next, export_next)
    next_bar.setVisible(False)
    bl.addWidget(next_bar)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Severity", "Rule", "Location", "Message"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.setToolTip("Double-click a finding to open Messages")
    _ui.style_tree(tree, header_hidden=False)
    _ui.configure_columns(tree, stretch=3)
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
        document.mark_validated(n_err == 0, error_count=n_err)
        next_bar.setVisible(n_err == 0)
        if hasattr(shell, "_sync_next_hint"):
            shell._sync_next_hint()
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
    save_next.clicked.connect(
        lambda: shell.run_action("dbc.save")
        if hasattr(shell, "run_action") else None)
    export_next.clicked.connect(
        lambda: shell.run_action("view.export")
        if hasattr(shell, "run_action") else shell.goto_page("export"))
    document.on_changed(lambda: summary.setText(
        "Document changed — re-run lint (%s)" % document.display_name()))

    root.run_lint = _on_run  # type: ignore[attr-defined]
    return root
