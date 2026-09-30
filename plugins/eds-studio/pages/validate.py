# -*- coding: utf-8 -*-
"""Validate — CiA 306 consistency; double-click jumps to Editor."""

from __future__ import annotations

import json

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QFileDialog,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import edsparse, plugin_shell, suite_chrome
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    run_btn = _ui.primary_btn(
        "Validate", "Run deep check + profile coverage (CiA)", "validate")
    profile_box = _ui.combo(
        ["Auto-detect profile", "CiA 301", "CiA 401", "CiA 402",
         "CiA 404", "CiA 406", "CiA 418", "CiA 419", "None"],
        "Force a device profile coverage check (or auto from 0x1000)")
    filt = _ui.line_edit("Filter findings", "Search findings…")
    filt.setMaximumWidth(160)
    summary = _ui.count_label()
    summary.setText("Ready")
    export_csv_btn = _ui.ghost_btn("CSV", "Export findings as CSV", "export")
    export_json_btn = _ui.ghost_btn("JSON", "Export findings as JSON", "export")
    export_sarif_btn = _ui.ghost_btn(
        "SARIF", "Export SARIF for CI pipelines", "export")
    goto_ed = _ui.ghost_btn(
        "Dictionary", "Open Dictionary on focused finding", "edit")
    crow.addWidget(run_btn)
    crow.addWidget(profile_box)
    crow.addWidget(filt)
    crow.addWidget(summary)
    crow.addStretch(1)
    crow.addWidget(goto_ed)
    crow.addWidget(export_csv_btn)
    crow.addWidget(export_json_btn)
    crow.addWidget(export_sarif_btn)
    layout.addWidget(chrome)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Severity", "Rule", "Index", "Message"])
    _ui.style_tree(tree, stretch_col=3)
    tree.setRootIsDecorated(False)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.setToolTip("Double-click a finding to open it in Editor")
    layout.addWidget(tree, 1)

    findings_cache = []

    def _paint_findings(*, log_result: bool = False):
        tree.clear()
        colors = {
            "error": QColor("#C62828"),
            "warning": QColor("#EF6C00"),
            "warn": QColor("#EF6C00"),
            "info": QColor("#1565C0"),
        }
        q = (filt.text() or "").strip().lower()
        n_err = n_warn = n_info = 0
        shown = 0
        total_err = sum(
            1 for f in findings_cache
            if (f.get("severity") or f.get("level")) == "error")
        total_warn = sum(
            1 for f in findings_cache
            if (f.get("severity") or f.get("level")) in ("warning", "warn"))
        for f in findings_cache:
            sev = f.get("severity") or f.get("level") or "info"
            blob = " ".join(str(f.get(k, "") or "") for k in (
                "severity", "level", "rule", "index", "message")).lower()
            if q and q not in blob:
                continue
            shown += 1
            if sev == "error":
                n_err += 1
            elif sev in ("warning", "warn"):
                n_warn += 1
            else:
                n_info += 1
            item = QTreeWidgetItem([
                sev, f.get("rule", ""), f.get("index", ""),
                f.get("message", ""),
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, f)
            c = colors.get(sev, QColor("#546E7A"))
            for col in range(4):
                item.setForeground(col, c)
            tree.addTopLevelItem(item)
        summary.setText(
            "%d shown · %d error · %d warn · %d info" % (
                shown, n_err, n_warn, n_info))
        document.mark_validated(total_err == 0, error_count=total_err)
        if log_result:
            log_fn("OK" if total_err == 0 else "WARN",
                   "Validate: %d errors, %d warnings" % (
                       total_err, total_warn))

    def run_lint():
        nonlocal findings_cache
        sel = profile_box.currentText()
        profile_id = None
        if sel.startswith("CiA "):
            profile_id = sel.split()[-1]
        elif sel == "None":
            profile_id = ""  # skip profile coverage
        if profile_id == "":
            findings_cache = edsparse.validate_eds(
                document.eds.entries,
                file_info=document.eds.file_info,
                device_info=document.eds.device_info,
                device_commissioning=document.eds.device_commissioning or None,
                deep=True,
            )
        else:
            findings_cache = edsparse.validate_document(
                document.eds, deep=True,
                profile_id=profile_id if profile_id else None)
        _paint_findings(log_result=True)

    def _goto(item, _col):
        f = item.data(0, Qt.ItemDataRole.UserRole) or {}
        od_index = f.get("od_index")
        if od_index is None:
            return
        sub = f.get("od_sub") or 0
        document.set_focus(od_index, sub)
        if hasattr(shell, "goto_editor_target"):
            shell.goto_editor_target(od_index, sub)
        else:
            shell.goto_editor_object(od_index, sub)

    def _goto_focus():
        item = tree.currentItem()
        if item is not None:
            _goto(item, 0)
            return
        if document.focus_index is not None and hasattr(
                shell, "goto_editor_target"):
            shell.goto_editor_target(
                document.focus_index, document.focus_subindex)

    def _export_csv():
        rows = [
            [f.get("severity") or f.get("level", ""),
             f.get("rule", ""), f.get("index", ""), f.get("message", "")]
            for f in findings_cache
        ]
        path = plugin_shell.export_csv(
            shell, ["Severity", "Rule", "Index", "Message"],
            rows, "eds_validate.csv")
        if path:
            log_fn("OK", "Exported %s" % path)

    def _export_json():
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export JSON", "eds_validate.json", "JSON (*.json)")
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            json.dump(findings_cache, f, indent=2)
        log_fn("OK", "Exported %s" % path)

    def _export_sarif():
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export SARIF", "eds_validate.sarif.json",
            "SARIF (*.json)")
        if not path:
            return
        results = []
        for f in findings_cache:
            sev = f.get("severity") or f.get("level") or "note"
            level = {"error": "error", "warning": "warning",
                     "warn": "warning"}.get(sev, "note")
            results.append({
                "ruleId": f.get("rule") or "eds",
                "level": level,
                "message": {"text": f.get("message", "")},
                "locations": [{
                    "physicalLocation": {
                        "artifactLocation": {
                            "uri": document.path or "unsaved.eds"},
                        "region": {"message": {"text": f.get("index", "")}},
                    }
                }],
            })
        sarif = {
            "version": "2.1.0",
            "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
            "runs": [{
                "tool": {"driver": {
                    "name": "EDS Studio Validate", "version": "1.0.0"}},
                "results": results,
            }],
        }
        with open(path, "w", encoding="utf-8") as f:
            json.dump(sarif, f, indent=2)
        log_fn("OK", "Exported SARIF %s" % path)

    run_btn.clicked.connect(run_lint)
    filt.textChanged.connect(lambda *_: _paint_findings())
    goto_ed.clicked.connect(_goto_focus)
    tree.itemDoubleClicked.connect(_goto)
    export_csv_btn.clicked.connect(_export_csv)
    export_json_btn.clicked.connect(_export_json)
    export_sarif_btn.clicked.connect(_export_sarif)

    root.run_lint = run_lint
    return root
