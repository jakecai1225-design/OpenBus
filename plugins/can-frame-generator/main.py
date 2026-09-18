# -*- coding: utf-8 -*-
"""can-frame-generator — multi-row TX (PCAN-Explorer / CANoe IG style).

Per-row period/count schedulers, inline edit, signal panel, CAN-FD BRS/ESI.
"""

from __future__ import annotations

import json
import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QTreeWidget,
    QTreeWidgetItem, QTextEdit, QFileDialog, QMessageBox, QHeaderView,
    QGroupBox, QFormLayout, QLineEdit, QSpinBox, QCheckBox, QComboBox,
    QDialog, QDialogButtonBox, QAbstractItemView,
)

import sin
from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "can-frame-generator"

_rows = []
_dbc = None
_dbc_path = ""
_running = False
_next_due = {}  # row id -> next send monotonic time


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _parse_hex(text):
    text = text.strip().replace(" ", "").replace("0x", "").replace("0X", "")
    if not text:
        return b""
    if len(text) % 2:
        text = "0" + text
    try:
        return bytes.fromhex(text)
    except ValueError:
        return None


def _row_key(r):
    return id(r)


def activate(context):
    global _dbc, _dbc_path, _running
    _dbc = None
    _dbc_path = ""
    _running = False
    _rows.clear()
    _next_due.clear()

    win = sin.ui.create_window("CAN Frame Generator")
    win.resize(1100, 660)
    plugin_shell.attach_status_bar(win, "Idle — no TX until you start")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("Quick add (manual)")
    cfg_l = QFormLayout(cfg)
    id_edit = QLineEdit("0x123")
    data_edit = QLineEdit("01 02 03 04 05 06 07 08")
    cycle_spin = QSpinBox()
    cycle_spin.setRange(0, 60000)
    cycle_spin.setValue(100)
    cycle_spin.setSuffix(" ms")
    count_spin = QSpinBox()
    count_spin.setRange(0, 1000000)
    count_spin.setSpecialValueText("unlimited")
    count_spin.setValue(0)
    fd_chk = QCheckBox("CAN FD")
    brs_chk = QCheckBox("BRS")
    esi_chk = QCheckBox("ESI")
    ext_chk = QCheckBox("Extended ID")
    cfg_l.addRow("ID:", id_edit)
    cfg_l.addRow("Data hex:", data_edit)
    cfg_l.addRow("Period (0=once):", cycle_spin)
    cfg_l.addRow("Count limit:", count_spin)
    flags = QHBoxLayout()
    for w in (ext_chk, fd_chk, brs_chk, esi_chk):
        flags.addWidget(w)
    cfg_l.addRow("Flags:", flags)
    layout.addWidget(cfg)

    dbc_bar = QHBoxLayout()
    dbc_btn = QPushButton("Load DBC…")
    dbc_label = QLabel("No DBC (manual hex)")
    dbc_label.setStyleSheet("color:#888;")
    dbc_bar.addWidget(dbc_btn)
    dbc_bar.addWidget(dbc_label, 1)
    layout.addLayout(dbc_bar)

    btns = QHBoxLayout()
    add_btn = QPushButton("Add")
    add_dbc_btn = QPushButton("Add from DBC…")
    edit_btn = QPushButton("Edit signals…")
    del_btn = QPushButton("Delete")
    send_one_btn = QPushButton("Send selected")
    play_seq_btn = QPushButton("Play sequence once")
    start_btn = QPushButton("Start cyclic")
    stop_btn = QPushButton("Stop")
    save_btn = QPushButton("Save JSON")
    load_btn = QPushButton("Load JSON")
    for b in (add_btn, add_dbc_btn, edit_btn, del_btn, send_one_btn, play_seq_btn,
              start_btn, stop_btn, load_btn, save_btn):
        btns.addWidget(b)
    layout.addLayout(btns)
    layout.addWidget(plugin_shell.help_label(
        "Each row has its own period. Double-click cells to edit. Ctrl+S saves entries. "
        "FD/BRS/ESI columns are sent when supported by the host."))

    tree = QTreeWidget()
    tree.setHeaderLabels([
        "On", "ID", "Ext", "FD", "BRS", "ESI", "DLC", "Data", "Period ms",
        "Count", "Sent", "Name",
    ])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setEditTriggers(
        QAbstractItemView.EditTrigger.DoubleClicked
        | QAbstractItemView.EditTrigger.EditKeyPressed)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    log_view.setMaximumHeight(120)
    layout.addWidget(log_view)

    def _log(text):
        log_view.append("[%s] %s" % (time.strftime("%H:%M:%S"), text))

    def _refresh():
        tree.blockSignals(True)
        tree.clear()
        for r in _rows:
            item = QTreeWidgetItem([
                "Y" if r.get("enabled", True) else "N",
                "0x%X" % r["id"],
                "Y" if r.get("ext") else "N",
                "Y" if r.get("fd") else "N",
                "Y" if r.get("brs") else "N",
                "Y" if r.get("esi") else "N",
                str(r.get("dlc", len(r.get("data", b"")))),
                _hex(r.get("data", b"")),
                str(r.get("cycle", 0)),
                str(r.get("count", 0)),
                str(r.get("sent", 0)),
                r.get("name", ""),
            ])
            for col in range(item.columnCount()):
                item.setFlags(item.flags() | Qt.ItemFlag.ItemIsEditable)
            tree.addTopLevelItem(item)
        tree.blockSignals(False)

    def _apply_item_edit(item, col):
        idx = tree.indexOfTopLevelItem(item)
        if idx < 0 or idx >= len(_rows):
            return
        r = _rows[idx]
        text = item.text(col).strip()
        try:
            if col == 0:
                r["enabled"] = text.upper() in ("Y", "1", "YES", "ON", "√")
            elif col == 1:
                r["id"] = int(text, 0)
            elif col == 2:
                r["ext"] = text.upper() in ("Y", "1", "YES")
            elif col == 3:
                r["fd"] = text.upper() in ("Y", "1", "YES")
            elif col == 4:
                r["brs"] = text.upper() in ("Y", "1", "YES")
            elif col == 5:
                r["esi"] = text.upper() in ("Y", "1", "YES")
            elif col == 6:
                r["dlc"] = int(text)
            elif col == 7:
                data = _parse_hex(text)
                if data is not None:
                    r["data"] = data
                    r["dlc"] = len(data)
            elif col == 8:
                r["cycle"] = max(0, int(text))
            elif col == 9:
                r["count"] = max(0, int(text))
            elif col == 11:
                r["name"] = text
        except ValueError:
            pass
        _refresh()

    tree.itemChanged.connect(_apply_item_edit)

    def _send_row(r):
        kwargs = {"extended": r.get("ext", False), "fd": r.get("fd", False)}
        # Host may ignore unknown kwargs; try common names.
        try:
            sin.frames.send(
                r["id"], r["data"],
                extended=kwargs["extended"], fd=kwargs["fd"],
                brs=r.get("brs", False), esi=r.get("esi", False))
        except TypeError:
            sin.frames.send(r["id"], r["data"],
                            extended=kwargs["extended"], fd=kwargs["fd"])
        r["sent"] = r.get("sent", 0) + 1

    def _add_manual():
        try:
            cid = int(id_edit.text(), 0)
        except ValueError:
            QMessageBox.warning(win, "Format", "ID must be hex/int (e.g. 0x123)")
            return
        data = _parse_hex(data_edit.text())
        if data is None or len(data) > 64:
            QMessageBox.warning(win, "Format", "Invalid hex or >64 bytes")
            return
        _rows.append({
            "id": cid, "ext": ext_chk.isChecked(),
            "fd": fd_chk.isChecked() or len(data) > 8,
            "brs": brs_chk.isChecked(), "esi": esi_chk.isChecked(),
            "dlc": len(data), "data": data, "cycle": cycle_spin.value(),
            "count": count_spin.value(), "enabled": True, "sent": 0, "name": "",
        })
        _refresh()
        _log("Added 0x%X period=%dms" % (cid, cycle_spin.value()))

    def _on_load_dbc():
        global _dbc, _dbc_path
        path = dbc_picker.pick_dbc(win)
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "DBC", "No messages in file")
            return
        _dbc = db
        _dbc_path = path
        dbc_label.setText("%s (%d messages)" % (path.split("\\")[-1].split("/")[-1],
                                                 len(db.messages)))
        dbc_label.setStyleSheet("color:#2e7d32;")
        plugin_shell.set_status(win, "DBC loaded")

    def _signal_dialog(msg, initial=None):
        dlg = QDialog(win)
        dlg.setWindowTitle("Signal values — %s" % msg.name)
        dlg.resize(480, 520)
        dl = QVBoxLayout(dlg)
        from PyQt6.QtWidgets import QScrollArea, QFrame
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        frame = QFrame()
        fl = QVBoxLayout(frame)
        edits = {}
        for sig in msg.signals:
            row = QHBoxLayout()
            row.addWidget(QLabel("%s [%.2f..%.2f]%s" % (
                sig.name, sig.minimum, sig.maximum,
                (" " + sig.unit) if sig.unit else "")), 1)
            e = QLineEdit("%.2f" % (initial or {}).get(sig.name, sig.minimum))
            edits[sig.name] = e
            row.addWidget(e)
            fl.addLayout(row)
        scroll.setWidget(frame)
        dl.addWidget(scroll, 1)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                              QDialogButtonBox.StandardButton.Cancel)
        dl.addWidget(bb)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return None
        values = {}
        for sig in msg.signals:
            try:
                values[sig.name] = float(edits[sig.name].text())
            except ValueError:
                values[sig.name] = sig.minimum
        return values

    def _add_from_dbc():
        if _dbc is None:
            QMessageBox.information(win, "DBC", "Load a DBC first")
            return
        dlg = QDialog(win)
        dlg.setWindowTitle("Pick message")
        dl = QVBoxLayout(dlg)
        combo = QComboBox()
        for mid in sorted(_dbc.messages.keys()):
            m = _dbc.messages[mid]
            combo.addItem("0x%X %s" % (mid, m.name), mid)
        dl.addWidget(combo)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                              QDialogButtonBox.StandardButton.Cancel)
        dl.addWidget(bb)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        msg = _dbc.messages[combo.currentData()]
        values = _signal_dialog(msg)
        if values is None:
            return
        data = bytes(dbcparse.encode_message(msg, values, dlc=max(msg.dlc, 8)))
        _rows.append({
            "id": msg.can_id, "ext": msg.extended, "fd": False,
            "brs": False, "esi": False, "dlc": len(data), "data": data,
            "cycle": msg.cycle_time or 100, "count": 0, "enabled": True,
            "sent": 0, "name": msg.name, "signal_values": values,
            "msg_name": msg.name,
        })
        _refresh()

    def _edit_signals():
        sel = tree.selectedItems()
        if not sel or _dbc is None:
            QMessageBox.information(win, "Edit", "Select a DBC-backed row (load DBC first)")
            return
        idx = tree.indexOfTopLevelItem(sel[0])
        r = _rows[idx]
        msg = None
        for m in _dbc.messages.values():
            if m.can_id == r["id"]:
                msg = m
                break
        if not msg:
            QMessageBox.information(win, "Edit", "No DBC message for this ID")
            return
        values = _signal_dialog(msg, r.get("signal_values"))
        if values is None:
            return
        data = bytes(dbcparse.encode_message(msg, values, dlc=max(msg.dlc, 8)))
        r["data"] = data
        r["dlc"] = len(data)
        r["signal_values"] = values
        r["name"] = msg.name
        _refresh()
        _log("Re-encoded %s" % msg.name)

    def _selected_index():
        sel = tree.selectedItems()
        if not sel:
            return -1
        return tree.indexOfTopLevelItem(sel[0])

    def _on_send_selected():
        idx = _selected_index()
        if idx < 0:
            return
        _send_row(_rows[idx])
        _log("TX 0x%X %s" % (_rows[idx]["id"], _hex(_rows[idx]["data"])))
        _refresh()

    def _on_play_sequence():
        n = 0
        for r in _rows:
            if r.get("enabled", True):
                _send_row(r)
                n += 1
        _log("Sequence: %d frames" % n)
        _refresh()

    tick = QTimer()

    def _on_tick():
        now = time.monotonic()
        for r in _rows:
            if not r.get("enabled", True) or not r.get("cycle"):
                continue
            lim = r.get("count", 0)
            if lim and r.get("sent", 0) >= lim:
                continue
            key = _row_key(r)
            due = _next_due.get(key, 0)
            if now >= due:
                _send_row(r)
                _next_due[key] = now + (r["cycle"] / 1000.0)
        _refresh()

    def _on_start():
        global _running
        if not any(r.get("cycle") and r.get("enabled", True) for r in _rows):
            QMessageBox.information(win, "Cyclic", "No enabled rows with period > 0")
            return
        _running = True
        now = time.monotonic()
        for r in _rows:
            if r.get("cycle") and r.get("enabled", True):
                _next_due[_row_key(r)] = now
        tick.timeout.connect(_on_tick)
        tick.start(5)
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        plugin_shell.set_status(win, "Cyclic TX running")
        _log("Cyclic start (per-row schedule)")

    def _on_stop():
        global _running
        _running = False
        tick.stop()
        try:
            tick.timeout.disconnect(_on_tick)
        except TypeError:
            pass
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        plugin_shell.set_status(win, "Stopped")
        _log("Cyclic stop")

    def _on_del():
        idx = _selected_index()
        if idx >= 0:
            r = _rows.pop(idx)
            _next_due.pop(_row_key(r), None)
            _refresh()

    def _serialize():
        return [{
            "id": r["id"], "ext": r.get("ext", False), "fd": r.get("fd", False),
            "brs": r.get("brs", False), "esi": r.get("esi", False),
            "dlc": r.get("dlc", 8), "data": r["data"].hex(),
            "cycle": r.get("cycle", 0), "count": r.get("count", 0),
            "enabled": r.get("enabled", True), "name": r.get("name", ""),
            "signal_values": r.get("signal_values", {}),
        } for r in _rows]

    def _load_list(data):
        _rows.clear()
        _next_due.clear()
        for d in data:
            _rows.append({
                "id": int(d["id"]), "ext": bool(d.get("ext")),
                "fd": bool(d.get("fd")), "brs": bool(d.get("brs")),
                "esi": bool(d.get("esi")), "dlc": int(d.get("dlc", 8)),
                "data": bytes.fromhex(d.get("data", "")),
                "cycle": int(d.get("cycle", 0)), "count": int(d.get("count", 0)),
                "enabled": bool(d.get("enabled", True)), "sent": 0,
                "name": d.get("name", ""),
                "signal_values": d.get("signal_values", {}),
            })
        _refresh()

    def _on_save():
        path = state_store.save_state(PLUGIN_ID, _serialize(), "entries.json")
        also, _ = QFileDialog.getSaveFileName(
            win, "Also export JSON?", "tx_entries.json", "JSON (*.json)")
        if also:
            with open(also, "w", encoding="utf-8") as f:
                json.dump(_serialize(), f, indent=2)
        plugin_shell.set_status(win, "Saved %s" % path, 4000)
        _log("Saved %d entries" % len(_rows))

    def _on_load():
        data = state_store.load_state(PLUGIN_ID, "entries.json")
        path, _ = QFileDialog.getOpenFileName(win, "Load JSON", "", "JSON (*.json)")
        if path:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        if not data:
            QMessageBox.information(win, "Load", "No entries found")
            return
        _load_list(data)
        _log("Loaded %d entries" % len(_rows))

    context.register_command(
        "canFrameGenerator.open", plugin_shell.bind_raise(win), "Tool: Frame Generator")

    add_btn.clicked.connect(_add_manual)
    dbc_btn.clicked.connect(_on_load_dbc)
    add_dbc_btn.clicked.connect(_add_from_dbc)
    edit_btn.clicked.connect(_edit_signals)
    del_btn.clicked.connect(_on_del)
    send_one_btn.clicked.connect(_on_send_selected)
    play_seq_btn.clicked.connect(_on_play_sequence)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    save_btn.clicked.connect(_on_save)
    load_btn.clicked.connect(_on_load)
    stop_btn.setEnabled(False)
    plugin_shell.bind_shortcut(win, "Ctrl+S", _on_save)

    saved = state_store.load_state(PLUGIN_ID, "entries.json")
    if saved:
        _load_list(saved)
    else:
        _rows.append({
            "id": 0x123, "ext": False, "fd": False, "brs": False, "esi": False,
            "dlc": 8, "data": bytes(range(1, 9)), "cycle": 100, "count": 0,
            "enabled": False, "sent": 0, "name": "sample (disabled)",
        })
        _refresh()

    win.show()
    sin.output.append("can-frame-generator ready (per-row schedule, FD flags)")


def deactivate():
    global _running
    _running = False
    sin.output.append("can-frame-generator deactivated")
