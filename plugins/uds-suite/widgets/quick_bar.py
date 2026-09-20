# -*- coding: utf-8 -*-
"""One-line quick actions — session + bus summary without leaving Diagnose."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QButtonGroup,
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QWidget,
)

from _shared import vscode_theme, codicons


class QuickBar(QWidget):
    """Essential controls always one click away while diagnosing.

    Session switch + keep-alive + bus summary → Setup. No full-width chrome.
    """

    def __init__(self, session, shell, parent=None):
        super().__init__(parent)
        self.setObjectName("SuiteQuickBar")
        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        self._session = session
        self._shell = shell

        row = QHBoxLayout(self)
        row.setContentsMargins(12, 6, 12, 6)
        row.setSpacing(10)

        lab = QLabel("Session")
        lab.setObjectName("SuiteFieldLabel")
        row.addWidget(lab)

        seg = QWidget()
        seg.setObjectName("SuiteSegment")
        seg_l = QHBoxLayout(seg)
        seg_l.setContentsMargins(0, 0, 0, 0)
        seg_l.setSpacing(0)
        group = QButtonGroup(seg)
        group.setExclusive(True)
        self._session_btns = {}
        for i, (code, label) in enumerate(
                ((0x01, "Default"), (0x03, "Extended"), (0x02, "Prog"))):
            b = QPushButton(label)
            b.setObjectName("SegmentBtn")
            b.setCheckable(True)
            b.setFixedHeight(24)
            b.setMinimumWidth(72)
            b.setCursor(Qt.CursorShape.PointingHandCursor)
            b.setToolTip("DiagnosticSessionControl 0x%02X — one click" % code)
            if i == 0:
                b.setProperty("segment", "first")
            elif i == 2:
                b.setProperty("segment", "last")
            else:
                b.setProperty("segment", "mid")
            b.style().unpolish(b)
            b.style().polish(b)
            group.addButton(b)
            self._session_btns[code] = b
            b.clicked.connect(lambda _=False, s=code: session.go_session(s))
            seg_l.addWidget(b)
        row.addWidget(seg)

        self.tp_check = QCheckBox("Keep-alive")
        self.tp_check.setChecked(session.tester_present)
        self.tp_check.setToolTip("TesterPresent 3E 80 while you work")
        self.tp_check.toggled.connect(lambda c: setattr(session, "tester_present", c))
        row.addWidget(self.tp_check)

        row.addWidget(vscode_theme.v_divider())

        self.summary = QLabel("")
        self.summary.setObjectName("SuiteHint")
        self.summary.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        row.addWidget(self.summary, 1)

        setup_btn = QPushButton("Bus IDs")
        setup_btn.setObjectName("GhostButton")
        setup_btn.setFixedHeight(24)
        setup_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        setup_btn.setToolTip("Change TX / RX / Func / timing in Setup")
        codicons.set_button(setup_btn, "settings", size=12)
        setup_btn.clicked.connect(lambda: shell.goto_page("setup"))
        row.addWidget(setup_btn)

        session.on_ids_changed(self._refresh)
        session.on_session_changed(self._on_session)
        self._refresh()

    def _refresh(self, *_a):
        s = self._session
        self.summary.setText(
            "TX 0x%X   RX 0x%X   Func 0x%X%s" % (
                s.tx_id, s.rx_id, s.func_id,
                "   functional" if s.functional else "",
            ))
        self.tp_check.blockSignals(True)
        self.tp_check.setChecked(s.tester_present)
        self.tp_check.blockSignals(False)

    def _on_session(self, name: str):
        # Clear checks; go_session name mapping is unknown here — leave visual soft
        for btn in self._session_btns.values():
            btn.setChecked(False)
        self.summary.setToolTip("Current session: %s" % name)
