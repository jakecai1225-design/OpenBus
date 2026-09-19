# -*- coding: utf-8 -*-
"""Setup workspace — ISO-TP request / response IDs for OBD."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QComboBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store, vscode_theme
from pages import scanner

SUITE_ID = "obd-suite"


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "ISO-TP",
        "Functional 0x7DF talks to every ECU. Physical 0x7E0 targets the powertrain ECU.")
    row = QHBoxLayout()
    req = QComboBox()
    req.addItem("0x7DF functional", 0x7DF)
    req.addItem("0x7E0 physical", 0x7E0)
    rx = QSpinBox()
    rx.setRange(0x7E8, 0x7EF)
    rx.setDisplayIntegerBase(16)
    rx.setPrefix("0x")
    rx.setValue(0x7E8)
    apply_btn = QPushButton("Apply")
    row.addWidget(QLabel("Request"))
    row.addWidget(req)
    row.addWidget(QLabel("Response"))
    row.addWidget(rx)
    row.addWidget(apply_btn)
    row.addStretch(1)
    body.addLayout(row)
    layout.addWidget(card)
    layout.addStretch(1)

    saved = state_store.load_state(SUITE_ID, "settings.json") or {}
    if saved.get("tx_id"):
        idx = req.findData(int(saved["tx_id"]))
        if idx >= 0:
            req.setCurrentIndex(idx)
    if saved.get("rx_id"):
        rx.setValue(int(saved["rx_id"]))

    def _apply():
        tx = int(req.currentData() or 0x7DF)
        rid = int(rx.value())
        session.tx_id = tx
        session.rx_id = rid
        scanner.apply_ids(tx, rid)
        prev = state_store.load_state(SUITE_ID, "settings.json") or {}
        prev["tx_id"] = tx
        prev["rx_id"] = rid
        state_store.save_state(SUITE_ID, prev, "settings.json")
        plugin_shell.set_status(parent, "IDs applied", 2000)
        log_fn("SYS", tx, b"", "OBD IDs request=0x%X response=0x%X" % (tx, rid))

    apply_btn.clicked.connect(_apply)
    return root
