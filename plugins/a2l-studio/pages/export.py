# -*- coding: utf-8 -*-
"""Export — slim A2L / CSV symbol table."""

from __future__ import annotations

from PyQt6.QtWidgets import QFileDialog, QVBoxLayout, QWidget

from _shared import a2lparse, plugin_shell
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)

    csv_btn = _ui.primary_btn(
        "Export CSV…", "Symbol table CSV", "export")
    slim_btn = _ui.ghost_btn(
        "Export slim A2L…", "Rewrite a minimal ASAP2 document", "save")
    apply_btn = _ui.ghost_btn(
        "Apply → XCP", "Hand off to XCP Studio", "apply")
    tip = _ui.quiet_label(
        "Deliver symbols for XCP or external tools. Full IF_DATA fidelity is out of scope.")
    lay.addWidget(_ui.tool_strip(csv_btn, slim_btn, apply_btn, stretch_at=0))
    lay.addWidget(tip)
    lay.addStretch(1)

    def on_csv():
        rows = a2lparse.export_symbol_csv(session.doc)
        path = plugin_shell.export_csv(
            parent, rows[0], rows[1:], "a2l_symbols.csv")
        if path:
            log_fn("SYS", "-", b"", "Exported CSV %s" % path)
            plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    def on_slim():
        path, _ = QFileDialog.getSaveFileName(
            parent, "Export slim A2L", "export.a2l",
            "ASAP2 (*.a2l);;All (*.*)")
        if not path:
            return
        text = a2lparse.export_slim_a2l(session.doc)
        try:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        except OSError as e:
            plugin_shell.set_status(parent, "Save failed: %s" % e, 4000)
            return
        log_fn("SYS", "-", b"", "Exported slim A2L %s" % path)
        plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    csv_btn.clicked.connect(on_csv)
    slim_btn.clicked.connect(on_slim)
    apply_btn.clicked.connect(
        lambda: parent.run_action("a2l.apply_xcp")
        if hasattr(parent, "run_action") else None)
    return root
