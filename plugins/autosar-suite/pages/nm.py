# -*- coding: utf-8 -*-
"""AUTOSAR CanNm monitor and a one-shot NM transmit."""

from __future__ import annotations

from PyQt6.QtWidgets import QHBoxLayout, QTableWidgetItem, QVBoxLayout, QWidget

from core.cannm import decode_nm, encode_nm, nm_state_name
from widgets import ghost, primary, spin, spin_hex, table


def build(_parent, session, _log):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)

    base = spin_hex(0, 0x7FF, session.nm_base, "NM CAN id = base + node id")
    node = spin(0, 255, 1, "Source node id to transmit")
    row = QHBoxLayout()
    row.addWidget(base)
    row.addWidget(node)
    send = primary("Send NM", "Transmit one CanNm PDU (repeat + active wakeup)")
    clear = ghost("Clear", "clear", "Clear the node table")
    row.addWidget(send)
    row.addWidget(clear)
    row.addStretch(1)
    root.addLayout(row)

    grid = table(["CAN ID", "Node", "State", "Repeat", "Wakeup", "PNI", "User"])
    root.addWidget(grid, 1)
    rows = {}

    def on_base(value):
        session.nm_base = value

    def on_frame(cid, data):
        pdu = decode_nm(cid, data, session.nm_base)
        if pdu is None:
            return
        key = pdu.node_id
        if key not in rows:
            rows[key] = grid.rowCount()
            grid.insertRow(rows[key])
            session.log("SYS", cid, data, "NM node %d" % key)
        r = rows[key]
        vals = (
            "0x%X" % pdu.can_id,
            str(pdu.node_id),
            nm_state_name(pdu),
            "yes" if pdu.repeat_request else "no",
            "yes" if pdu.active_wakeup else "no",
            "yes" if pdu.pni else "no",
            pdu.user_data.hex(" ").upper(),
        )
        for c, text in enumerate(vals):
            grid.setItem(r, c, QTableWidgetItem(text))

    def do_send():
        cid, payload = encode_nm(
            node.value(), b"", repeat=True, active=True, base=session.nm_base)
        session.send(cid, payload, "CanNm node %d" % node.value())

    def do_clear():
        grid.setRowCount(0)
        rows.clear()

    base.valueChanged.connect(on_base)
    send.clicked.connect(do_send)
    clear.clicked.connect(do_clear)
    session.on_bus(on_frame)
    return page
