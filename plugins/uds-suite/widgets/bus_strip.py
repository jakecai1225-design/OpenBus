# -*- coding: utf-8 -*-
"""Deprecated — connection chrome lives on Session leaf + Setup + status bar.

Kept so old imports do not crash; do not mount on work pages.
"""

from __future__ import annotations

from PyQt6.QtWidgets import QLabel, QWidget


class BusStrip(QWidget):
    """No-op stub (retired full-width connection strip)."""

    def __init__(self, session, *, with_session_buttons: bool = False, parent=None):
        super().__init__(parent)
        self.setVisible(False)
        lab = QLabel("")
        lab.setParent(self)
