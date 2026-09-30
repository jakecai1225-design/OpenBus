# -*- coding: utf-8 -*-
"""Setup workspace — ISO-TP request / response IDs for OBD."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QComboBox,
    QLabel,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store
from pages import _ui, scanner

SUITE_ID = "obd-suite"


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    req = QComboBox()
    req.addItem("0x7DF functional", 0x7DF)
    req.addItem("0x7E0 physical", 0x7E0)
    req.setToolTip("Functional 0x7DF talks to every ECU; 0x7E0 targets ECM")
    rx = QSpinBox()
    rx.setRange(0x7E8, 0x7EF)
    rx.setDisplayIntegerBase(16)
    rx.setPrefix("0x")
    rx.setValue(int(session.rx_id) or 0x7E8)
    rx.setToolTip("Expected response ID (0x7E8–0x7EF)")
    apply_btn = _ui.primary_btn("Apply", "Apply request / response IDs", "apply")
    open_scan = _ui.ghost_btn(
        "Open Scanner", "Go to Scanner after applying IDs", "search")

    layout.addWidget(_ui.tool_strip(
        QLabel("Request"), req, QLabel("Response"), rx, apply_btn, open_scan))

    body = QWidget()
    body_l = QVBoxLayout(body)
    body_l.setContentsMargins(24, 32, 24, 24)
    hint = _ui.quiet_label(
        "Set ISO-TP IDs once here. Scanner and Readiness use the shared session.")
    hint.setToolTip(hint.text())
    body_l.addWidget(hint)
    body_l.addStretch(1)
    layout.addWidget(body, 1)

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
        session.apply_ids(tx, rid)
        scanner.apply_ids(tx, rid)
        prev = state_store.load_state(SUITE_ID, "settings.json") or {}
        prev["tx_id"] = tx
        prev["rx_id"] = rid
        state_store.save_state(SUITE_ID, prev, "settings.json")
        plugin_shell.set_status(parent, "IDs applied", 2000)
        log_fn("SYS", tx, b"", "OBD IDs request=0x%X response=0x%X" % (tx, rid))
        if hasattr(parent, "_sync_next_hint"):
            parent._sync_next_hint()

    def _open_scanner():
        if hasattr(parent, "run_action"):
            parent.run_action("obd.goto", page="scanner")
        elif hasattr(parent, "goto_page"):
            parent.goto_page("scanner")

    apply_btn.clicked.connect(_apply)
    open_scan.clicked.connect(_open_scanner)
    _ui.polish_work_surface(root)
    return root
