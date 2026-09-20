# -*- coding: utf-8 -*-
"""Small control helpers. Hints live on tooltips, not extra caption rows."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QPushButton,
    QSpinBox,
    QTableWidget,
    QWidget,
)

from _shared import codicons


def spin(lo: int, hi: int, value: int, tip: str = "") -> QSpinBox:
    box = QSpinBox()
    box.setObjectName("SuiteSpin")
    box.setRange(lo, hi)
    box.setValue(int(value))
    box.setFixedHeight(28)
    if tip:
        box.setToolTip(tip)
    return box


def combo(items: list, tip: str = "") -> QComboBox:
    box = QComboBox()
    box.addItems(items)
    box.setFixedHeight(28)
    if tip:
        box.setToolTip(tip)
    return box


def ghost(text: str, icon: str, tip: str) -> QPushButton:
    btn = QPushButton(text)
    btn.setObjectName("GhostButton")
    btn.setFixedHeight(22)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    codicons.set_button(btn, icon, size=12)
    return btn


def primary(text: str, tip: str) -> QPushButton:
    btn = QPushButton(text)
    btn.setObjectName("PrimaryButton")
    btn.setFixedHeight(28)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    return btn


def table(headers: list) -> QTableWidget:
    grid = QTableWidget(0, len(headers))
    grid.setHorizontalHeaderLabels(headers)
    grid.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    grid.verticalHeader().setVisible(False)
    grid.setShowGrid(False)
    grid.setAlternatingRowColors(True)
    grid.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    hdr = grid.horizontalHeader()
    hdr.setHighlightSections(False)
    hdr.setStretchLastSection(True)
    hdr.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    if headers:
        hdr.setSectionResizeMode(len(headers) - 1, QHeaderView.ResizeMode.Stretch)
    grid.verticalHeader().setDefaultSectionSize(24)
    return grid


def wrap_width(widget: QWidget, width: int = 680) -> QWidget:
    host = QWidget()
    lay = QHBoxLayout(host)
    lay.setContentsMargins(16, 12, 16, 8)
    widget.setMaximumWidth(width)
    lay.addWidget(widget, 0, Qt.AlignmentFlag.AlignTop)
    lay.addStretch(1)
    return host
