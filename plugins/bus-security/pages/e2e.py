# -*- coding: utf-8 -*-
"""E2E workspace — AUTOSAR E2E CRC / alive-counter check (read-only)."""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import dbc_picker, dbcparse, plugin_shell, state_store

PLUGIN_ID = "bus-security"
SETTINGS_KEY = "e2e.json"

_CRC_PRESETS = {
    "CRC8 (SAE J1850, E2E P01)": (8, 0x1D, 0xFF, 0xFF),
    "CRC8H2F (E2E P02)": (8, 0x2F, 0xFF, 0xFF),
    "CRC16-CCITT (E2E P05)": (16, 0x1021, 0xFFFF, 0x00),
    "CRC32 (E2E P04, MPEG-2)": (32, 0x04C11DB7, 0xFFFFFFFF, 0x00000000),
    "XOR-8 checksum": (8, 0x00, 0x00, 0x00),
}

_CRC_NAME_HINTS = (
    "checksum", "crc", "crc8", "crc16", "crc32", "e2e_crc", "e2ecrc",
)
_ALIVE_NAME_HINTS = (
    "alive", "alivecounter", "alive_counter", "counter", "msgcounter",
    "msg_counter", "sequencenumber", "seqcounter",
)


def _crc_compute(data, preset_name, init_override=None, xor_override=None):
    if preset_name == "XOR-8 checksum":
        acc = 0
        for b in data:
            acc ^= b
        return acc
    width, poly, init, xorout = _CRC_PRESETS[preset_name]
    if init_override is not None:
        init = init_override & ((1 << width) - 1)
    if xor_override is not None:
        xorout = xor_override & ((1 << width) - 1)
    reg = init
    mask = (1 << width) - 1
    for b in data:
        reg ^= b << (width - 8)
        for _ in range(8):
            if reg & (1 << (width - 1)):
                reg = ((reg << 1) ^ poly) & mask
            else:
                reg = (reg << 1) & mask
    return (reg ^ xorout) & mask


def _crc_extract(data, offset, width):
    if width == 8:
        return data[offset] if offset < len(data) else None
    if width == 16:
        if offset + 1 < len(data):
            return (data[offset] << 8) | data[offset + 1]
        return None
    if width == 32:
        if offset + 3 < len(data):
            return ((data[offset] << 24) | (data[offset + 1] << 16) |
                    (data[offset + 2] << 8) | data[offset + 3])
        return None
    return None


def _bit_to_byte(start_bit, little_endian):
    return start_bit // 8


def _guess_preset(bit_length, name_l):
    if "xor" in name_l:
        return "XOR-8 checksum"
    if bit_length <= 8 or "crc8" in name_l or "j1850" in name_l:
        if "h2f" in name_l or "p02" in name_l:
            return "CRC8H2F (E2E P02)"
        return "CRC8 (SAE J1850, E2E P01)"
    if bit_length <= 16 or "crc16" in name_l or "p05" in name_l:
        return "CRC16-CCITT (E2E P05)"
    return "CRC32 (E2E P04, MPEG-2)"


def _hint_match(name, hints):
    n = name.lower().replace("-", "").replace("_", "")
    for h in hints:
        h2 = h.replace("_", "")
        if h2 in n or n in h2:
            return True
    return False


