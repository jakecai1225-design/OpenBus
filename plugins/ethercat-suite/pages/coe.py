# -*- coding: utf-8 -*-
"""CoE object list and SDO mailbox builder (bytes only — no NIC master)."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLineEdit,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.coe import parse_sdo, sdo_download_request, sdo_upload_request
from widgets import ghost, primary, spin, table


def build(_parent, session, _log):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)

    index = spin(0, 0xFFFF, 0x1000, "Object index")
    index.setDisplayIntegerBase(16)
    index.setPrefix("0x")
    sub = spin(0, 255, 0, "Subindex")
    value = spin(0, 0x7FFFFFFF, 0, "Download value (expedited)")
    size = spin(1, 4, 4, "Expedited size: 1, 2 or 4")
    upload = primary("Upload request", "Build a CoE SDO upload mailbox")
    download = ghost("Download request", "send", "Build a CoE SDO download mailbox")
    row = QHBoxLayout()
    for w in (index, sub, value, size, upload, download):
        row.addWidget(w)
    row.addStretch(1)
    root.addLayout(row)

    hex_line = QLineEdit()
    hex_line.setFixedHeight(28)
    hex_line.setPlaceholderText("mailbox hex")
    hex_line.setToolTip("Last mailbox, or paste one to parse")
    parse = ghost("Parse", "search", "Decode a mailbox hex string as SDO")
    line = QHBoxLayout()
    line.addWidget(hex_line, 1)
    line.addWidget(parse)
    root.addLayout(line)

    grid = table(["Index", "Name", "Type", "Bits"])
    root.addWidget(grid, 1)

    def refill():
        grid.setRowCount(0)
        slave = session.current()
        if slave is None or slave.device is None:
            return
        for obj in slave.device.objects:
            r = grid.rowCount()
            grid.insertRow(r)
            vals = ("0x%04X" % obj.index, obj.name, obj.type_name, str(obj.bit_size))
            for c, text in enumerate(vals):
                grid.setItem(r, c, QTableWidgetItem(text))

    def show(raw: bytes, note: str):
        hex_line.setText(raw.hex(" ").upper())
        session.log("SYS", "-", raw, note)

    def do_upload():
        raw = sdo_upload_request(index.value(), sub.value())
        show(raw, "SDO upload 0x%04X:%d" % (index.value(), sub.value()))

    def do_download():
        raw = sdo_download_request(index.value(), sub.value(), value.value(), size.value())
        show(raw, "SDO download 0x%04X:%d" % (index.value(), sub.value()))

    def do_parse():
        text = "".join(hex_line.text().split())
        try:
            raw = bytes.fromhex(text)
        except ValueError:
            session.log("ERR", "-", b"", "Mailbox hex is invalid")
            return
        info = parse_sdo(raw)
        if not info.get("ok"):
            session.log("ERR", "-", raw, "SDO parse: %s" % info.get("reason"))
            return
        index.setValue(info["index"])
        sub.setValue(info["subindex"])
        session.log(
            "SYS", "-", raw,
            "SDO cs 0x%02X idx 0x%04X:%d" % (info["cs"], info["index"], info["subindex"]))

    def on_row():
        item = grid.item(grid.currentRow(), 0)
        if item is None:
            return
        try:
            index.setValue(int(item.text(), 16))
        except ValueError:
            pass

    upload.clicked.connect(do_upload)
    download.clicked.connect(do_download)
    parse.clicked.connect(do_parse)
    grid.itemSelectionChanged.connect(on_row)
    session.on_changed(refill)
    refill()
    return page
