# -*- coding: utf-8 -*-
"""PDO workspace — EDS mapping plus expedited SDO read of 0x16xx / 0x1Axx."""

from __future__ import annotations

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import vscode_theme
from core.pdo_map import decode_mapping, mapping_indexes, pdo_label


def _parse_int(text):
    text = (text or "").strip()
    if not text:
        return None
    try:
        return int(text, 0)
    except ValueError:
        return None


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "PDO mapping",
        "Shows RPDO/TPDO map objects. Read from node uses the shared Node-ID.")
    row = QHBoxLayout()
    eds_btn = QPushButton("Load from EDS")
    read_btn = QPushButton("Read from node")
    stop_btn = QPushButton("Stop")
    status = QLabel("Idle")
    row.addWidget(eds_btn)
    row.addWidget(read_btn)
    row.addWidget(stop_btn)
    row.addStretch(1)
    row.addWidget(status)
    body.addLayout(row)
    layout.addWidget(card)

    tree = QTreeWidget()
    tree.setHeaderLabels(["PDO", "Slot", "Object", "Name", "Bits", "Source"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    rows = []
    queue = []
    running = {"v": False}

    def _entries():
        return session.od_entries or []

    def _name(index, sub):
        for e in _entries():
            if e.index == index and e.subindex == sub:
                return e.name or ""
        return ""

    def _refresh():
        tree.clear()
        for pdo, slot, index, sub, bits, name, source in rows:
            tree.addTopLevelItem(QTreeWidgetItem([
                pdo, str(slot), "0x%04X:%02X" % (index, sub),
                name, str(bits), source,
            ]))
        status.setText("%d mapped slots" % len(rows))

    def _add(pdo_index, slot, value, source):
        decoded = decode_mapping(value)
        if decoded is None:
            return
        index, sub, bits = decoded
        rows.append((
            pdo_label(pdo_index), slot, index, sub, bits,
            _name(index, sub), source,
        ))

    def _from_eds():
        rows.clear()
        for e in _entries():
            if e.index not in mapping_indexes() or e.subindex == 0:
                continue
            value = _parse_int(e.default_value)
            if value is None:
                continue
            _add(e.index, e.subindex, value, "EDS")
        _refresh()
        log_fn("SYS", session.node_id, b"", "EDS mapping slots: %d" % len(rows))

    def _stop():
        running["v"] = False
        queue.clear()
        status.setText("Stopped")

    def _next():
        if not running["v"]:
            return
        if not queue:
            running["v"] = False
            _refresh()
            log_fn("SYS", session.node_id, b"", "SDO mapping read done (%d slots)" % len(rows))
            return
        index, sub, phase = queue.pop(0)
        status.setText("SDO 0x%04X:%02X" % (index, sub))

        def _done(ok, value, note):
            if ok and phase == "count":
                count = int(value or 0) & 0xFF
                for slot in range(1, min(count, 8) + 1):
                    queue.append((index, slot, "map"))
            elif ok and phase == "map":
                _add(index, sub, int(value or 0), "SDO")
            QTimer.singleShot(40, _next)

        if not session.sdo.upload(index, sub, _done):
            queue.insert(0, (index, sub, phase))
            QTimer.singleShot(80, _next)

    def _read_node():
        rows.clear()
        tree.clear()
        queue.clear()
        for index in mapping_indexes():
            queue.append((index, 0, "count"))
        running["v"] = True
        session.sdo.set_node(session.node_id)
        _next()

    eds_btn.clicked.connect(_from_eds)
    read_btn.clicked.connect(_read_node)
    stop_btn.clicked.connect(_stop)
    session.on_od_changed(_from_eds)
    _from_eds()
    return root
