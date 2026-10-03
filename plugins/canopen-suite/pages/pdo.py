# -*- coding: utf-8 -*-
"""Live PDO — mapping view + RPDO/TPDO payload unpack (DeviceExplorer slice).

Sidebar leaf when viz is present. One tool strip; empty_state when unmapped.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtWidgets import (
    QLabel,
    QStackedWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.cob_classify import classify_cob
from core.pdo_map import decode_mapping, mapping_indexes, pdo_label
from pages import _ui


def _parse_int(text):
    text = (text or "").strip()
    if not text:
        return None
    try:
        return int(text, 0)
    except ValueError:
        return None


def _unpack_bits(payload: bytes, bit_offset: int, bit_length: int) -> int:
    """Little-endian bitfield extract from PDO payload."""
    raw = bytes(payload or b"")
    if not raw or bit_length <= 0:
        return 0
    acc = int.from_bytes(raw.ljust(8, b"\x00")[:8], "little")
    mask = (1 << bit_length) - 1
    return (acc >> bit_offset) & mask


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    status = QLabel("Idle")
    status.setObjectName("SuiteCount")
    status.setToolTip("Live mapping + last RX values for shared Node-ID")
    eds_btn = _ui.ghost_btn(
        "From EDS", "Load RPDO/TPDO maps from the applied OD", "folder")
    read_btn = _ui.primary_btn(
        "Read node",
        "Expedited SDO read of 0x16xx / 0x1Axx on the shared Node-ID",
        "arrow-right")
    stop_btn = _ui.ghost_btn("Stop", "Cancel the SDO mapping read", "stop")
    open_map = _ui.ghost_btn(
        "EDS PDO map", "Edit mapping in the file-layer editor", "flow")
    layout.addWidget(_ui.tool_strip(
        eds_btn, read_btn, stop_btn, open_map, status, stretch_at=4))

    tree = QTreeWidget()
    tree.setHeaderLabels([
        "PDO", "Slot", "Object", "Name", "Bits", "Source", "Live",
    ])
    _ui.style_tree(tree, header_hidden=False)
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    _ui.configure_columns(tree, stretch=3)

    empty_eds_pdo = _ui.primary_btn(
        "EDS PDO map", "Edit mapping in the file-layer editor", "flow")
    empty_eds = _ui.ghost_btn(
        "From EDS", "Load maps from the applied OD", "folder")
    empty_read = _ui.ghost_btn(
        "Read node", "SDO-upload mapping objects from the bus", "arrow-right")
    empty = _ui.empty_state(
        "No live mapping yet",
        "Map in EDS PDO map, load from applied OD, or read the node — then watch Live.",
        actions=[empty_eds_pdo, empty_eds, empty_read])

    body = QStackedWidget()
    body.addWidget(empty)
    body.addWidget(tree)
    layout.addWidget(body, 1)

    # rows: (pdo_label, slot, index, sub, bits, name, source, bit_offset, pdo_kind)
    rows = []
    live_vals = {}  # (index, sub) -> display
    queue = []
    running = {"v": False}

    def _entries():
        return session.od_entries or session.draft_entries or []

    def _name(index, sub):
        for e in _entries():
            if e.index == index and e.subindex == sub:
                return e.name or ""
        return ""

    def _show_rows():
        if rows:
            body.setCurrentWidget(tree)
        else:
            body.setCurrentWidget(empty)

    def _refresh():
        tree.clear()
        for pdo, slot, index, sub, bits, name, source, _off, _kind in rows:
            live = live_vals.get((index, sub), "—")
            tree.addTopLevelItem(QTreeWidgetItem([
                pdo, str(slot), "0x%04X:%02X" % (index, sub),
                name, str(bits), source, live,
            ]))
        status.setText("%d slots · Node %d" % (len(rows), session.node_id))
        _ui.fit_columns(tree, stretch=3)
        _show_rows()

    def _add(pdo_index, slot, value, source):
        decoded = decode_mapping(value)
        if decoded is None:
            return
        index, sub, bits = decoded
        # Compute bit offset within this PDO from prior slots of same PDO
        bit_off = 0
        for r in rows:
            if r[0] == pdo_label(pdo_index):
                bit_off += int(r[4])
        kind = "TPDO" if 0x1A00 <= pdo_index <= 0x1BFF else "RPDO"
        rows.append((
            pdo_label(pdo_index), slot, index, sub, bits,
            _name(index, sub), source, bit_off, kind,
        ))

    def _from_eds():
        rows.clear()
        live_vals.clear()
        for e in _entries():
            if e.index not in mapping_indexes() or e.subindex == 0:
                continue
            value = _parse_int(e.default_value or e.parameter_value)
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
            log_fn(
                "SYS", session.node_id, b"",
                "SDO mapping read done (%d slots)" % len(rows))
            return
        index, sub, phase = queue.pop(0)
        status.setText("SDO 0x%04X:%02X" % (index, sub))

        def _done(ok, value, note):
            if ok and phase == "count":
                count = int(value or 0) & 0xFF
                for slot in range(1, min(count, 8) + 1):
                    queue.append((index, slot, "map"))
            elif ok and phase == "map":
                raw = value
                if isinstance(value, (bytes, bytearray)):
                    raw = int.from_bytes(bytes(value)[:4].ljust(4, b"\x00"), "little")
                _add(index, sub, int(raw or 0), "SDO")
            QTimer.singleShot(40, _next)

        if not session.sdo.upload(index, sub, _done):
            queue.insert(0, (index, sub, phase))
            QTimer.singleShot(80, _next)

    def _read_node():
        rows.clear()
        live_vals.clear()
        tree.clear()
        queue.clear()
        for index in mapping_indexes():
            queue.append((index, 0, "count"))
        running["v"] = True
        session.sdo.set_node(session.node_id)
        body.setCurrentWidget(tree)
        _next()

    def _open_map():
        if hasattr(parent, "goto_page"):
            parent.goto_page("eds_pdo")
        elif hasattr(parent, "run_action"):
            parent.run_action("view.pdo_map")

    def _on_bus(frame):
        if not rows:
            return
        kind, node, _lab = classify_cob(frame.id)
        if node != session.node_id:
            return
        if not (kind.startswith("TPDO") or kind.startswith("RPDO")):
            return
        data = bytes(frame.data) if frame.data else b""
        if not data:
            return
        changed = False
        for pdo, _slot, index, sub, bits, _name, _src, bit_off, _rk in rows:
            if pdo != kind and not pdo.startswith(kind):
                continue
            val = _unpack_bits(data, bit_off, bits)
            live_vals[(index, sub)] = "0x%X" % val
            changed = True
        if changed:
            _refresh()

    eds_btn.clicked.connect(_from_eds)
    empty_eds.clicked.connect(_from_eds)
    read_btn.clicked.connect(_read_node)
    empty_read.clicked.connect(_read_node)
    stop_btn.clicked.connect(_stop)
    open_map.clicked.connect(_open_map)
    empty_eds_pdo.clicked.connect(_open_map)
    session.on_od_changed(_from_eds)
    session.on_bus_frame(_on_bus)
    _from_eds()
    return root
