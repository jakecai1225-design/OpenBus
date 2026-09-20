# -*- coding: utf-8 -*-
"""VS Code–style page chrome helpers.

OUTPUT is owned by AppShell's bottom panel (suite_chrome workbench).
Pages should only build their center content — do not attach a second log.
"""

from __future__ import annotations

from PyQt6.QtWidgets import QVBoxLayout, QWidget


def page(parent) -> tuple[QWidget, QVBoxLayout]:
    """Flat workbench column — breathing room via margins, not nested frames."""
    root = QWidget(parent)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(16, 12, 16, 12)
    lay.setSpacing(12)
    return root, lay


def attach_output(parent, layout: QVBoxLayout, body: QWidget, session):
    """Deprecated: shell-level OUTPUT replaced per-page panels.

    Kept as a no-op that just mounts *body* so older call sites still run.
    """
    layout.addWidget(body, 1)
    return None
