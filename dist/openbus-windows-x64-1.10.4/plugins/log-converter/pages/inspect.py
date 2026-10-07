# -*- coding: utf-8 -*-
"""Inspect workspace — probe headers + format support matrix."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog, QFormLayout, QGroupBox, QHeaderView, QLabel, QPlainTextEdit,
    QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget,
)

from formats import FORMAT_CATALOG, OPEN_FILTER, human_size, probe_file
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    root.setObjectName("SuitePage")
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    open_btn = _ui.primary_btn("Probe file…", "Inspect a CAN log header", "search")
    layout.addWidget(_ui.tool_strip(open_btn))

    body = QWidget()
    body_lay = QVBoxLayout(body)
    body_lay.setContentsMargins(_ui.PAD_X, _ui.PAD_Y, _ui.PAD_X, _ui.PAD_Y)
    body_lay.setSpacing(10)
    layout.addWidget(body, 1)

    probe_box = QGroupBox("File probe")
    probe_form = QFormLayout(probe_box)
    path_lbl = QLabel("—")
    path_lbl.setTextInteractionFlags(
        Qt.TextInteractionFlag.TextSelectableByMouse)
    path_lbl.setWordWrap(True)
    fmt_lbl = QLabel("—")
    size_lbl = QLabel("—")
    fd_lbl = QLabel("—")
    notes_lbl = QLabel("—")
    notes_lbl.setWordWrap(True)
    probe_form.addRow("Path:", path_lbl)
    probe_form.addRow("Format:", fmt_lbl)
    probe_form.addRow("Size:", size_lbl)
    probe_form.addRow("CAN FD:", fd_lbl)
    probe_form.addRow("Notes:", notes_lbl)
    body_lay.addWidget(probe_box)

    preview = QPlainTextEdit()
    preview.setReadOnly(True)
    preview.setPlaceholderText("Header / early-line preview appears here…")
    preview.setMaximumBlockCount(40)
    preview.setFixedHeight(120)
    body_lay.addWidget(preview)

    matrix_box = QGroupBox("Format support matrix")
    matrix_lay = QVBoxLayout(matrix_box)
    matrix = QTableWidget(0, 6)
    matrix.setHorizontalHeaderLabels(
        ["Format", "Extensions", "Engine", "CAN FD", "Description", "Tools"])
    matrix.setEditTriggers(matrix.EditTrigger.NoEditTriggers)
    matrix.verticalHeader().setVisible(False)
    matrix.setSelectionBehavior(matrix.SelectionBehavior.SelectRows)
    matrix.horizontalHeader().setSectionResizeMode(
        4, QHeaderView.ResizeMode.Stretch)
    matrix.setWordWrap(True)
    for info in FORMAT_CATALOG:
        row = matrix.rowCount()
        matrix.insertRow(row)
        vals = [
            info.label,
            " ".join(info.extensions),
            "Yes" if info.engine else "Planned",
            info.can_fd,
            info.description,
            info.typical_tools,
        ]
        for c, text in enumerate(vals):
            item = QTableWidgetItem(text)
            if c == 2 and info.engine:
                item.setForeground(Qt.GlobalColor.darkGreen)
            elif c == 2:
                item.setForeground(Qt.GlobalColor.darkGray)
            matrix.setItem(row, c, item)
    matrix.resizeRowsToContents()
    matrix_lay.addWidget(matrix)
    body_lay.addWidget(matrix_box, 1)

    legend = QLabel(
        "Engine formats convert via OpenBus CanFileIO (host thread). "
        "MF4 / generic LOG are catalogued for roadmap parity with OEM benches "
        "(CANape, INCA, asammdf) — not yet writable here.")
    legend.setObjectName("SuiteHint")
    legend.setWordWrap(True)
    body_lay.addWidget(legend)

    def _show_probe(path: str):
        probe = probe_file(path)
        path_lbl.setText(path or "—")
        fmt_lbl.setText(
            "%s%s" % (
                probe.get("format_label") or "Unknown",
                " (host)" if probe.get("engine") else ""))
        size_lbl.setText(human_size(int(probe.get("size") or 0)))
        fd_lbl.setText(probe.get("can_fd_hint") or "n/a")
        notes = probe.get("notes") or []
        notes_lbl.setText(" · ".join(notes) if notes else "—")
        preview.setPlainText(probe.get("preview") or "")
        session.note_source(path)
        log_fn("SYS", "-", b"", "Probed %s" % os.path.basename(path))

    def _on_open():
        path, _ = QFileDialog.getOpenFileName(
            parent, "Probe CAN log", session.start_dir(), OPEN_FILTER)
        if path:
            _show_probe(path)

    open_btn.clicked.connect(_on_open)
    root.probe_path = _show_probe  # type: ignore[attr-defined]
    return root
