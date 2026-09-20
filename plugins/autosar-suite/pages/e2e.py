# -*- coding: utf-8 -*-
"""E2E Profile 1 protect / check. Failures go to OUTPUT; the table keeps history."""

from __future__ import annotations

from PyQt6.QtWidgets import QHBoxLayout, QTableWidgetItem, QVBoxLayout, QWidget

from core.e2e import p01_check, p01_protect
from widgets import combo, primary, spin, table


def build(_parent, session, _log):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)

    can_id = spin(0, 0x1FFFFFFF, session.e2e_can_id, "CAN id watched for Profile 1")
    can_id.setDisplayIntegerBase(16)
    can_id.setPrefix("0x")
    data_id = spin(0, 0xFFFF, session.e2e_data_id, "16-bit DataID")
    data_id.setDisplayIntegerBase(16)
    data_id.setPrefix("0x")
    mode = combo(["BOTH", "LOW", "ALT"], "DataID bytes included in the CRC")
    mode.setCurrentText(session.e2e_mode if session.e2e_mode in ("BOTH", "LOW", "ALT") else "BOTH")
    counter = spin(0, 14, 0, "Profile 1 counter nibble")
    send = primary("Protect and send", "CRC-8 SAE J1850 over DataID + payload, then send")

    row = QHBoxLayout()
    for w in (can_id, data_id, mode, counter, send):
        row.addWidget(w)
    row.addStretch(1)
    root.addLayout(row)

    grid = table(["CAN ID", "Counter", "CRC", "Expect", "Result"])
    root.addWidget(grid, 1)

    def pull():
        session.e2e_can_id = can_id.value()
        session.e2e_data_id = data_id.value()
        session.e2e_mode = mode.currentText()

    def do_send():
        pull()
        raw = p01_protect(b"\x00\x00\x11\x22\x00\x00\x00\x00",
                          session.e2e_data_id, counter.value(), session.e2e_mode)
        session.send(session.e2e_can_id, raw, "E2E P01 cnt %d" % counter.value())
        counter.setValue((counter.value() + 1) % 15)

    def on_frame(cid, data):
        if cid != session.e2e_can_id:
            return
        result = p01_check(data, session.e2e_data_id, session.e2e_mode)
        row_i = grid.rowCount()
        grid.insertRow(row_i)
        vals = (
            "0x%X" % cid,
            str(result.get("counter", "")),
            "0x%02X" % result.get("crc", 0),
            "0x%02X" % result.get("expect", 0) if "expect" in result else "-",
            "OK" if result.get("ok") else result.get("reason", "fail"),
        )
        for c, text in enumerate(vals):
            grid.setItem(row_i, c, QTableWidgetItem(text))
        if grid.rowCount() > 200:
            grid.removeRow(0)
        if not result.get("ok"):
            session.log("ERR", cid, data, "E2E %s" % result.get("reason"))

    for w in (can_id, data_id):
        w.valueChanged.connect(lambda _v: pull())
    mode.currentTextChanged.connect(lambda _t: pull())
    send.clicked.connect(do_send)
    session.on_bus(on_frame)
    return page
