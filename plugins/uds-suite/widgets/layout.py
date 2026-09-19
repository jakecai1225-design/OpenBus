# -*- coding: utf-8 -*-
"""VS Code–style page chrome helpers: workbench layout + OUTPUT panel."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import vscode_theme, codicons


def page(parent) -> tuple[QWidget, QVBoxLayout]:
    """Flat workbench column — breathing room via margins, not nested frames."""
    root = QWidget(parent)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(16, 12, 16, 12)
    lay.setSpacing(12)
    return root, lay


def attach_output(parent, layout: QVBoxLayout, body: QWidget, session) -> QTableWidget:
    """Split body + OUTPUT. Live TX/RX feedback for pages that send frames."""
    splitter = QSplitter(Qt.Orientation.Vertical)
    splitter.setHandleWidth(3)
    splitter.setChildrenCollapsible(False)
    splitter.addWidget(body)

    clear_btn = QPushButton("Clear")
    clear_btn.setObjectName("GhostButton")
    clear_btn.setFixedSize(80, 26)
    codicons.set_button(clear_btn, "clear", size=12)

    panel = QWidget()
    panel.setObjectName("SuiteLogPanel")
    panel.setMinimumHeight(160)
    pv = QVBoxLayout(panel)
    pv.setContentsMargins(0, 0, 0, 0)
    pv.setSpacing(0)

    head = QWidget()
    head.setObjectName("SuiteLogHeader")
    head.setFixedHeight(32)
    hl = QHBoxLayout(head)
    hl.setContentsMargins(0, 4, 0, 0)
    hl.setSpacing(8)
    title = QLabel("OUTPUT")
    title.setObjectName("SuiteSectionTitle")
    hl.addWidget(title)
    hint = QLabel("Live TX / RX")
    hint.setObjectName("SuiteHint")
    hl.addWidget(hint)
    hl.addStretch(1)
    hl.addWidget(clear_btn)
    pv.addWidget(head)

    table = QTableWidget(0, 4)
    table.setObjectName("OutputTable")
    table.setHorizontalHeaderLabels(["Time", "Dir", "ID", "Note"])
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.setShowGrid(False)
    table.setAlternatingRowColors(True)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    hdr = table.horizontalHeader()
    hdr.setHighlightSections(False)
    hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    table.verticalHeader().setDefaultSectionSize(22)
    pv.addWidget(table, 1)
    splitter.addWidget(panel)
    # Prefer workbench, but keep OUTPUT tall enough to read a sequence.
    splitter.setSizes([640, 220])
    splitter.setStretchFactor(0, 4)
    splitter.setStretchFactor(1, 1)
    layout.addWidget(splitter, 1)

    def _on_log(direction, can_id, pdu, note, color=None):
        import time
        tstr = time.strftime("%H:%M:%S", time.localtime())
        idstr = ("0x%X" % can_id) if isinstance(can_id, int) else str(can_id)
        colors = {
            "TX": QColor(vscode_theme.TX),
            "RX": QColor(vscode_theme.RX),
            "ERR": QColor(vscode_theme.ERR),
            "FC": QColor(vscode_theme.FC),
        }
        c = colors.get(color or direction, QColor(vscode_theme.TEXT_DIM))
        row = table.rowCount()
        table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            table.setItem(row, col, item)
        while table.rowCount() > 400:
            table.removeRow(0)
        table.scrollToBottom()

    prev = getattr(session, "_log_fn", None)

    def _fan(direction, can_id, pdu, note, color=None):
        if prev:
            prev(direction, can_id, pdu, note, color)
        _on_log(direction, can_id, pdu, note, color)

    session.set_log_fn(_fan)
    clear_btn.clicked.connect(lambda: table.setRowCount(0))
    return table
