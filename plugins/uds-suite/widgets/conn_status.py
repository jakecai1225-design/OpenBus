# -*- coding: utf-8 -*-
"""One-line connection status — Diagnose / Security use this instead of BusStrip."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QSizePolicy, QWidget

from _shared import vscode_theme, codicons


class ConnStatus(QWidget):
    """Compact bar: current TX/RX/session + jump to Setup."""

    def __init__(self, session, shell, parent=None):
        super().__init__(parent)
        self.setObjectName("ConnStatus")
        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        self._session = session
        self._shell = shell

        row = QHBoxLayout(self)
        row.setContentsMargins(0, 2, 0, 8)
        row.setSpacing(10)
        title = QLabel("Bus")
        title.setObjectName("BusStripLabel")
        row.addWidget(title)
        self.summary = QLabel("")
        self.summary.setObjectName("SuiteHint")
        row.addWidget(self.summary, 1)
        self.session_badge = QLabel("Session: unknown")
        self.session_badge.setObjectName("SessionBadge")
        row.addWidget(self.session_badge)
        btn = QPushButton("Setup")
        btn.setObjectName("SecondaryButton")
        btn.setFixedSize(84, 26)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setToolTip("Open Setup to change IDs, session, and timing")
        codicons.set_button(btn, "settings", size=12)
        btn.clicked.connect(lambda: shell.goto_page("setup"))
        row.addWidget(btn)

        session.on_ids_changed(self._refresh)
        session.on_session_changed(self._on_session)
        self._refresh()

    def _refresh(self, *_a):
        s = self._session
        self.summary.setText(
            "TX 0x%X  /  RX 0x%X  /  Func 0x%X%s" % (
                s.tx_id, s.rx_id, s.func_id,
                "  /  functional" if s.functional else "",
            ))

    def _on_session(self, name: str):
        self.session_badge.setText("Session: %s" % name)
