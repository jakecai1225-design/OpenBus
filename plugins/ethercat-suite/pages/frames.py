# -*- coding: utf-8 -*-
"""Captured EtherCAT frames: datagrams and CoE mailbox, as editor tabs."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QHBoxLayout,
    QPlainTextEdit,
    QStackedWidget,
    QTabBar,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.coe import parse_sdo, sdo_upload_request
from core.datagram import parse_frame
from widgets import ghost, primary, spin, table


def _hex_bytes(text: str) -> bytes:
    cleaned = "".join(text.replace(",", " ").split())
    return bytes.fromhex(cleaned) if cleaned else b""


def _datagrams(session):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    edit = QPlainTextEdit()
    edit.setPlaceholderText("EtherCAT hex, with or without Ethernet header")
    edit.setToolTip("Paste a capture. Type 0x88A4 is stripped when present.")
    decode = primary("Decode", "Split the frame into datagrams")
    row = QHBoxLayout()
    row.addStretch(1)
    row.addWidget(decode)
    root.addWidget(edit, 1)
    root.addLayout(row)
    grid = table(["Cmd", "ADP", "ADO", "Len", "WKC", "Data"])
    root.addWidget(grid, 2)

    def run():
        try:
            raw = _hex_bytes(edit.toPlainText())
        except ValueError:
            session.log("ERR", "-", b"", "Frame hex is invalid")
            return
        try:
            frame = parse_frame(raw)
        except ValueError as exc:
            session.log("ERR", "-", b"", str(exc))
            return
        grid.setRowCount(0)
        for dg in frame.datagrams:
            r = grid.rowCount()
            grid.insertRow(r)
            vals = (
                dg.cmd_name,
                str(dg.adp),
                "0x%04X" % dg.ado,
                str(len(dg.data)),
                str(dg.wkc),
                dg.data.hex(" ").upper(),
            )
            for c, text in enumerate(vals):
                grid.setItem(r, c, QTableWidgetItem(text))
        session.log("SYS", "-", raw, "%d datagram(s)" % len(frame.datagrams))

    decode.clicked.connect(run)
    return page


def _mailbox(session):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    index = spin(0, 0xFFFF, 0x1000, "Object index")
    index.setDisplayIntegerBase(16)
    index.setPrefix("0x")
    sub = spin(0, 255, 0, "Subindex")
    build = primary("Build upload", "Show a CoE SDO upload mailbox")
    parse = ghost("Parse", "search", "Parse the hex below as CoE SDO")
    row = QHBoxLayout()
    row.addWidget(index)
    row.addWidget(sub)
    row.addWidget(build)
    row.addWidget(parse)
    row.addStretch(1)
    root.addLayout(row)
    edit = QPlainTextEdit()
    edit.setPlaceholderText("mailbox hex")
    root.addWidget(edit, 1)

    def do_build():
        raw = sdo_upload_request(index.value(), sub.value())
        edit.setPlainText(raw.hex(" ").upper())
        session.log("SYS", "-", raw, "CoE upload 0x%04X:%d" % (index.value(), sub.value()))

    def do_parse():
        try:
            raw = _hex_bytes(edit.toPlainText())
        except ValueError:
            session.log("ERR", "-", b"", "Mailbox hex is invalid")
            return
        info = parse_sdo(raw)
        if not info.get("ok"):
            session.log("ERR", "-", raw, info.get("reason", "parse failed"))
            return
        session.log(
            "SYS", "-", raw,
            "cs 0x%02X  0x%04X:%d" % (info["cs"], info["index"], info["subindex"]))

    build.clicked.connect(do_build)
    parse.clicked.connect(do_parse)
    return page


def build(parent, session, _log):
    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    bar.addTab("Datagrams")
    bar.addTab("Mailbox")
    stack = QStackedWidget()
    stack.addWidget(_datagrams(session))
    stack.addWidget(_mailbox(session))
    bar.currentChanged.connect(stack.setCurrentIndex)
    parent._frame_tabs = bar
    return stack
