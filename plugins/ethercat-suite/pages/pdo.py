# -*- coding: utf-8 -*-
"""PDO process image for the selected slave."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLineEdit,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.pdo import flat_entries, pack_entries, unpack_entries
from widgets import combo, ghost, primary, table


def _hex(text: str) -> bytes:
    cleaned = "".join(text.split())
    if len(cleaned) % 2:
        cleaned = "0" + cleaned
    return bytes.fromhex(cleaned) if cleaned else b""


def build(_parent, session, _log):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    direction = combo(["Rx", "Tx"], "RxPDO is master outputs; TxPDO is slave inputs")
    preview = QLineEdit()
    preview.setFixedHeight(28)
    preview.setPlaceholderText("hex")
    preview.setToolTip("Packed process image, hex")
    pack = primary("Pack", "Pack the value column into the hex field")
    unpack = ghost("Unpack", "import", "Decode the hex field into the table")
    row = QHBoxLayout()
    row.addWidget(direction)
    row.addWidget(preview, 1)
    row.addWidget(pack)
    row.addWidget(unpack)
    root.addLayout(row)
    grid = table(["Name", "Index", "Bits", "Value"])
    root.addWidget(grid, 1)

    def entries():
        slave = session.current()
        if slave is None or slave.device is None:
            return []
        pdos = slave.device.rx_pdos if direction.currentText() == "Rx" else slave.device.tx_pdos
        return flat_entries(pdos)

    def refill():
        grid.setRowCount(0)
        for entry in entries():
            r = grid.rowCount()
            grid.insertRow(r)
            grid.setItem(r, 0, QTableWidgetItem(entry.name))
            grid.setItem(r, 1, QTableWidgetItem("0x%04X:%d" % (entry.index, entry.subindex)))
            grid.setItem(r, 2, QTableWidgetItem(str(entry.bit_len)))
            grid.setItem(r, 3, QTableWidgetItem("0"))
            grid.item(r, 3).setFlags(
                grid.item(r, 3).flags() | grid.item(r, 3).flags())

    def values():
        out = []
        for r in range(grid.rowCount()):
            item = grid.item(r, 3)
            text = item.text().strip() if item else "0"
            out.append(int(text, 0))
        return out

    def do_pack():
        try:
            blob = pack_entries(entries(), values())
        except ValueError:
            session.log("ERR", "-", b"", "PDO value is not an integer")
            return
        preview.setText(blob.hex(" ").upper())
        session.log("SYS", "-", blob, "PDO %s packed" % direction.currentText())

    def do_unpack():
        try:
            blob = _hex(preview.text())
        except ValueError:
            session.log("ERR", "-", b"", "PDO hex is invalid")
            return
        got = unpack_entries(blob, entries())
        for r, (_entry, value) in enumerate(got):
            if grid.item(r, 3):
                grid.item(r, 3).setText(str(value))

    # Value cells stay editable.
    from PyQt6.QtWidgets import QAbstractItemView
    grid.setEditTriggers(
        QAbstractItemView.EditTrigger.DoubleClicked
        | QAbstractItemView.EditTrigger.EditKeyPressed)

    direction.currentTextChanged.connect(lambda _t: refill())
    pack.clicked.connect(do_pack)
    unpack.clicked.connect(do_unpack)
    session.on_changed(refill)
    refill()
    return page
