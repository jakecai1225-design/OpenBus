# -*- coding: utf-8 -*-
"""Report workspace — text summary of the shared offline corpus."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QFileDialog,
    QHBoxLayout,
    QPushButton,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, vscode_theme
from core.report import report_text


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "Report",
        "Snapshot of the logs loaded on Open. Export writes a plain-text snippet.")
    row = QHBoxLayout()
    refresh_btn = QPushButton("Refresh")
    export_btn = QPushButton("Export…")
    row.addWidget(refresh_btn)
    row.addWidget(export_btn)
    row.addStretch(1)
    body.addLayout(row)
    layout.addWidget(card)

    view = QTextEdit()
    view.setReadOnly(True)
    layout.addWidget(view, 1)

    def _fill():
        view.setPlainText(report_text(session.frames, session.frame_names))

    def _export():
        path, _ = QFileDialog.getSaveFileName(
            root, "Export report", "log_report.txt", "Text (*.txt)")
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            f.write(view.toPlainText())
        session.note_export_path(path)
        plugin_shell.set_status(parent, "Exported %s" % path, 3000)
        log_fn("RX", "-", b"", "Report exported %s" % path)

    refresh_btn.clicked.connect(_fill)
    export_btn.clicked.connect(_export)
    session.on_corpus_changed(_fill)
    _fill()
    return root
