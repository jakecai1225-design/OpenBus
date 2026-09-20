# -*- coding: utf-8 -*-
"""SecOC authentic I-PDU: freshness + truncated HMAC-SHA256."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLineEdit,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.secoc import build_secured, verify_secured
from widgets import primary, spin, table


def _parse_hex(text: str) -> bytes:
    cleaned = "".join(text.split()).replace("0x", "")
    if not cleaned:
        return b""
    if len(cleaned) % 2:
        cleaned = "0" + cleaned
    return bytes.fromhex(cleaned)


def build(_parent, session, _log):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)

    can_id = spin(0, 0x1FFFFFFF, session.secoc_can_id, "CAN id for the secured PDU")
    can_id.setDisplayIntegerBase(16)
    can_id.setPrefix("0x")
    fv_bits = spin(0, 64, session.secoc_fv_bits, "Freshness value length in bits")
    mac_bits = spin(0, 128, session.secoc_mac_bits, "Truncated MAC length in bits")
    fv = spin(0, 0xFFFFFF, 1, "Freshness counter to transmit")
    key = QLineEdit(session.secoc_key.hex())
    key.setFixedHeight(28)
    key.setMaximumWidth(180)
    key.setToolTip("Demo MAC key, hex")
    payload = QLineEdit("11 22 33 44")
    payload.setFixedHeight(28)
    payload.setToolTip("Authentic payload, hex")
    send = primary("Build and send", "Append freshness and truncated HMAC, then send")

    row = QHBoxLayout()
    for w in (can_id, fv_bits, mac_bits, fv, key, payload, send):
        row.addWidget(w)
    row.addStretch(1)
    root.addLayout(row)

    grid = table(["CAN ID", "Payload", "FV", "MAC", "Result"])
    root.addWidget(grid, 1)

    def pull():
        session.secoc_can_id = can_id.value()
        session.secoc_fv_bits = fv_bits.value()
        session.secoc_mac_bits = mac_bits.value()
        try:
            session.secoc_key = _parse_hex(key.text()) or b"\x00"
        except ValueError:
            session.log("ERR", "-", b"", "Key is not hex")

    def do_send():
        pull()
        try:
            body = _parse_hex(payload.text())
        except ValueError:
            session.log("ERR", "-", b"", "Payload is not hex")
            return
        frame = build_secured(
            body, fv.value(), session.secoc_fv_bits, session.secoc_mac_bits, session.secoc_key)
        session.send(session.secoc_can_id, frame, "SecOC fv %d" % fv.value())
        fv.setValue(fv.value() + 1)

    def on_frame(cid, data):
        if cid != session.secoc_can_id:
            return
        info = verify_secured(data, session.secoc_fv_bits, session.secoc_mac_bits, session.secoc_key)
        r = grid.rowCount()
        grid.insertRow(r)
        if not info.get("ok"):
            vals = ("0x%X" % cid, "-", "-", "-", info.get("reason", "fail"))
        else:
            vals = (
                "0x%X" % cid,
                info["payload"].hex(" ").upper(),
                str(info["freshness"]),
                "%X" % info["mac"],
                "OK" if info.get("mac_ok") else "MAC",
            )
        for c, text in enumerate(vals):
            grid.setItem(r, c, QTableWidgetItem(text))
        if grid.rowCount() > 200:
            grid.removeRow(0)
        if not info.get("mac_ok"):
            session.log("ERR", cid, data, "SecOC %s" % ("MAC" if info.get("ok") else info.get("reason")))

    send.clicked.connect(do_send)
    session.on_bus(on_frame)
    return page
