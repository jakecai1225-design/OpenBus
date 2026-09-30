# -*- coding: utf-8 -*-
"""Deprecated — use Session leaf + status Context Next instead of QuickBar."""

from __future__ import annotations

from PyQt6.QtWidgets import QWidget


class QuickBar(QWidget):
    """No-op stub (retired second chrome row)."""

    def __init__(self, session, parent=None):
        super().__init__(parent)
        self.setVisible(False)
