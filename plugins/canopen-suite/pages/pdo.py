# -*- coding: utf-8 -*-
"""PDO mapping — one tool row, then the table. No card shell."""

from __future__ import annotations

from PyQt6.QtCore import Qt, QTimer
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
    layout.setContentsMargins(8, 6, 8, 6)
    layout.setSpacing(6)

    tools = QHBoxLayout()
    eds_btn = QPushButton("From EDS")
    eds_btn.setObjectName("GhostButton")
    eds_btn.setFixedHeight(28)
    eds_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    eds_btn.setToolTip("Load RPDO/TPDO map objects from the applied OD")
    read_btn = QPushButton("Read node")
    read_btn.setObjectName("PrimaryButton")
    read_btn.setFixedHeight(28)
    read_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    read_btn.setToolTip("Expedited SDO read of 0x16xx / 0x1Axx on the shared Node-ID")
    stop_btn = QPushButton("Stop")
    stop_btn.setObjectName("GhostButton")
    stop_btn.setFixedHeight(28)
    stop_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    status = QLabel("Idle")
    status.setObjectName("SuiteHint")
    tools.addWidget(eds_btn)
    tools.addWidget(read_btn)
    tools.addWidget(stop_btn)
    tools.addStretch(1)
    tools.addWidget(status)
    layout.addLayout(tools)

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
        status.setText("%d slots" % len(rows))

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
