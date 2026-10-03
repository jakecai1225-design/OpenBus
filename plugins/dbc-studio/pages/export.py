# -*- coding: utf-8 -*-
"""Export workspace — matrix + C codegen + open-after-export."""

from __future__ import annotations

import os

from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QTabWidget,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from pages import _ui
from _shared import codicons, plugin_shell, state_store, suite_chrome, vscode_theme
from core import codegen, export_matrix

PLUGIN_ID = "dbc-studio"


def _open_path(path: str) -> None:
    if not path or not os.path.isfile(path):
        return
    try:
        os.startfile(path)  # Windows
    except Exception:
        try:
            import subprocess
            subprocess.Popen(["xdg-open", path])  # noqa: S603
        except Exception:
            pass


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    tabs = QTabWidget()
    tabs.setObjectName("SuiteEditorTabs")
    tabs.setDocumentMode(True)
    layout.addWidget(tabs, 1)

    # ---- Matrix tab ----
    matrix = QWidget()
    ml = QVBoxLayout(matrix)
    suite_chrome.page_margins(ml)


    of = QFormLayout()
    vscode_theme.tune_form(of)
    fmt = QComboBox()
    fmt.addItems(["CSV", "JSON", "HTML"])
    fmt.setFixedHeight(_ui.CTRL_H)
    opt_minmax = QCheckBox("Include min / max")
    opt_minmax.setChecked(True)
    opt_nodes = QCheckBox("Include receivers")
    opt_nodes.setChecked(True)
    opt_values = QCheckBox("Include value tables")
    opt_comment = QCheckBox("Include comments")
    open_after = QCheckBox("Open file after export")
    open_after.setChecked(True)
    open_after.setToolTip("Launch the exported file with the system default app")
    of.addRow(vscode_theme.field_label("Format"), fmt)
    of.addRow(opt_minmax)
    of.addRow(opt_nodes)
    of.addRow(opt_values)
    of.addRow(opt_comment)
    of.addRow(open_after)
    vscode_theme.polish_form_labels(of)
    ml.addLayout(of)

    mrow = QHBoxLayout()
    mrow.addStretch(1)
    export_btn = QPushButton("Export matrix")
    export_btn.setFixedHeight(_ui.CTRL_H)
    codicons.set_button(export_btn, "export", primary=True)
    mrow.addWidget(export_btn)
    ml.addLayout(mrow)
    ml.addStretch(1)
    tabs.addTab(matrix, "Matrix")

    # ---- C codegen tab ----
    code = QWidget()
    cl = QVBoxLayout(code)
    suite_chrome.page_margins(cl)

    crow = QHBoxLayout()
    crow.setSpacing(8)
    crow.addWidget(QLabel("Identifier style"))
    style = QComboBox()
    style.addItems(["keep", "upper"])
    style.setFixedHeight(_ui.CTRL_H)
    crow.addWidget(style)
    gen_btn = QPushButton("Generate")
    gen_btn.setFixedHeight(_ui.CTRL_H)
    codicons.set_button(gen_btn, "apply", primary=True)
    crow.addWidget(gen_btn)
    crow.addStretch(1)
    open_code = QCheckBox("Open after save")
    open_code.setChecked(True)
    crow.addWidget(open_code)
    save_h = QPushButton("Save .h")
    save_h.setObjectName("GhostButton")
    save_h.setFixedHeight(_ui.CTRL_H)
    codicons.set_button(save_h, "save")
    save_c = QPushButton("Save .c")
    save_c.setObjectName("GhostButton")
    save_c.setFixedHeight(_ui.CTRL_H)
    codicons.set_button(save_c, "save")
    crow.addWidget(save_h)
    crow.addWidget(save_c)
    cl.addLayout(crow)

    preview = QTextEdit()
    preview.setObjectName("SuiteCode")
    preview.setReadOnly(True)
    preview.setPlaceholderText("Generated C pack / unpack preview")
    cl.addWidget(preview, 1)
    tabs.addTab(code, "C Codegen")

    generated = {"h": "", "c": ""}

    def _opts():
        return {
            "minmax": opt_minmax.isChecked(),
            "nodes": opt_nodes.isChecked(),
            "values": opt_values.isChecked(),
            "comment": opt_comment.isChecked(),
        }

    def _on_matrix():
        opts = _opts()
        kind = fmt.currentText().lower()
        path = None
        if kind == "csv":
            headers, rows = export_matrix.build_matrix_rows(document.db, opts)
            path = plugin_shell.export_csv(
                shell, headers, rows, "dbc_matrix.csv")
            if path:
                log_fn("Export", "Matrix CSV %s" % path)
        elif kind == "json":
            path, _ = QFileDialog.getSaveFileName(
                shell, "Export JSON", "dbc_matrix.json", "JSON (*.json)")
            if not path:
                return
            data = export_matrix.build_json(document.db, opts)
            import json
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
            log_fn("Export", "Matrix JSON %s" % path)
        else:
            path, _ = QFileDialog.getSaveFileName(
                shell, "Export HTML", "dbc_matrix.html", "HTML (*.html)")
            if not path:
                return
            export_matrix.write_html(path, document.db, opts)
            log_fn("Export", "Matrix HTML %s" % path)
        state_store.save_state(PLUGIN_ID, {
            "export_format": fmt.currentText(),
            "export_opts": opts,
            "open_after": open_after.isChecked(),
            "open_code": open_code.isChecked(),
        }, "export.json")
        plugin_shell.set_status(shell, "Matrix exported", 2500)
        if path and open_after.isChecked():
            _open_path(path)

    def _on_gen():
        h, c = codegen.generate_c(document.db, style.currentText())
        generated["h"], generated["c"] = h, c
        preview.setPlainText(
            "// ---- header ----\n" + h + "\n// ---- source ----\n" + c)
        log_fn("Export", "Generated C for %d messages" % len(
            document.db.messages))
        plugin_shell.set_status(shell, "C codegen ready", 2500)

    def _save(which: str):
        if not generated[which]:
            _on_gen()
        default = "dbc_codec.h" if which == "h" else "dbc_codec.c"
        path, _ = QFileDialog.getSaveFileName(
            shell, "Save", default,
            "C Header (*.h)" if which == "h" else "C Source (*.c)")
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            f.write(generated[which])
        log_fn("Export", "Wrote %s" % path)
        if open_code.isChecked():
            _open_path(path)

    export_btn.clicked.connect(_on_matrix)
    gen_btn.clicked.connect(_on_gen)
    save_h.clicked.connect(lambda: _save("h"))
    save_c.clicked.connect(lambda: _save("c"))

    saved = state_store.load_state(PLUGIN_ID, "export.json") or {}
    if saved.get("export_format") in ("CSV", "JSON", "HTML"):
        fmt.setCurrentText(saved["export_format"])
    opts = saved.get("export_opts") or {}
    opt_minmax.setChecked(bool(opts.get("minmax", True)))
    opt_nodes.setChecked(bool(opts.get("nodes", True)))
    opt_values.setChecked(bool(opts.get("values", False)))
    opt_comment.setChecked(bool(opts.get("comment", False)))
    if "open_after" in saved:
        open_after.setChecked(bool(saved["open_after"]))
    if "open_code" in saved:
        open_code.setChecked(bool(saved["open_code"]))

    return root
