# -*- coding: utf-8 -*-
"""Project overview — retired from the activity stack.

Project create/open and Project EDS live under the File menu.
This module remains importable for tests / stale deep-links only.
"""

from __future__ import annotations

from PyQt6.QtWidgets import QLabel, QVBoxLayout, QWidget

from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(_ui.PAD_X * 2, 20, _ui.PAD_X * 2, 16)
    lay.setSpacing(8)
    tip = QLabel(
        "Projects live under File → New/Open Project and File → Project EDS.")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    lay.addWidget(tip)
    lay.addStretch(1)

    def _refresh(_=None):
        pass

    root.refresh_project = _refresh  # type: ignore[attr-defined]
    return root
