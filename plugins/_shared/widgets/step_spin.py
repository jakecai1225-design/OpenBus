# -*- coding: utf-8 -*-
"""Unified numeric field — fixed 28px height, matching chevron steppers."""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal, QSize
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QSpinBox,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, vscode_theme as T

CTRL_H = 28
STEP_W = 18


class StepSpin(QWidget):
    """Bordered spin: edit + identical up/down chevrons (no native mismatch)."""

    valueChanged = pyqtSignal(int)

    def __init__(
        self,
        value: int = 0,
        *,
        minimum: int = 0,
        maximum: int = 99,
        hex_mode: bool = False,
        suffix: str = "",
        prefix: str = "",
        width: int = 120,
        steppers: bool = True,
        parent=None,
    ):
        super().__init__(parent)
        self.setObjectName("StepSpin")
        self.setFixedHeight(CTRL_H)
        if width > 0:
            self.setFixedWidth(width)

        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(0)

        self.spin = QSpinBox()
        self.spin.setObjectName("SuiteSpin")
        self.spin.setRange(minimum, maximum)
        self.spin.setValue(value)
        self.spin.setButtonSymbols(QSpinBox.ButtonSymbols.NoButtons)
        self.spin.setFixedHeight(CTRL_H)
        self.spin.setAlignment(
            Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        self.spin.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        if hex_mode:
            self.spin.setDisplayIntegerBase(16)
            self.spin.setPrefix(prefix or "0x")
        elif prefix:
            self.spin.setPrefix(prefix)
        if suffix:
            self.spin.setSuffix(suffix)
        self.spin.valueChanged.connect(self.valueChanged.emit)
        # Alias so callers can connect editingFinished like a QSpinBox
        self.editingFinished = self.spin.editingFinished
        row.addWidget(self.spin, 1)

        self._up = self._down = None
        if steppers:
            col = QWidget()
            col.setObjectName("StepSpinButtons")
            col.setFixedWidth(STEP_W)
            col.setFixedHeight(CTRL_H)
            vl = QVBoxLayout(col)
            vl.setContentsMargins(0, 0, 0, 0)
            vl.setSpacing(0)
            half = CTRL_H // 2
            self._up = self._make_step("chevron-up", "Increase", half)
            self._down = self._make_step(
                "chevron-down", "Decrease", CTRL_H - half)
            self._up.clicked.connect(self.spin.stepUp)
            self._down.clicked.connect(self.spin.stepDown)
            vl.addWidget(self._up)
            vl.addWidget(self._down)
            row.addWidget(col)

    def _make_step(self, icon: str, tip: str, height: int) -> QToolButton:
        btn = QToolButton()
        btn.setObjectName("StepSpinBtn")
        btn.setAutoRaise(True)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        btn.setToolTip(tip)
        btn.setFixedSize(STEP_W, height)
        btn.setIconSize(QSize(12, 12))
        try:
            # Match toolbar contrast — dim but readable on light panels
            codicons.set_button(btn, icon, color=T.TEXT, size=12)
        except Exception:
            btn.setText("▴" if "up" in icon else "▾")
            btn.setStyleSheet("font-size: 9px; padding: 0; border: none;")
        return btn

    def value(self) -> int:
        return self.spin.value()

    def setValue(self, v: int) -> None:
        self.spin.setValue(v)

    def setRange(self, lo: int, hi: int) -> None:
        self.spin.setRange(lo, hi)

    def blockSignals(self, block: bool) -> bool:
        a = super().blockSignals(block)
        self.spin.blockSignals(block)
        return a

    def setToolTip(self, tip: str) -> None:
        super().setToolTip(tip)
        self.spin.setToolTip(tip)

    def setEnabled(self, on: bool) -> None:
        super().setEnabled(on)
        self.spin.setEnabled(on)
        if self._up:
            self._up.setEnabled(on)
        if self._down:
            self._down.setEnabled(on)
