# -*- coding: utf-8 -*-
"""Spin editors with VS Code chevron steppers (native arrows are too small)."""

from __future__ import annotations

from PyQt6.QtCore import Qt, pyqtSignal, QSize
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QPushButton,
    QSpinBox,
    QWidget,
)

from _shared import vscode_theme, codicons


class StepSpin(QWidget):
    """QSpinBox with large icon steppers instead of native tiny arrows."""

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
        width: int = 100,
        parent=None,
    ):
        super().__init__(parent)
        self.setObjectName("StepSpin")
        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(2)

        self.spin = QSpinBox()
        self.spin.setRange(minimum, maximum)
        self.spin.setValue(value)
        self.spin.setButtonSymbols(QSpinBox.ButtonSymbols.NoButtons)
        self.spin.setFixedHeight(28)
        self.spin.setMinimumWidth(width)
        self.spin.setAlignment(Qt.AlignmentFlag.AlignCenter)
        if hex_mode:
            self.spin.setDisplayIntegerBase(16)
            self.spin.setPrefix(prefix or "0x")
        elif prefix:
            self.spin.setPrefix(prefix)
        if suffix:
            self.spin.setSuffix(suffix)
        self.spin.valueChanged.connect(self.valueChanged.emit)

        self.down_btn = QPushButton()
        self.up_btn = QPushButton()
        for b, name, tip in (
            (self.down_btn, "chevron-down", "Decrease"),
            (self.up_btn, "chevron-up", "Increase"),
        ):
            b.setObjectName("StepSpinBtn")
            b.setFixedSize(28, 28)
            b.setCursor(Qt.CursorShape.PointingHandCursor)
            b.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            b.setToolTip(tip)
            b.setIcon(codicons.icon(name, vscode_theme.TEXT, 14))
            b.setIconSize(QSize(14, 14))

        # Order: value, then down, then up (easy left-to-right hit targets).
        self.down_btn.clicked.connect(self.spin.stepDown)
        self.up_btn.clicked.connect(self.spin.stepUp)
        row.addWidget(self.spin, 1)
        row.addWidget(self.down_btn)
        row.addWidget(self.up_btn)

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
