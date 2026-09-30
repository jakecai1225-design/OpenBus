# -*- coding: utf-8 -*-
"""Compact spin editors — one bordered control, no detached steppers."""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import QHBoxLayout, QSpinBox, QWidget

CTRL_H = 28


class StepSpin(QWidget):
    """Clean QSpinBox wrapper (type / wheel / arrow keys). No external buttons.

    Outer host owns the 1px border (QSS ``QWidget#StepSpin``). Inner spin is
    borderless and slightly shorter so the host bottom border stays visible.
    """

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
        self.setFixedHeight(CTRL_H)
        # Required for QSS border/background on a plain QWidget host.
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.setAutoFillBackground(True)
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
        # Leave 2px for the host border (top+bottom) so it is never clipped.
        self.spin.setFixedHeight(CTRL_H - 2)
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
        a = super().blockSignals(block)
        self.spin.blockSignals(block)
        return a

    def setToolTip(self, tip: str) -> None:
        super().setToolTip(tip)
        self.spin.setToolTip(tip)
