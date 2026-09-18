# -*- coding: utf-8 -*-
"""e2e-checksum — AUTOSAR E2E live CRC / alive-counter check.

Multi-rule CRC presets (P01/P02/P04/P05 + XOR), alive nibble check,
fail log + CSV export. Optional DBC import for CRC/Alive signal layout.
Read-only: never transmits.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QMessageBox, QHeaderView,
    QDialog, QDialogButtonBox, QFormLayout, QLineEdit, QComboBox,
    QSpinBox,
)

import sin
from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "e2e-checksum"

# CRC type: (width, poly, init, xorout)
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

_rules = {}
_errors = []
_summary = {"checked": 0, "crc_ok": 0, "crc_err": 0, "alive_err": 0}
_monitor = False
_running = True
_dbc_path = ""


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
    """Map DBC start bit to containing byte index (best-effort)."""
    if little_endian:
        return start_bit // 8
    # Motorola: start is MSB; still typically within start_bit//8
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
    """Build rule dicts from DBC messages that look like E2E layouts."""
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
            # high nibble if start_bit % 8 >= 4 for Intel-ish layouts
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


def _on_frame(frame):
    if not _running or not _monitor:
        return
    rule = _rules.get(frame.id)
    if rule is None:
        return
    _summary["checked"] += 1
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
        _summary["crc_ok"] += 1
        rule["ok"] = rule.get("ok", 0) + 1
        rule["stat_text"] = "OK"
    else:
        _summary["crc_err"] += 1
        rule["bad"] = rule.get("bad", 0) + 1
        rule["stat_text"] = "CRC fail"
        _errors.append((
            time.time(), frame.id, "CRC_error",
            "expected 0x%X actual 0x%X" % (expected, actual)))
    if rule["alive_byte"] < len(data):
        b = data[rule["alive_byte"]]
        cnt = (b >> 4) & 0xF if rule["alive_high"] else b & 0xF
        last = rule.get("alive_last")
        if last is not None:
            delta = (cnt - last) & 0xF
            if delta == 0:
                _summary["alive_err"] += 1
                _errors.append((
                    time.time(), frame.id, "alive_repeat",
                    "alive=%d did not increment" % cnt))
            elif delta != 1:
                _summary["alive_err"] += 1
                _errors.append((
                    time.time(), frame.id, "alive_jump",
                    "%d → %d (lost %d)" % (last, cnt, delta - 1)))
        rule["alive_last"] = cnt


def activate(context):
    global _monitor, _dbc_path, _running
    _running = True
    _monitor = False
    _rules.clear()
    del _errors[:]
    _summary.update({"checked": 0, "crc_ok": 0, "crc_err": 0, "alive_err": 0})
    _dbc_path = ""

    win = sin.ui.create_window("E2E Checksum")
    win.resize(980, 640)
    plugin_shell.attach_status_bar(
        win, "Add rules or import DBC — read-only check")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

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

    summary = QLabel(
        "Add rules (default E2E P01: CRC@byte0, alive@byte1 low nibble)")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

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

    saved = state_store.load_state(PLUGIN_ID, "settings.json", default={}) or {}
    if saved.get("dbc_path"):
        _dbc_path = str(saved["dbc_path"])
        dbc_lbl.setText("DBC: %s" % _dbc_path)
    for r in saved.get("rules", []):
        try:
            cid = int(r["id"], 0) if isinstance(r["id"], str) else int(r["id"])
        except (KeyError, ValueError, TypeError):
            continue
        ptype = r.get("crc_type", "CRC8 (SAE J1850, E2E P01)")
        if ptype not in _CRC_PRESETS:
            continue
        _rules[cid] = {
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
        for cid, r in _rules.items():
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
            "dbc_path": _dbc_path,
            "rules": rules_out,
        }, "settings.json")

    def _refresh_rules():
        rule_tree.clear()
        for cid, r in sorted(_rules.items()):
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

    def _on_add():
        dlg = QDialog(win)
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
            QMessageBox.warning(win, "Invalid ID", "ID must be hex")
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

        _rules[cid] = {
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
        global _dbc_path
        path = dbc_picker.pick_dbc(win, "Select DBC for E2E layout")
        if not path:
            return
        try:
            added = _rules_from_dbc(path)
        except Exception as e:
            QMessageBox.warning(win, "DBC error", str(e))
            return
        if not added:
            QMessageBox.information(
                win, "No E2E signals",
                "No CRC/Checksum-named signals found in DBC")
            return
        n = 0
        for mid, rule in added:
            _rules[mid] = rule
            n += 1
        _dbc_path = path
        dbc_lbl.setText("DBC: %s (%d rules)" % (path, n))
        _refresh_rules()
        _persist()
        plugin_shell.set_status(win, "Imported %d rules from DBC" % n, 5000)

    def _on_del():
        item = rule_tree.currentItem()
        if item is None:
            return
        try:
            cid = int(item.text(0), 0)
            _rules.pop(cid, None)
            _refresh_rules()
            _persist()
        except ValueError:
            pass

    def _on_start():
        global _monitor
        if not _rules:
            QMessageBox.information(win, "No rules", "Add or import rules first")
            return
        _monitor = True
        for r in _rules.values():
            r["alive_last"] = None
            r["stat_text"] = "checking"
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        _refresh_rules()
        plugin_shell.set_status(win, "Checking E2E")

    def _on_stop():
        global _monitor
        _monitor = False
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        for r in _rules.values():
            if r["stat_text"] == "checking":
                r["stat_text"] = "idle"
        _refresh_rules()
        summary.setText("Check stopped")
        plugin_shell.set_status(win, "Stopped")

    def _on_export():
        if not _errors:
            QMessageBox.information(win, "No data", "No fail records")
            return
        rows = []
        for ts, cid, kind, detail in _errors:
            rows.append([
                time.strftime("%H:%M:%S.%f", time.localtime(ts))[:-3],
                "0x%X" % cid, kind, detail,
            ])
        path = plugin_shell.export_csv(
            win, ["Time", "ID", "Kind", "Detail"], rows, "e2e_errors.csv")
        if path:
            plugin_shell.set_status(
                win, "Exported %d fails" % len(_errors), 5000)

    def _on_clear():
        del _errors[:]
        _summary.update({
            "checked": 0, "crc_ok": 0, "crc_err": 0, "alive_err": 0,
        })
        err_tree.clear()
        for r in _rules.values():
            r["alive_last"] = None
            r["ok"] = r["bad"] = 0
            r["stat_text"] = "idle"
        _refresh_rules()

    def _refresh():
        if _monitor:
            total = max(1, _summary["crc_ok"] + _summary["crc_err"])
            rate = 100.0 * _summary["crc_ok"] / total
            summary.setText(
                "Checking · %d frames · CRC pass %.2f%% (%d/%d) · "
                "alive errs %d · rules %d"
                % (_summary["checked"], rate, _summary["crc_ok"],
                   _summary["crc_ok"] + _summary["crc_err"],
                   _summary["alive_err"], len(_rules)))
            _refresh_rules()
        if _errors:
            err_tree.clear()
            colors = {
                "CRC_error": QColor("#c62828"),
                "alive_repeat": QColor("#ef6c00"),
                "alive_jump": QColor("#ef6c00"),
            }
            for ts, cid, kind, detail in _errors[-200:]:
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

    timer = QTimer()
    timer.timeout.connect(_refresh)
    timer.start(500)

    context.on_frame(_on_frame)
    context.register_command(
        "e2eChecksum.open", plugin_shell.bind_raise(win),
        "Security: E2E Checksum")

    win.show()
    sin.output.append("E2E Checksum loaded (multi-rule live check, read-only)")


def deactivate():
    global _running, _monitor
    _running = False
    _monitor = False
    sin.output.append("E2E Checksum deactivated")
