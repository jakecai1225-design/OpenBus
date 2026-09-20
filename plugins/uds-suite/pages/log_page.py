# -*- coding: utf-8 -*-
"""Log workspace — retained for API compatibility.

The activity log now lives in the shell bottom OUTPUT panel.
Calling build() returns an empty placeholder; prefer shell.goto_page('log')
which expands the OUTPUT panel.
"""

from __future__ import annotations

from PyQt6.QtWidgets import QLabel, QVBoxLayout, QWidget


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    hint = QLabel(
        "Activity log moved to the OUTPUT panel below.\n"
        "Use Ctrl+J or the panel layout button to show or hide it.")
    hint.setObjectName("SuiteHint")
    hint.setWordWrap(True)
    layout.addWidget(hint)
    layout.addStretch(1)
    # Expand OUTPUT if the shell supports it
    wb = getattr(parent, "_wb", None)
    if wb is not None:
        wb.expand_panel()
    return root
