# -*- coding: utf-8 -*-
"""Quality snapshot — rolling bus-load estimate from live frames."""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QDoubleSpinBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from _shared import vscode_theme


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "Quality",
        "One-second snapshot. Load uses a rough bit estimate against the reference baud.")
    row = QHBoxLayout()
    baud = QDoubleSpinBox()
    baud.setRange(10, 8000)
    baud.setValue(500)
    baud.setSuffix(" kbps")
    reset_btn = QPushButton("Reset")
    summary = QLabel("Collecting")
    row.addWidget(QLabel("Baud"))
    row.addWidget(baud)
    row.addWidget(reset_btn)
    row.addStretch(1)
    row.addWidget(summary)
    body.addLayout(row)
    layout.addWidget(card)

    detail = QLabel("No frames in the current window.")
    detail.setWordWrap(True)
    layout.addWidget(detail)
    layout.addStretch(1)

    window = {"bits": 0, "frames": 0, "ids": set(), "t0": time.monotonic()}

    def _reset():
        window["bits"] = 0
        window["frames"] = 0
        window["ids"] = set()
        window["t0"] = time.monotonic()

    def _on_frame(frame):
        data = bytes(getattr(frame, "data", b"") or b"")
        window["frames"] += 1
        window["bits"] += 64 + 8 * len(data)
        window["ids"].add(frame.id)

    def _tick():
        elapsed = time.monotonic() - window["t0"]
        if elapsed >= 1.0:
            bps = baud.value() * 1000.0
            load = (100.0 * window["bits"] / bps) if bps else 0.0
            summary.setText("%.0f%% load" % min(load, 999.0))
            detail.setText(
                "%d frames/s · %d IDs · %d payload-bits (est.) in %.2f s"
                % (window["frames"], len(window["ids"]), window["bits"], elapsed))
            _reset()

    session.on_bus_frame(_on_frame)
    reset_btn.clicked.connect(_reset)
    timer = QTimer(root)
    timer.timeout.connect(_tick)
    timer.start(200)
    return root
