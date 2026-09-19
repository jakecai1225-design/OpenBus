# -*- coding: utf-8 -*-
"""Compact connection toolbar for pages that send frames.

One thin strip (not four tall cards) so Workbench and OUTPUT keep the space.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import (
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from _shared import vscode_theme, codicons


def _lab(text: str) -> QLabel:
    lab = QLabel(text)
    lab.setObjectName("BusStripLabel")
    return lab


class BusStrip(QWidget):
    """Shared TX / RX / timing. Optional session shortcuts for Diagnose."""

    applied = pyqtSignal()

    def __init__(self, session, *, with_session_buttons: bool = False, parent=None):
        super().__init__(parent)
        self.setObjectName("BusStrip")
        self.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        self._session = session

        outer = QVBoxLayout(self)
        outer.setContentsMargins(8, 6, 8, 6)
        outer.setSpacing(4)

        row1 = QHBoxLayout()
        row1.setSpacing(6)
        row1.addWidget(_lab("TX"))
        self.tx_spin = self._hex_spin(0x7E0)
        row1.addWidget(self.tx_spin)
        row1.addWidget(_lab("RX"))
        self.rx_spin = self._hex_spin(0x7E8)
        row1.addWidget(self.rx_spin)
        row1.addWidget(_lab("Func"))
        self.func_spin = self._hex_spin(0x7DF)
        row1.addWidget(self.func_spin)
        self.func_check = QCheckBox("Functional")
        self.func_check.setToolTip("Send on the functional ID instead of TX")
        row1.addWidget(self.func_check)

        self.apply_btn = QPushButton("Apply")
        self.apply_btn.setObjectName("PrimaryButton")
        self.apply_btn.setFixedSize(78, 26)
        self.apply_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.apply_btn.setToolTip("Write IDs and timing into the shared session")
        codicons.set_button(self.apply_btn, "apply", primary=True, size=12)
        self.apply_btn.clicked.connect(self._on_apply)
        row1.addWidget(self.apply_btn)

        if with_session_buttons:
            row1.addWidget(vscode_theme.v_divider())
            for code, label in ((0x01, "Default"), (0x03, "Extended"), (0x02, "Prog")):
                b = QPushButton(label)
                b.setObjectName("SecondaryButton")
                b.setFixedHeight(26)
                b.setCursor(Qt.CursorShape.PointingHandCursor)
                b.setToolTip("Session 0x%02X" % code)
                b.clicked.connect(lambda _=False, s=code: session.go_session(s))
                row1.addWidget(b)
            self.session_label = QLabel("Session: unknown")
            self.session_label.setObjectName("SessionBadge")
            row1.addWidget(self.session_label)
            self.tp_check = QCheckBox("Keep-alive")
            self.tp_check.setToolTip(
                "TesterPresent 3E 80. During flash it is functional so 36 is not blocked.")
            self.tp_check.toggled.connect(self._on_tp)
            row1.addWidget(self.tp_check)
            session.on_session_changed(self._on_session)
        else:
            self.session_label = None
            self.tp_check = QCheckBox("Keep-alive")
            self.tp_check.setToolTip(
                "TesterPresent 3E 80. During flash it is functional so 36 is not blocked.")
            self.tp_check.toggled.connect(self._on_tp)
            row1.addWidget(self.tp_check)

        row1.addStretch(1)
        outer.addLayout(row1)

        row2 = QHBoxLayout()
        row2.setSpacing(6)
        row2.addWidget(_lab("P2"))
        self.p2_spin = self._ms(2000, 50, 10000)
        self.p2_spin.setToolTip("P2 server timeout")
        row2.addWidget(self.p2_spin)
        row2.addWidget(_lab("P2*"))
        self.p2s_spin = self._ms(5000, 50, 60000)
        self.p2s_spin.setToolTip("P2* after NRC 0x78")
        row2.addWidget(self.p2s_spin)
        row2.addWidget(_lab("TP"))
        self.tp_spin = self._ms(2000, 200, 10000)
        self.tp_spin.setToolTip("TesterPresent interval")
        row2.addWidget(self.tp_spin)
        row2.addWidget(vscode_theme.v_divider())
        self.read_id_btn = QPushButton("Read ID")
        self.read_id_btn.setObjectName("SecondaryButton")
        self.read_id_btn.setFixedHeight(26)
        self.read_id_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.read_id_btn.setToolTip("F186, F187, F18A, F18C, F190, F191, F195, F197")
        codicons.set_button(self.read_id_btn, "info", size=12)
        self.read_id_btn.clicked.connect(self._session.read_identity)
        row2.addWidget(self.read_id_btn)
        hint = QLabel("Apply writes IDs and timing. Shared by every page that sends.")
        hint.setObjectName("SuiteHint")
        row2.addWidget(hint, 1)
        outer.addLayout(row2)

        self.sync_from_session()
        session.on_ids_changed(self.sync_from_session)

    def _hex_spin(self, val: int) -> QSpinBox:
        s = QSpinBox()
        s.setRange(1, 0x7FF)
        s.setDisplayIntegerBase(16)
        s.setPrefix("0x")
        s.setValue(val)
        s.setFixedWidth(72)
        s.setFixedHeight(26)
        s.setButtonSymbols(QSpinBox.ButtonSymbols.NoButtons)
        s.setAlignment(Qt.AlignmentFlag.AlignCenter)
        return s

    def _ms(self, value, lo, hi) -> QSpinBox:
        s = QSpinBox()
        s.setRange(lo, hi)
        s.setValue(value)
        s.setSuffix(" ms")
        s.setFixedWidth(88)
        s.setFixedHeight(26)
        s.setButtonSymbols(QSpinBox.ButtonSymbols.NoButtons)
        return s

    def sync_from_session(self):
        s = self._session
        for spin, val in (
            (self.tx_spin, s.tx_id),
            (self.rx_spin, s.rx_id),
            (self.func_spin, s.func_id),
        ):
            spin.blockSignals(True)
            spin.setValue(val)
            spin.blockSignals(False)
        self.func_check.blockSignals(True)
        self.func_check.setChecked(s.functional)
        self.func_check.blockSignals(False)
        self.tp_check.blockSignals(True)
        self.tp_check.setChecked(s.tester_present)
        self.tp_check.blockSignals(False)
        c = s.client
        self.p2_spin.blockSignals(True)
        self.p2_spin.setValue(min(10000, max(50, c.p2_ms)))
        self.p2_spin.blockSignals(False)
        self.p2s_spin.blockSignals(True)
        self.p2s_spin.setValue(min(60000, max(50, c.p2star_ms)))
        self.p2s_spin.blockSignals(False)
        self.tp_spin.blockSignals(True)
        self.tp_spin.setValue(min(10000, max(200, getattr(s, "tp_interval_ms", 2000))))
        self.tp_spin.blockSignals(False)

    def _on_apply(self):
        s = self._session
        s.functional = self.func_check.isChecked()
        s.apply_ids(self.tx_spin.value(), self.rx_spin.value(), self.func_spin.value())
        if hasattr(s, "set_timing"):
            s.set_timing(self.p2_spin.value(), self.p2s_spin.value(), self.tp_spin.value())
        self.applied.emit()

    def _on_tp(self, checked: bool):
        self._session.tester_present = checked

    def _on_session(self, name: str):
        if self.session_label is not None:
            self.session_label.setText("Session: %s" % name)
        c = self._session.client
        self.p2_spin.setValue(min(10000, max(50, c.p2_ms)))
        self.p2s_spin.setValue(min(60000, max(50, c.p2star_ms)))
