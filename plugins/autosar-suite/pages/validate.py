# -*- coding: utf-8 -*-
"""Validate — deep + cross-artifact checks + suggested fixes."""

from __future__ import annotations

import json
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

from _shared import arxmlparse, plugin_shell, suite_chrome
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    run_btn = _ui.primary_btn(
        "Validate", "COM + ECUC + SWC cross-artifact check", "validate")
    summary = _ui.quiet_label("Ready")
    fix_btn = _ui.ghost_btn(
        "Apply safe fixes",
        "Bump DLC + re-derive ECUC when missing links", "apply")
    recipe = QComboBox()
    recipe.addItem("Recipe pack: all safe", [
        "bump_dlc", "unique_can_ids", "name_empty_signals", "derive_ecuc"])
    recipe.addItem("Recipe: bump DLC only", ["bump_dlc"])
    recipe.addItem("Recipe: unique CAN IDs", ["unique_can_ids"])
    recipe.addItem("Recipe: re-derive ECUC", ["derive_ecuc"])
    recipe.setFixedHeight(26)
    recipe.setToolTip("DaVinci-style solving action pack")
    recipe_btn = _ui.ghost_btn("Run pack", "Apply selected recipe pack", "apply")
    out_btn = _ui.ghost_btn(
        "Write out/", "SARIF + HTML under project out/", "export")
    csv_btn = _ui.ghost_btn("CSV", "Export findings", "export")
    json_btn = _ui.ghost_btn("JSON", "Export JSON", "export")
    sarif_btn = _ui.ghost_btn("SARIF", "Export SARIF for CI", "export")
    crow.addWidget(run_btn)
    crow.addWidget(summary)
    crow.addStretch(1)
    crow.addWidget(fix_btn)
    crow.addWidget(recipe)
    crow.addWidget(recipe_btn)
    crow.addWidget(out_btn)
    crow.addWidget(csv_btn)
    crow.addWidget(json_btn)
    crow.addWidget(sarif_btn)
    layout.addWidget(chrome)

    tree = QTreeWidget()
    tree.setHeaderLabels([
        "Severity", "Artifact", "Rule", "Location", "Message", "Suggested fix"])
    _ui.style_tree(tree)
    tree.setRootIsDecorated(False)
    tree.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tree.header().setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
    tree.header().setSectionResizeMode(5, QHeaderView.ResizeMode.Stretch)
    tree.setToolTip("Double-click to open in Editor")
    layout.addWidget(tree, 1)

    cache = []

    def run_lint():
        nonlocal cache
        cache = document.validate_all()
        tree.clear()
        colors = {
            "error": QColor("#C62828"),
            "warning": QColor("#EF6C00"),
            "warn": QColor("#EF6C00"),
            "info": QColor("#1565C0"),
        }
        n_err = n_warn = 0
        for f in cache:
            sev = f.get("severity") or f.get("level") or "info"
            if sev == "error":
                n_err += 1
            elif sev in ("warning", "warn"):
                n_warn += 1
            item = QTreeWidgetItem([
                sev, f.get("artifact", "com"), f.get("rule", ""),
                f.get("location", ""), f.get("message", ""), f.get("fix", ""),
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, f)
            c = colors.get(sev, QColor("#546E7A"))
            for col in range(6):
                item.setForeground(col, c)
            tree.addTopLevelItem(item)
        summary.setText("%d error · %d warn" % (n_err, n_warn))
        log_fn("OK" if n_err == 0 else "WARN",
               "Validate: %d errors" % n_err)

    def _goto(item, _c):
        f = item.data(0, Qt.ItemDataRole.UserRole) or {}
        art = f.get("artifact") or "com"
        if art == "ecuc":
            document.set_editor_mode("ecuc")
            shell.goto_page("editor")
        else:
            shell.goto_editor_target(f.get("pdu", ""), f.get("signal", ""))

    def _safe_fixes():
        model = document.clone_model()
        changed = 0
        for pdu in model.ipdus:
            need = 0
            for sig in pdu.signals:
                if arxmlparse.normalize_endian(sig.endian) == arxmlparse.ENDIAN_INTEL:
                    need = max(need, sig.start_bit + sig.length)
            need_bytes = (need + 7) // 8
            if need_bytes > pdu.dlc:
                pdu.dlc = need_bytes
                changed += 1
        if changed:
            document.apply_model(model)
            log_fn("OK", "Adjusted DLC on %d PDU(s)" % changed)
        rules = {f.get("rule") for f in cache}
        if "ecuc_empty" in rules or "ecuc_missing_pdu" in rules:
            document.derive_ecuc()
            log_fn("OK", "Re-derived ECUC-lite")
            changed += 1
        if "swc_empty" in rules or "swc_signal_missing" in rules:
            document.derive_swc()
            log_fn("OK", "Re-derived SWC ports")
            changed += 1
        if changed:
            run_lint()
        else:
            log_fn("SYS", "No safe fixes needed")

    def _run_pack():
        recipes = recipe.currentData() or []
        log = document.apply_fix_pack(list(recipes))
        for line in log:
            log_fn("OK", line)
        run_lint()

    def _write_out():
        if not document.has_project():
            log_fn("WARN", "Open a project to write out/")
            return
        try:
            paths = document.write_out_reports(cache or document.validate_all())
            log_fn("OK", "Wrote %s" % paths.get("sarif", ""))
        except (OSError, ValueError) as e:
            log_fn("ERR", str(e))

    def _export_csv():
        rows = [[
            f.get("severity"), f.get("artifact"), f.get("rule"),
            f.get("location"), f.get("message"), f.get("fix"),
        ] for f in cache]
        path = plugin_shell.export_csv(
            shell,
            ["Severity", "Artifact", "Rule", "Location", "Message", "Fix"],
            rows, "arxml_validate.csv")
        if path:
            log_fn("OK", "Exported %s" % path)

    def _export_json():
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export JSON", "arxml_validate.json", "JSON (*.json)")
        if path:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(cache, f, indent=2)
            log_fn("OK", "Exported %s" % path)

    def _export_sarif():
        default = "arxml_validate.sarif.json"
        if document.has_project():
            default = os.path.join(
                document.project_root, "out", "findings.sarif")
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export SARIF", default, "JSON (*.json)")
        if not path:
            return
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(arxmlparse.findings_to_sarif(cache), f, indent=2)
        log_fn("OK", "Exported SARIF")

    run_btn.clicked.connect(run_lint)
    fix_btn.clicked.connect(_safe_fixes)
    recipe_btn.clicked.connect(_run_pack)
    out_btn.clicked.connect(_write_out)
    tree.itemDoubleClicked.connect(_goto)
    csv_btn.clicked.connect(_export_csv)
    json_btn.clicked.connect(_export_json)
    sarif_btn.clicked.connect(_export_sarif)
    root.run_lint = run_lint
    return root
