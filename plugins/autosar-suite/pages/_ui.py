# -*- coding: utf-8 -*-
"""AUTOSAR Studio UI — shared suite_ui density."""

from __future__ import annotations

from PyQt6.QtWidgets import QLabel

from _shared import vscode_theme
from _shared.suite_ui import *  # noqa: F401,F403
from _shared.suite_ui import apply_suite_chrome as apply_autosar_chrome  # noqa: F401
from _shared.suite_ui import quiet_label


def tip_panel() -> QLabel:
    """Mutable hint label used by editor property panes."""
    lab = QLabel("")
    lab.setWordWrap(True)
    lab.setStyleSheet(
        "color:%s;font-size:12px;padding:6px 8px;"
        "background:transparent;border-top:1px solid %s;" % (
            vscode_theme.TEXT_MUTED, vscode_theme.BORDER_SOFT))
    lab.setMinimumHeight(48)
    return lab