def _rules_from_dbc(path):
    db = dbcparse.parse_file(path)
    added = []
    for mid, msg in db.messages.items():
        crc_sig = None
        alive_sig = None
        for sig in msg.signals:
            if crc_sig is None and _hint_match(sig.name, _CRC_NAME_HINTS):
                crc_sig = sig
            if alive_sig is None and _hint_match(sig.name, _ALIVE_NAME_HINTS):
                alive_sig = sig
        if crc_sig is None:
            continue
        name_l = crc_sig.name.lower()
        preset = _guess_preset(crc_sig.bit_length, name_l)
        width = _CRC_PRESETS[preset][0]
        crc_off = _bit_to_byte(crc_sig.start_bit, crc_sig.little_endian)
        alive_byte = 1
        alive_high = False
        if alive_sig is not None:
            alive_byte = _bit_to_byte(alive_sig.start_bit, alive_sig.little_endian)
            if alive_sig.little_endian:
                alive_high = (alive_sig.start_bit % 8) >= 4
            else:
                alive_high = (7 - (alive_sig.start_bit % 8)) < 4
        data_start = 0
        data_end = max(msg.dlc, crc_off + width // 8)
        rule = {
            "crc_type": preset, "width": width,
            "crc_off": crc_off,
            "data_start": data_start,
            "data_end": data_end,
            "alive_byte": alive_byte,
            "alive_high": alive_high,
            "init": None, "xor": None,
            "alive_last": None, "stat_text": "idle",
            "ok": 0, "bad": 0,
            "name": msg.name,
        }
        added.append((mid, rule))
    return added


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    rules = {}
    errors = []
    summary = {"checked": 0, "crc_ok": 0, "crc_err": 0, "alive_err": 0}
    monitor = {"on": False}
    dbc_path = {"path": ""}

    layout.addWidget(plugin_shell.help_label(
        "Default P01 layout: CRC @ byte0, alive @ byte1 low nibble, "
        "protected bytes after. Import DBC to seed rules from signals "
        "named CRC/Checksum/AliveCounter. Monitoring is receive-only."))

    btns = QHBoxLayout()
    add_btn = QPushButton("Add rule…")
    dbc_btn = QPushButton("Import from DBC…")
    del_btn = QPushButton("Delete selected")
    start_btn = QPushButton("Start check")
    stop_btn = QPushButton("Stop")
    export_btn = QPushButton("Export fail log CSV…")
    clear_btn = QPushButton("Clear results")
    for w in (add_btn, dbc_btn, del_btn, start_btn, stop_btn):
        btns.addWidget(w)
    btns.addStretch(1)
    btns.addWidget(export_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)
    stop_btn.setEnabled(False)

    status = QLabel(
        "Add rules (default E2E P01: CRC@byte0, alive@byte1 low nibble)")
    status.setStyleSheet("font-weight:bold;")
    layout.addWidget(status)

    dbc_lbl = QLabel("DBC: (none)")
    dbc_lbl.setStyleSheet("color:#607d8b;")
    layout.addWidget(dbc_lbl)

    rule_tree = QTreeWidget()
    rule_tree.setHeaderLabels([
        "ID", "Name", "CRC type", "CRC pos", "Protect", "Alive", "Status",
    ])
    rule_tree.setRootIsDecorated(False)
    rule_tree.setAlternatingRowColors(True)
    rule_tree.setMaximumHeight(180)
    layout.addWidget(rule_tree)

    err_tree = QTreeWidget()
    err_tree.setHeaderLabels(["Time", "ID", "Kind", "Detail"])
    err_tree.setRootIsDecorated(False)
    err_tree.setAlternatingRowColors(True)
    err_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(err_tree, 1)

    saved = state_store.load_state(PLUGIN_ID, SETTINGS_KEY, default={}) or {}
    if saved.get("dbc_path"):
        dbc_path["path"] = str(saved["dbc_path"])
        dbc_lbl.setText("DBC: %s" % dbc_path["path"])
    for r in saved.get("rules", []):
        try:
            cid = int(r["id"], 0) if isinstance(r["id"], str) else int(r["id"])
        except (KeyError, ValueError, TypeError):
            continue
        ptype = r.get("crc_type", "CRC8 (SAE J1850, E2E P01)")
        if ptype not in _CRC_PRESETS:
            continue
        rules[cid] = {
            "crc_type": ptype,
            "width": _CRC_PRESETS[ptype][0],
            "crc_off": int(r.get("crc_off", 0)),
            "data_start": int(r.get("data_start", 2)),
            "data_end": int(r.get("data_end", 8)),
            "alive_byte": int(r.get("alive_byte", 1)),
            "alive_high": bool(r.get("alive_high", False)),
            "init": r.get("init"),
            "xor": r.get("xor"),
            "alive_last": None,
            "stat_text": "idle",
            "ok": 0, "bad": 0,
            "name": r.get("name", ""),
        }

    def _persist():
        rules_out = []
        for cid, r in rules.items():
            rules_out.append({
                "id": "0x%X" % cid,
                "crc_type": r["crc_type"],
                "crc_off": r["crc_off"],
                "data_start": r["data_start"],
                "data_end": r["data_end"],
                "alive_byte": r["alive_byte"],
                "alive_high": r["alive_high"],
                "init": r["init"],
                "xor": r["xor"],
                "name": r.get("name", ""),
            })
        state_store.save_state(PLUGIN_ID, {
            "dbc_path": dbc_path["path"],
            "rules": rules_out,
        }, SETTINGS_KEY)

    def _refresh_rules():
        rule_tree.clear()
        for cid, r in sorted(rules.items()):
            rule_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid,
                r.get("name", ""),
                r["crc_type"],
                "byte%d (%dbit)" % (r["crc_off"], r["width"]),
                "byte%d-%d" % (r["data_start"], r["data_end"]),
                "byte%d %s" % (
                    r["alive_byte"],
                    "high" if r["alive_high"] else "low"),
                r["stat_text"],
            ]))

    _refresh_rules()

    def _on_frame(frame):
        if not monitor["on"]:
            return
        rule = rules.get(frame.id)
        if rule is None:
            return
        summary["checked"] += 1
        data = frame.data
        width = rule["width"]
        off = rule["crc_off"]
        start, end = rule["data_start"], min(rule["data_end"], len(data))
        covered = bytearray()
        nbytes = width // 8
        for i in range(start, end):
            if off <= i < off + nbytes:
                continue
            covered.append(data[i])
        if len(covered) < 1 or end <= start:
            return
        expected = _crc_compute(
            bytes(covered), rule["crc_type"], rule["init"], rule["xor"])
        actual = _crc_extract(data, off, width)
        if actual is None:
            return
        if actual == expected:
            summary["crc_ok"] += 1
            rule["ok"] = rule.get("ok", 0) + 1
            rule["stat_text"] = "OK"
        else:
            summary["crc_err"] += 1
            rule["bad"] = rule.get("bad", 0) + 1
            rule["stat_text"] = "CRC fail"
            errors.append((
                time.time(), frame.id, "CRC_error",
                "expected 0x%X actual 0x%X" % (expected, actual)))
            log_fn("E2E", "CRC fail ID 0x%X" % frame.id, "ERR")
        if rule["alive_byte"] < len(data):
            b = data[rule["alive_byte"]]
            cnt = (b >> 4) & 0xF if rule["alive_high"] else b & 0xF
            last = rule.get("alive_last")
            if last is not None:
                delta = (cnt - last) & 0xF
                if delta == 0:
                    summary["alive_err"] += 1
                    errors.append((
                        time.time(), frame.id, "alive_repeat",
                        "alive=%d did not increment" % cnt))
                elif delta != 1:
                    summary["alive_err"] += 1
                    errors.append((
                        time.time(), frame.id, "alive_jump",
                        "%d → %d (lost %d)" % (last, cnt, delta - 1)))
            rule["alive_last"] = cnt

    def _on_add():
        dlg = QDialog(parent)
        dlg.setWindowTitle("Add E2E check rule")
        dlg.resize(440, 360)
        form = QFormLayout(dlg)
        id_edit = QLineEdit("0x123")
        name_edit = QLineEdit("")
        preset = QComboBox()
        preset.addItems(list(_CRC_PRESETS.keys()))
        crc_off = QSpinBox()
        crc_off.setRange(0, 63)
        crc_off.setValue(0)
        data_start = QSpinBox()
        data_start.setRange(0, 63)
        data_start.setValue(2)
        data_end = QSpinBox()
        data_end.setRange(1, 64)
        data_end.setValue(8)
        alive_byte = QSpinBox()
        alive_byte.setRange(0, 63)
        alive_byte.setValue(1)
        alive_high = QComboBox()
        alive_high.addItems(["Low nibble", "High nibble"])
        init_edit = QLineEdit("")
        init_edit.setPlaceholderText("hex init override, empty=default")
        xor_edit = QLineEdit("")
        xor_edit.setPlaceholderText("hex xorout override, empty=default")
        form.addRow("Message ID:", id_edit)
        form.addRow("Name (optional):", name_edit)
        form.addRow("CRC type:", preset)
        form.addRow("CRC byte offset:", crc_off)
        form.addRow("Protect start byte:", data_start)
        form.addRow("Protect end byte (excl.):", data_end)
        form.addRow("Alive byte:", alive_byte)
        form.addRow("Alive nibble:", alive_high)
        form.addRow("Init override:", init_edit)
        form.addRow("Xorout override:", xor_edit)
        btns2 = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok |
            QDialogButtonBox.StandardButton.Cancel)
        btns2.accepted.connect(dlg.accept)
        btns2.rejected.connect(dlg.reject)
        form.addRow(btns2)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        try:
            cid = int(id_edit.text(), 0)
        except ValueError:
            QMessageBox.warning(parent, "Invalid ID", "ID must be hex")
            return
        ptype = preset.currentText()
        width = _CRC_PRESETS[ptype][0]

        def _opt(text):
            t = text.strip()
            if not t:
                return None
            try:
                return int(t, 16)
            except ValueError:
                return None

        rules[cid] = {
            "crc_type": ptype, "width": width,
            "crc_off": crc_off.value(),
            "data_start": data_start.value(),
            "data_end": max(data_end.value(), data_start.value() + 1),
            "alive_byte": alive_byte.value(),
            "alive_high": alive_high.currentIndex() == 1,
            "init": _opt(init_edit.text()),
            "xor": _opt(xor_edit.text()),
            "alive_last": None, "stat_text": "idle",
            "ok": 0, "bad": 0,
            "name": name_edit.text().strip(),
        }
        _refresh_rules()
        _persist()

    def _on_dbc():
        path = dbc_picker.pick_dbc(parent, "Select DBC for E2E layout")
        if not path:
            return
        try:
            added = _rules_from_dbc(path)
        except Exception as e:
            QMessageBox.warning(parent, "DBC error", str(e))
            return
        if not added:
            QMessageBox.information(
                parent, "No E2E signals",
                "No CRC/Checksum-named signals found in DBC")
            return
        n = 0
        for mid, rule in added:
            rules[mid] = rule
            n += 1
        dbc_path["path"] = path
        dbc_lbl.setText("DBC: %s (%d rules)" % (path, n))
        _refresh_rules()
        _persist()
        plugin_shell.set_status(parent, "Imported %d rules from DBC" % n, 5000)
        log_fn("E2E", "Imported %d rules from DBC" % n, "OK")

    def _on_del():
        item = rule_tree.currentItem()
        if item is None:
            return
        try:
            cid = int(item.text(0), 0)
            rules.pop(cid, None)
            _refresh_rules()
            _persist()
        except ValueError:
            pass

    def _on_start():
        if not rules:
            QMessageBox.information(parent, "No rules", "Add or import rules first")
            return
        monitor["on"] = True
        for r in rules.values():
            r["alive_last"] = None
            r["stat_text"] = "checking"
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        _refresh_rules()
        plugin_shell.set_status(parent, "Checking E2E")
        log_fn("E2E", "Check started · %d rules" % len(rules))

    def _on_stop():
        if not monitor["on"]:
            return
        monitor["on"] = False
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        for r in rules.values():
            if r["stat_text"] == "checking":
                r["stat_text"] = "idle"
        _refresh_rules()
        status.setText("Check stopped")
        plugin_shell.set_status(parent, "E2E stopped")
        log_fn("E2E", "Stopped")

    def _on_export():
        if not errors:
            QMessageBox.information(parent, "No data", "No fail records")
            return
        rows = []
        for ts, cid, kind, detail in errors:
            rows.append([
                time.strftime("%H:%M:%S.%f", time.localtime(ts))[:-3],
                "0x%X" % cid, kind, detail,
            ])
        path = plugin_shell.export_csv(
            parent, ["Time", "ID", "Kind", "Detail"], rows, "e2e_errors.csv")
        if path:
            plugin_shell.set_status(
                parent, "Exported %d fails" % len(errors), 5000)

    def _on_clear():
        del errors[:]
        summary.update({
            "checked": 0, "crc_ok": 0, "crc_err": 0, "alive_err": 0,
        })
        err_tree.clear()
        for r in rules.values():
            r["alive_last"] = None
            r["ok"] = r["bad"] = 0
            r["stat_text"] = "idle"
        _refresh_rules()

    def _refresh():
        if monitor["on"]:
            total = max(1, summary["crc_ok"] + summary["crc_err"])
            rate = 100.0 * summary["crc_ok"] / total
            status.setText(
                "Checking · %d frames · CRC pass %.2f%% (%d/%d) · "
                "alive errs %d · rules %d"
                % (summary["checked"], rate, summary["crc_ok"],
                   summary["crc_ok"] + summary["crc_err"],
                   summary["alive_err"], len(rules)))
            _refresh_rules()
        if errors:
            err_tree.clear()
            colors = {
                "CRC_error": QColor("#c62828"),
                "alive_repeat": QColor("#ef6c00"),
                "alive_jump": QColor("#ef6c00"),
            }
            for ts, cid, kind, detail in errors[-200:]:
                item = QTreeWidgetItem([
                    time.strftime("%H:%M:%S", time.localtime(ts)),
                    "0x%X" % cid, kind, detail,
                ])
                item.setForeground(2, colors.get(kind, QColor("#333")))
                err_tree.addTopLevelItem(item)
            sb = err_tree.verticalScrollBar()
            sb.setValue(sb.maximum())

    add_btn.clicked.connect(_on_add)
    dbc_btn.clicked.connect(_on_dbc)
    del_btn.clicked.connect(_on_del)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)

    timer = QTimer(root)
    timer.timeout.connect(_refresh)
    timer.start(500)

    session.on_bus_frame(_on_frame)
    session.register_stop_handler(_on_stop)
    return root
