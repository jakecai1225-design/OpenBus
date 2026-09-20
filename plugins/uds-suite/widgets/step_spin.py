# -*- coding: utf-8 -*-
"""Compact spin editors — one bordered control, no detached steppers."""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import QSpinBox, QWidget, QHBoxLayout


class StepSpin(QWidget):
    """Clean QSpinBox wrapper (type / wheel / arrow keys). No external buttons."""

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
        width: int = 96,
        parent=None,
    ):
        super().__init__(parent)
        self.setObjectName("StepSpin")
        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(0)

        self.spin = QSpinBox()
        self.spin.setObjectName("SuiteSpin")
        self.spin.setRange(minimum, maximum)
        self.spin.setValue(value)
        self.spin.setButtonSymbols(QSpinBox.ButtonSymbols.NoButtons)
        self.spin.setFixedHeight(28)
        self.spin.setFixedWidth(width)
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
        row.addWidget(self.spin)

    def value(self) -> int:
        return self.spin.value()

    def setValue(self, v: int) -> None:
        self.spin.setValue(v)

    def setRange(self, lo: int, hi: int) -> None:
        self.spin.setRange(lo, hi)

    def blockSignals(self, block: bool) -> bool:
        return self.spin.blockSignals(block)

    def setToolTip(self, tip: str) -> None:
        super().setToolTip(tip)
        self.spin.setToolTip(tip)
