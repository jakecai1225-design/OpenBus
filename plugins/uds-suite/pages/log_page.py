# -*- coding: utf-8 -*-
"""Log workspace — the only place that shows the full activity log."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QHBoxLayout,
    QHeaderView,
    QPushButton,
    QTableWidget,
    QVBoxLayout,
    QWidget,
)

from _shared import vscode_theme, codicons


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(14)

    export_btn = QPushButton("Export")
    export_btn.setObjectName("SecondaryButton")
    export_btn.setFixedSize(96, 28)
    export_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    codicons.set_button(export_btn, "export")
    clear_btn = QPushButton("Clear")
    clear_btn.setObjectName("SecondaryButton")
    clear_btn.setFixedSize(88, 28)
    clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    codicons.set_button(clear_btn, "clear")

    tools = QWidget()
    tl = QHBoxLayout(tools)
    tl.setContentsMargins(0, 0, 0, 0)
    tl.setSpacing(8)
    frames = QCheckBox("ISO-TP frames")
    frames.setToolTip("Include ISO-TP flow-control frames")
    frames.toggled.connect(lambda c: setattr(session, "show_isotp_frames", c))
    tl.addWidget(frames)
    tl.addWidget(export_btn)
    tl.addWidget(clear_btn)

    card, body = vscode_theme.block(
        "Activity log",
        "Every workspace appends here. Newest rows stay at the bottom.",
        trailing=tools,
    )

    table = QTableWidget(0, 5)
    table.setHorizontalHeaderLabels(["Time", "Dir", "CAN ID", "PDU", "Note"])
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.setShowGrid(False)
    table.setAlternatingRowColors(True)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    table.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    hdr = table.horizontalHeader()
    hdr.setHighlightSections(False)
    hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    hdr.setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
    table.verticalHeader().setDefaultSectionSize(24)
    body.addWidget(table, 1)
    layout.addWidget(card, 1)

    if hasattr(parent, "bind_log_table"):
        parent.bind_log_table(table)

    export_btn.clicked.connect(lambda: getattr(parent, "export_log", lambda: None)())
    clear_btn.clicked.connect(lambda: getattr(parent, "clear_log", lambda: None)())
    return root
