# -*- coding: utf-8 -*-
"""Export workspace — matrix + C codegen."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QCheckBox, QComboBox, QFileDialog, QFormLayout,
    QHBoxLayout, QLabel, QPushButton, QTabWidget, QTextEdit,
    QVBoxLayout, QWidget,
)

from _shared import plugin_shell, state_store, vscode_theme
from core import codegen, export_matrix

PLUGIN_ID = "dbc-studio"


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)
    tabs = QTabWidget()
    layout.addWidget(tabs)

    matrix = QWidget()
    ml = QVBoxLayout(matrix)
    ml.setContentsMargins(8, 8, 8, 8)
    opts_card, of_host = vscode_theme.block(
        "Matrix options", "Export a message/signal matrix for review or tooling.")
    of = QFormLayout()
    vscode_theme.tune_form(of)
    fmt = QComboBox()
    fmt.addItems(["CSV", "JSON", "HTML"])
    opt_minmax = QCheckBox("Include min/max")
    opt_minmax.setChecked(True)
    opt_nodes = QCheckBox("Include receivers")
    opt_nodes.setChecked(True)
    opt_values = QCheckBox("Include value tables")
    opt_comment = QCheckBox("Include comments")
    of.addRow("Format:", fmt)
    of.addRow(opt_minmax)
    of.addRow(opt_nodes)
    of.addRow(opt_values)
    of.addRow(opt_comment)
    of_host.addLayout(of)
    ml.addWidget(opts_card)
    mrow = QHBoxLayout()
    export_btn = QPushButton("Export matrix…")
    mrow.addStretch()
    mrow.addWidget(export_btn)
    ml.addLayout(mrow)
    ml.addStretch()
    tabs.addTab(matrix, "Matrix")

    code = QWidget()
    cl = QVBoxLayout(code)
    style = QComboBox()
    style.addItems(["keep", "upper"])
    crow = QHBoxLayout()
    crow.addWidget(QLabel("Identifier style:"))
    crow.addWidget(style)
    gen_btn = QPushButton("Generate")
    save_h = QPushButton("Save .h…")
    save_c = QPushButton("Save .c…")
    crow.addWidget(gen_btn)
    crow.addStretch()
    crow.addWidget(save_h)
    crow.addWidget(save_c)
    cl.addLayout(crow)
    preview = QTextEdit()
    preview.setReadOnly(True)
    preview.setPlaceholderText("Generated C pack/unpack preview")
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
        }, "export.json")

    def _on_gen():
        h, c = codegen.generate_c(document.db, style.currentText())
        generated["h"], generated["c"] = h, c
        preview.setPlainText(
            "// ---- header ----\n" + h + "\n// ---- source ----\n" + c)
        log_fn("Export", "Generated C for %d messages" % len(document.db.messages))

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

    return root
