# -*- coding: utf-8 -*-
"""Fuzzer workspace — CAN ID-range mutation (rate-limited TX)."""

from __future__ import annotations

import random

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGroupBox,
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

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "bus-security"
SETTINGS_KEY = "fuzzer.json"


def _make_payload(mode, dlc, mask, state):
    if mode == 0:
        return bytes(random.getrandbits(8) for _ in range(dlc))
    if mode == 1:
        buf = bytearray(dlc)
        pos = random.randrange(dlc) if dlc else 0
        if dlc:
            buf[pos] = random.getrandbits(8)
            state["mutated_bytes"] += 1
        return bytes(buf)
    buf = bytearray(dlc)
    for i in range(dlc):
        m = mask[i] if mask and i < len(mask) else 0xFF
        buf[i] = random.getrandbits(8) & m
        if m:
            state["mutated_bytes"] += 1
    return bytes(buf)


def _range_row(id_min, id_max):
    box = QWidget()
    lay = QHBoxLayout(box)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.addWidget(id_min)
    lay.addWidget(QLabel("~"))
    lay.addWidget(id_max)
    return box


def _h(*widgets):
    box = QWidget()
    lay = QHBoxLayout(box)
    lay.setContentsMargins(0, 0, 0, 0)
    for w in widgets:
        lay.addWidget(w)
    lay.addStretch(1)
    return box


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    state = {"running": False, "sent": 0, "mutated_bytes": 0, "limit": 0}
    dist = {}

    warn = QLabel(
        "WARNING: Fuzzing transmits random frames on the bus. "
        "Use only on a test bench or isolated network. Unauthorized "
        "fuzzing may damage ECUs or violate policy.")
    warn.setWordWrap(True)
    warn.setStyleSheet(
        "background:#ffebee;color:#c62828;border:1px solid #c62828;"
        "padding:8px;font-weight:bold;")
    layout.addWidget(warn)

    cfg = QGroupBox("Fuzz settings")
    cfg_l = QFormLayout(cfg)
    id_min = QLineEdit("0x100")
    id_max = QLineEdit("0x1FF")
    cfg_l.addRow("ID range:", _range_row(id_min, id_max))
    ext_chk = QCheckBox("Extended (29-bit)")
    fd_chk = QCheckBox("CAN FD")
    cfg_l.addRow("Frame flags:", _h(ext_chk, fd_chk))
    dlc_spin = QSpinBox()
    dlc_spin.setRange(1, 64)
    dlc_spin.setValue(8)
    cfg_l.addRow("Data length:", dlc_spin)
    mode_combo = QComboBox()
    mode_combo.addItems([
        "Full random payload",
        "Single-byte flip",
        "Structured template (mask)",
    ])
    cfg_l.addRow("Mutation mode:", mode_combo)
    mask_edit = QLineEdit("FF FF FF FF 00 00 00 00")
    mask_edit.setPlaceholderText(
        "Mask: 0=keep byte 0, 1=randomize (e.g. FF FF FF FF 00 00 00 00)")
    cfg_l.addRow("Structured mask:", mask_edit)
    interval_spin = QSpinBox()
    interval_spin.setRange(1, 10000)
    interval_spin.setValue(session.default_interval_ms)
    interval_spin.setSuffix(" ms")
    cfg_l.addRow("Send interval:", interval_spin)
    limit_spin = QSpinBox()
    limit_spin.setRange(0, 1000000)
    limit_spin.setValue(session.default_send_limit)
    limit_spin.setSpecialValueText("unlimited")
    cfg_l.addRow("Send limit:", limit_spin)
    layout.addWidget(cfg)

    layout.addWidget(plugin_shell.help_label(
        "Rate limit = send interval. Suite strip defaults apply on Apply. "
        "Stop / Stop All aborts immediately. Stats export is CSV per-ID."))

    btns = QHBoxLayout()
    start_btn = QPushButton("Start fuzzing")
    stop_btn = QPushButton("Stop")
    export_btn = QPushButton("Export stats CSV…")
    clear_btn = QPushButton("Clear stats")
    btns.addWidget(start_btn)
    btns.addWidget(stop_btn)
    btns.addStretch(1)
    btns.addWidget(export_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)
    stop_btn.setEnabled(False)

    stats = QLabel("Ready (no frames sent)")
    stats.setStyleSheet("font-weight:bold;")
    layout.addWidget(stats)

    tree = QTreeWidget()
    tree.setHeaderLabels(["ID", "Sent", "Share %", "Last payload"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    timer = QTimer(root)
    refresher = QTimer(root)

    saved = state_store.load_state(PLUGIN_ID, SETTINGS_KEY, default={}) or {}
    if saved.get("id_min"):
        id_min.setText(str(saved["id_min"]))
    if saved.get("id_max"):
        id_max.setText(str(saved["id_max"]))
    if "interval_ms" in saved:
        interval_spin.setValue(int(saved["interval_ms"]))
    if "limit" in saved:
        limit_spin.setValue(int(saved["limit"]))
    if "mode" in saved:
        mode_combo.setCurrentIndex(int(saved["mode"]))
    if saved.get("mask"):
        mask_edit.setText(str(saved["mask"]))
    if "dlc" in saved:
        dlc_spin.setValue(int(saved["dlc"]))
    ext_chk.setChecked(bool(saved.get("extended", False)))
    fd_chk.setChecked(bool(saved.get("fd", False)))

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "id_min": id_min.text().strip(),
            "id_max": id_max.text().strip(),
            "interval_ms": interval_spin.value(),
            "limit": limit_spin.value(),
            "mode": mode_combo.currentIndex(),
            "mask": mask_edit.text().strip(),
            "dlc": dlc_spin.value(),
            "extended": ext_chk.isChecked(),
            "fd": fd_chk.isChecked(),
        }, SETTINGS_KEY)

    def _parse_id(text):
        try:
            v = int(text, 0)
            if 0 <= v <= 0x1FFFFFFF:
                return v
        except ValueError:
            pass
        return None

    def _parse_mask():
        try:
            toks = mask_edit.text().replace(" ", "").strip()
            if not toks:
                return None
            if len(toks) % 2:
                toks = "0" + toks
            return bytes.fromhex(toks)
        except ValueError:
            return b""

    def _set_running(running):
        state["running"] = running
        start_btn.setEnabled(not running)
        stop_btn.setEnabled(running)
        for w in (id_min, id_max, mode_combo, dlc_spin, ext_chk, fd_chk,
                  limit_spin, mask_edit):
            w.setEnabled(not running)

    def _on_stop():
        if not state["running"]:
            return
        state["running"] = False
        timer.stop()
        _set_running(False)
        stats.setText("Stopped · sent %d frames" % state["sent"])
        plugin_shell.set_status(parent, "Fuzzer stopped")
        log_fn("Fuzz", "Stopped · sent %d" % state["sent"])

    def _tick():
        if not state["running"]:
            return
        id_lo = _parse_id(id_min.text())
        id_hi = _parse_id(id_max.text())
        if id_lo is None or id_hi is None or id_lo > id_hi:
            _on_stop()
            plugin_shell.set_status(parent, "Fuzzer stopped: bad ID range")
            return
        dlc = dlc_spin.value()
        if fd_chk.isChecked():
            dlc = min(64, max(0, dlc))
        mask = _parse_mask() if mode_combo.currentIndex() == 2 else None
        if mask == b"":
            _on_stop()
            stats.setText("Mask parse error — stopped")
            return
        cid = random.randint(id_lo, id_hi)
        payload = _make_payload(mode_combo.currentIndex(), dlc, mask, state)
        try:
            sin.frames.send(cid, payload, ext_chk.isChecked(), fd_chk.isChecked())
        except Exception as e:
            stats.setText("Send error: %s" % e)
            plugin_shell.set_status(parent, "Send error")
            log_fn("ERR", "Fuzzer send error: %s" % e, "ERR")
            return
        state["sent"] += 1
        dist[cid] = dist.get(cid, 0) + 1
        dist["_last"] = payload.hex().upper()
        if state["limit"] and state["sent"] >= state["limit"]:
            _on_stop()
            stats.setText("Send limit %d reached — stopped" % state["limit"])
            plugin_shell.set_status(parent, "Fuzzer limit reached", 5000)

    def _on_start():
        id_lo = _parse_id(id_min.text())
        id_hi = _parse_id(id_max.text())
        if id_lo is None or id_hi is None or id_lo > id_hi:
            QMessageBox.warning(
                parent, "Invalid range",
                "ID range must be hex with min <= max")
            return
        if mode_combo.currentIndex() == 2:
            mask = _parse_mask()
            if mask == b"":
                QMessageBox.warning(
                    parent, "Invalid mask",
                    "Structured mask must be a hex byte string")
                return
        if hasattr(parent, "confirm_danger"):
            ok = parent.confirm_danger(
                "Confirm fuzzing",
                "This will transmit mutated frames on the connected bus.\n"
                "Continue only on a safe test bench.")
        else:
            ok = QMessageBox.warning(
                parent, "Confirm fuzzing",
                "This will transmit mutated frames on the connected bus.\n"
                "Continue only on a safe test bench.",
                QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Cancel) == QMessageBox.StandardButton.Ok
        if not ok:
            return
        _persist()
        state["limit"] = limit_spin.value()
        _set_running(True)
        timer.start(interval_spin.value())
        stats.setText("Fuzzing…")
        plugin_shell.set_status(
            parent, "Fuzzing at %d ms interval" % interval_spin.value())
        log_fn("Fuzz", "Started · interval %d ms · limit %s" % (
            interval_spin.value(),
            "unlimited" if state["limit"] == 0 else str(state["limit"])), "TX")

    def _on_export():
        rows_src = [(k, v) for k, v in dist.items() if k != "_last"]
        if not rows_src:
            QMessageBox.information(parent, "No data", "No stats to export")
            return
        rows = []
        for cid, n in sorted(rows_src, key=lambda kv: -kv[1]):
            rows.append([
                "0x%X" % cid, n,
                "%.2f" % (100.0 * n / max(1, state["sent"])),
            ])
        path = plugin_shell.export_csv(
            parent, ["ID", "Sent", "Share%"], rows, "fuzz_stats.csv")
        if path:
            plugin_shell.set_status(parent, "Exported %s" % path, 5000)

    def _on_clear():
        dist.clear()
        state["sent"] = 0
        state["mutated_bytes"] = 0
        tree.clear()
        stats.setText("Ready (stats cleared)")

    def _refresh():
        if state["running"]:
            stats.setText(
                "Sending · %d frames · %d mutated bytes · interval %d ms"
                % (state["sent"], state["mutated_bytes"],
                   interval_spin.value()))
        tree.clear()
        last = dist.get("_last", "")
        rows = [(k, v) for k, v in dist.items() if k != "_last"]
        if not rows:
            return
        top = max(rows, key=lambda kv: kv[1])[0]
        for cid, n in sorted(rows, key=lambda kv: -kv[1])[:100]:
            tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid, str(n),
                "%.2f" % (100.0 * n / max(1, state["sent"])),
                last if cid == top else "",
            ]))

    def _on_interval(v):
        if state["running"]:
            timer.start(v)

    def _on_defaults():
        if not state["running"]:
            interval_spin.setValue(session.default_interval_ms)
            limit_spin.setValue(session.default_send_limit)

    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)
    interval_spin.valueChanged.connect(_on_interval)

    timer.timeout.connect(_tick)
    refresher.timeout.connect(_refresh)
    refresher.start(500)

    session.register_stop_handler(_on_stop)
    session.on_defaults_changed(_on_defaults)
    return root
