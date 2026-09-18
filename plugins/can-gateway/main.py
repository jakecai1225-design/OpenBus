# -*- coding: utf-8 -*-
"""can-gateway — CAN frame gateway / forwarder.

Rule table: ID match (exact/mask) → remap / payload patch / rate-limit / pass-through.
Rules persist via state_store; start/stop forwarding; hit counters.
"""

from __future__ import annotations

import json
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
    QHeaderView, QLineEdit, QFormLayout, QGroupBox, QSpinBox,
)

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "can-gateway"

_rules = []
_active = False
_win = None


def _match_rule(frame):
    for r in _rules:
        if not r.get("enabled", True):
            continue
        mask = r.get("mask", 0x7FF)
        if (frame.id & mask) == (r["match"] & mask):
            rate = r.get("rate_limit_ms", 0)
            now = time.time() * 1000.0
            if rate and r.get("last_tx") and now - r["last_tx"] < rate:
                return None, r, True
            return r, r, False
    return None, None, False


def _on_frame(frame):
    if not _active:
        return
    r, _, limited = _match_rule(frame)
    if r is None:
        return
    r["hits"] = r.get("hits", 0) + 1
    r["last_ts"] = time.time()
    if limited:
        r["dropped"] = r.get("dropped", 0) + 1
        return
    new_id = r.get("new_id")
    out_id = new_id if (new_id is not None and new_id >= 0) else frame.id
    data = bytearray(frame.data)
    patch = r.get("patch")
    if patch:
        try:
            pos = int(patch.get("pos", 0))
            vals = bytes.fromhex(patch.get("hex", "").replace(" ", ""))
            data[pos:pos + len(vals)] = vals
        except (ValueError, IndexError):
            pass
    r["last_tx"] = time.time() * 1000.0
    sin.frames.send(out_id, bytes(data), extended=frame.extended, fd=frame.fd)


def _rules_for_persist():
    return [{
        "match": r["match"],
        "mask": r.get("mask", 0x7FF),
        "new_id": r.get("new_id"),
        "patch": r.get("patch"),
        "rate_limit_ms": r.get("rate_limit_ms", 0),
        "enabled": r.get("enabled", True),
    } for r in _rules]


def _load_rule_dicts(data):
    out = []
    for d in data:
        p = d.get("patch")
        patch_text = "-"
        if p:
            patch_text = "pos=%d,hex=%s" % (p.get("pos", 0), p.get("hex", ""))
        out.append({
            "match": int(d["match"]),
            "mask": int(d.get("mask", 0x7FF)),
            "new_id": d.get("new_id"),
            "patch": p,
            "patch_text": patch_text,
            "rate_limit_ms": int(d.get("rate_limit_ms", 0)),
            "enabled": bool(d.get("enabled", True)),
            "hits": 0,
            "dropped": 0,
        })
    return out


