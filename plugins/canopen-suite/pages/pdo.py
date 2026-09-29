# -*- coding: utf-8 -*-
"""Live PDO — bus / applied-OD mapping view (Device menu power path).

Not in the Live sidebar (EDS → PDO map owns file mapping).
One tool strip + empty_state when nothing is mapped yet.
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


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    status = QLabel("Idle")
    status.setObjectName("SuiteCount")
    status.setToolTip("Live mapping (bus) — read node or load from applied OD")
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
    tree.setHeaderLabels(["PDO", "Slot", "Object", "Name", "Bits", "Source"])
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
        "No live mapping (bus) yet",
        "Map objects in EDS PDO map, load from the applied OD, or read the node.",
        actions=[empty_eds_pdo, empty_eds, empty_read])

    body = QStackedWidget()
    body.addWidget(empty)
    body.addWidget(tree)
    layout.addWidget(body, 1)

    rows = []
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
        for pdo, slot, index, sub, bits, name, source in rows:
            tree.addTopLevelItem(QTreeWidgetItem([
                pdo, str(slot), "0x%04X:%02X" % (index, sub),
                name, str(bits), source,
            ]))
        status.setText("%d slots · Node %d" % (len(rows), session.node_id))
        _ui.fit_columns(tree, stretch=3)
        _show_rows()

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
        body.setCurrentWidget(tree)
        _next()

    def _open_map():
        if hasattr(parent, "run_action"):
            parent.run_action("view.eds")  # fallback
        if hasattr(parent, "goto_page"):
            parent.goto_page("eds_pdo")

    def _open_dict():
        if hasattr(parent, "run_action"):
            parent.run_action("view.eds")

    eds_btn.clicked.connect(_from_eds)
    empty_eds.clicked.connect(_from_eds)
    read_btn.clicked.connect(_read_node)
    empty_read.clicked.connect(_read_node)
    stop_btn.clicked.connect(_stop)
    open_map.clicked.connect(_open_map)
    empty_eds_pdo.clicked.connect(_open_map)
    session.on_od_changed(_from_eds)
    _from_eds()
    return root
