# -*- coding: utf-8 -*-
"""Setup — retired (Node-ID via Preferences / Live; EDS via File menu).

Kept as a stub so stale imports do not crash. Prefer prefs.open / File.
"""

from __future__ import annotations

from PyQt6.QtWidgets import QLabel, QVBoxLayout, QWidget

from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    """Deprecated page — redirects callers to status hint only."""
    root = QWidget(parent)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(_ui.PAD_X * 2, 20, _ui.PAD_X * 2, 16)
    tip = QLabel(
        "Setup moved: Node-ID is Preferences (click N# in the status bar). "
        "EDS files are under File.")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    lay.addWidget(tip)
    lay.addStretch(1)
    return root