def activate(context):
    global _active, _win
    _active = False

    win = sin.ui.create_window("CAN Gateway")
    win.resize(1000, 620)
    plugin_shell.attach_status_bar(win, "Forwarding stopped")
    _win = win

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("Add rule")
    cfg_l = QFormLayout(cfg)
    match_edit = QLineEdit("0x100")
    mask_edit = QLineEdit("0x7FF")
    newid_edit = QLineEdit("")
    newid_edit.setPlaceholderText("leave empty = keep ID")
    patch_edit = QLineEdit("")
    patch_edit.setPlaceholderText("e.g. pos=0,hex=AA BB")
    rate_spin = QSpinBox()
    rate_spin.setRange(0, 10000)
    rate_spin.setValue(0)
    rate_spin.setSuffix(" ms")
    cfg_l.addRow("Match ID:", match_edit)
    cfg_l.addRow("Match mask:", mask_edit)
    cfg_l.addRow("Remap ID:", newid_edit)
    cfg_l.addRow("Payload patch:", patch_edit)
    cfg_l.addRow("Rate limit (0=none):", rate_spin)
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    add_btn = QPushButton("Add rule")
    del_btn = QPushButton("Delete selected")
    toggle_btn = QPushButton("Toggle enable")
    start_btn = QPushButton("Start forwarding")
    stop_btn = QPushButton("Stop forwarding")
    save_btn = QPushButton("Export JSON…")
    load_btn = QPushButton("Import JSON…")
    clear_btn = QPushButton("Clear counters")
    for w in (add_btn, del_btn, toggle_btn):
        btns.addWidget(w)
    btns.addStretch(1)
    for w in (start_btn, stop_btn, clear_btn, load_btn, save_btn):
        btns.addWidget(w)
    layout.addLayout(btns)

    layout.addWidget(plugin_shell.help_label(
        "Rules persist across sessions. Forwarding is off until you Start. "
        "Match (ID & mask) == (frame.id & mask); optional remap, patch, rate limit."))

    tree = QTreeWidget()
    tree.setHeaderLabels([
        "On", "Match ID", "Mask", "Remap", "Patch", "Rate ms",
        "Hits", "Dropped", "Last hit"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    run_label = QLabel("Forwarding stopped — click Start after adding rules")
    run_label.setStyleSheet("font-weight:bold;")
    layout.addWidget(run_label)

    def _persist():
        state_store.save_state(PLUGIN_ID, {"rules": _rules_for_persist()})

    def _refresh():
        tree.clear()
        for r in _rules:
            tree.addTopLevelItem(QTreeWidgetItem([
                "Y" if r.get("enabled", True) else "N",
                "0x%X" % r["match"],
                "0x%X" % r.get("mask", 0x7FF),
                ("0x%X" % r["new_id"]) if r.get("new_id") is not None else "-",
                r.get("patch_text", "-"),
                str(r.get("rate_limit_ms", 0)),
                str(r.get("hits", 0)),
                str(r.get("dropped", 0)),
                time.strftime("%H:%M:%S", time.localtime(r["last_ts"]))
                if r.get("last_ts") else "-",
            ]))

    def _parse_id(text, default=None):
        text = (text or "").strip()
        if not text:
            return default
        try:
            return int(text, 0)
        except ValueError:
            return default

    def _on_add():
        match = _parse_id(match_edit.text())
        if match is None:
            QMessageBox.warning(win, "Format", "Match ID must be hex/decimal")
            return
        mask = _parse_id(mask_edit.text(), 0x7FF) or 0x7FF
        new_id = _parse_id(newid_edit.text(), None)
        patch_text = patch_edit.text().strip()
        patch = None
        if patch_text:
            try:
                pos_s, hex_s = patch_text.split(",", 1)
                pos = int(pos_s.strip().replace("pos=", ""))
                hex_s = hex_s.strip().replace("hex=", "").replace(" ", "")
                bytes.fromhex(hex_s)
                patch = {"pos": pos, "hex": hex_s}
            except (ValueError, IndexError):
                QMessageBox.warning(win, "Format", "Patch format: pos=0,hex=AABB")
                return
        _rules.append({
            "match": match, "mask": mask, "new_id": new_id,
            "patch": patch, "patch_text": patch_text if patch else "-",
            "rate_limit_ms": rate_spin.value(), "enabled": True,
            "hits": 0, "dropped": 0,
        })
        _refresh()
        _persist()
        plugin_shell.set_status(win, "Rule added (%d total)" % len(_rules), 2500)

    def _on_del():
        sel = tree.selectedItems()
        if not sel:
            return
        idx = tree.indexOfTopLevelItem(sel[0])
        if 0 <= idx < len(_rules):
            _rules.pop(idx)
            _refresh()
            _persist()

    def _on_toggle():
        sel = tree.selectedItems()
        if not sel:
            return
        idx = tree.indexOfTopLevelItem(sel[0])
        if 0 <= idx < len(_rules):
            _rules[idx]["enabled"] = not _rules[idx].get("enabled", True)
            _refresh()
            _persist()

    def _on_start():
        global _active
        if not _rules:
            QMessageBox.information(win, "Gateway", "Add at least one rule first")
            return
        _active = True
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        run_label.setText("Forwarding ON (%d rules)" % len(_rules))
        run_label.setStyleSheet("font-weight:bold;color:#c62828;")
        plugin_shell.set_status(win, "Forwarding started")

    def _on_stop():
        global _active
        _active = False
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        run_label.setText("Forwarding stopped")
        run_label.setStyleSheet("font-weight:bold;")
        plugin_shell.set_status(win, "Forwarding stopped")

    def _on_clear():
        for r in _rules:
            r["hits"] = 0
            r["dropped"] = 0
            r["last_ts"] = None
        _refresh()

    def _on_save():
        path, _ = QFileDialog.getSaveFileName(
            win, "Export rules", "gateway_rules.json", "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(_rules_for_persist(), f, ensure_ascii=False, indent=2)
            plugin_shell.set_status(win, "Exported %d rules" % len(_rules), 4000)
        except OSError as e:
            QMessageBox.warning(win, "Export failed", str(e))

    def _on_load():
        path, _ = QFileDialog.getOpenFileName(
            win, "Import rules", "", "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            _rules.clear()
            _rules.extend(_load_rule_dicts(data))
            _refresh()
            _persist()
            plugin_shell.set_status(win, "Imported %d rules" % len(_rules), 3000)
        except (OSError, ValueError, KeyError, TypeError) as e:
            QMessageBox.warning(win, "Import failed", str(e))

    add_btn.clicked.connect(_on_add)
    del_btn.clicked.connect(_on_del)
    toggle_btn.clicked.connect(_on_toggle)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    clear_btn.clicked.connect(_on_clear)
    save_btn.clicked.connect(_on_save)
    load_btn.clicked.connect(_on_load)
    stop_btn.setEnabled(False)

    context.on_frame(_on_frame)
    context.register_command(
        "canGateway.open", plugin_shell.bind_raise(win), "Tools: Gateway")

    saved = state_store.load_state(PLUGIN_ID, default={}) or {}
    _rules.clear()
    if isinstance(saved.get("rules"), list) and saved["rules"]:
        _rules.extend(_load_rule_dicts(saved["rules"]))
    else:
        _rules.append({
            "match": 0x100, "mask": 0x700, "new_id": None, "patch": None,
            "patch_text": "-", "rate_limit_ms": 0, "enabled": False,
            "hits": 0, "dropped": 0,
        })
    _refresh()

    refresh_timer = QTimer(win)
    refresh_timer.timeout.connect(_refresh)
    refresh_timer.start(500)

    win.show()
    sin.output.append("can-gateway loaded (rules persist; forwarding off until Start)")


def deactivate():
    global _active, _win
    _active = False
    _win = None
    try:
        sin.output.append("can-gateway deactivated")
    except Exception:
        pass
