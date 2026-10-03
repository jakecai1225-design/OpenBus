# -*- coding: utf-8 -*-
"""CiA 402 Drive panel (lite) — controlword / statusword + common transitions."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFormLayout,
    QLabel,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core.cia301_codes import (
    STATUSWORD_BITS,
    bitfield_summary,
    statusword_state,
)
from pages import _ui

# Common CiA 402 controlword commands (bits 0..3 + Enable Operation etc.)
_CW_CMDS = (
    ("Shutdown", 0x0006),
    ("Switch on", 0x0007),
    ("Enable op", 0x000F),
    ("Disable voltage", 0x0000),
    ("Quick stop", 0x000B),
    ("Fault reset", 0x0080),
)


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)

    status = QLabel("—")
    status.setObjectName("SuiteCount")
    status.setToolTip("0x6041 statusword decode for the shared Node-ID")

    read_btn = _ui.primary_btn(
        "Read status", "SDO upload 0x6041 (statusword)", "arrow-right")
    refresh_health = _ui.ghost_btn(
        "Health", "Show Heartbeat / EMCY summary in OUTPUT", "info")

    btns = []
    for label, cw in _CW_CMDS:
        b = _ui.ghost_btn(
            label, "Write controlword 0x%04X to 0x6040" % cw, "play")
        b.clicked.connect(
            lambda _=False, v=cw: _write_cw(parent, session, log_fn, status, v))
        btns.append(b)

    lay.addWidget(_ui.tool_strip(
        read_btn, refresh_health, *btns[:3], stretch_at=0))
    lay.addWidget(_ui.tool_strip(*btns[3:], stretch_at=0))

    form_host = QWidget()
    form_host.setObjectName("SuitePropPanel")
    form = QFormLayout(form_host)
    form.setContentsMargins(_ui.PAD_X, 8, _ui.PAD_X, _ui.PAD_X)
    form.setSpacing(8)
    form.setFieldGrowthPolicy(
        QFormLayout.FieldGrowthPolicy.FieldsStayAtSizeHint)
    form.setLabelAlignment(
        Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)

    sw_lbl = QLabel("—")
    sw_lbl.setTextInteractionFlags(
        Qt.TextInteractionFlag.TextSelectableByMouse)
    state_lbl = QLabel("—")
    bits_lbl = QLabel("—")
    bits_lbl.setWordWrap(True)
    health_lbl = QLabel("—")
    health_lbl.setWordWrap(True)
    form.addRow(_ui.field_label("Statusword"), sw_lbl)
    form.addRow(_ui.field_label("State"), state_lbl)
    form.addRow(_ui.field_label("Bits"), bits_lbl)
    form.addRow(_ui.field_label("Network"), health_lbl)
    lay.addWidget(form_host, 0)
    lay.addStretch(1)

    tip = _ui.quiet_label(
        "CiA 402 lite — confirm each write. Full profile objects live under Profiles.")
    lay.addWidget(tip)

    def _paint_sw(value: int):
        sw_lbl.setText("0x%04X" % (value & 0xFFFF))
        state_lbl.setText(statusword_state(value))
        bits_lbl.setText(bitfield_summary(value, STATUSWORD_BITS) or "—")
        status.setText("%s · Node %d" % (statusword_state(value), session.node_id))

    def on_read():
        def done(ok, value, note):
            if ok:
                raw = value if isinstance(value, int) else 0
                if isinstance(value, (bytes, bytearray)):
                    raw = int.from_bytes(value[:2], "little")
                _paint_sw(int(raw) & 0xFFFF)
                if hasattr(session, "set_live_value"):
                    session.set_live_value(0x6041, 0, "0x%04X" % (raw & 0xFFFF))
                plugin_shell.set_status(parent, "Statusword OK", 2500)
            else:
                status.setText("FAIL: %s" % note)
                plugin_shell.set_status(parent, "Statusword read failed", 3000)

        if not session.sdo_upload(0x6041, 0, on_done=done):
            status.setText("SDO busy")

    def on_health():
        text = session.health_summary() if hasattr(session, "health_summary") else "—"
        health_lbl.setText(text)
        for ev in session.health.recent_emcy(5):
            log_fn(
                "ERR", "-", b"",
                "EMCY node %d: 0x%04X %s" % (
                    ev.node_id, ev.error_code, ev.message))
        plugin_shell.set_status(parent, text, 4000)

    def _refresh_health_lbl():
        if hasattr(session, "health_summary"):
            health_lbl.setText(session.health_summary())

    read_btn.clicked.connect(on_read)
    refresh_health.clicked.connect(on_health)
    if hasattr(session, "on_health_changed"):
        session.on_health_changed(_refresh_health_lbl)
    _refresh_health_lbl()
    return root


def _write_cw(parent, session, log_fn, status, value: int) -> None:
    from PyQt6.QtWidgets import QMessageBox
    reply = QMessageBox.question(
        parent,
        "Confirm controlword",
        "Write 0x%04X to node %d object 0x6040?"
        % (value, session.node_id),
        QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        QMessageBox.StandardButton.No,
    )
    if reply != QMessageBox.StandardButton.Yes:
        return

    def done(ok, _v, note):
        if ok:
            status.setText("CW 0x%04X OK" % value)
            log_fn("TX", "-", b"", "CiA 402 CW 0x%04X" % value)
            plugin_shell.set_status(parent, "Controlword OK", 2500)
        else:
            status.setText("FAIL: %s" % note)
            plugin_shell.set_status(parent, "Controlword failed", 3000)

    if not session.sdo_download(0x6040, 0, value, 2, on_done=done):
        status.setText("SDO busy")
