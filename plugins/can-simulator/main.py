# -*- coding: utf-8 -*-
"""can-simulator — Restbus lite (CANoe IG / TSMaster Simulation style).

Per-message enable + cycle, sender-node filter, save/load config, counters.
"""

from __future__ import annotations

import math
import random
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QTreeWidget,
    QTreeWidgetItem, QTextEdit, QMessageBox, QHeaderView, QComboBox,
    QDialog, QDialogButtonBox, QLineEdit, QCheckBox, QSpinBox,
)

import sin
from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "can-simulator"

_dbc = None
_dbc_path = ""
_sims = []
_running = False
_t0 = 0.0
_next_due = {}
_tx_count = 0
_t_start = 0.0

MODE_MAP = {
    "const": "const", "constant": "const", "恒值": "const",
    "inc": "inc", "increment": "inc", "递增": "inc",
    "sine": "sine", "正弦": "sine",
    "rand": "rand", "random": "rand", "随机": "rand",
    "ramp": "ramp", "斜坡": "ramp",
}
MODE_LABELS = ["const", "inc", "sine", "rand", "ramp"]


def activate(context):
    global _dbc, _dbc_path, _running, _tx_count
    _dbc = None
    _dbc_path = ""
    _running = False
    _tx_count = 0
    del _sims[:]
    _next_due.clear()

    win = sin.ui.create_window("CAN Simulator (Restbus)")
    win.resize(1020, 640)
    plugin_shell.attach_status_bar(win, "Idle")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_btn = QPushButton("Load DBC…")
    node_combo = QComboBox()
    node_combo.addItem("(all nodes)", "")
    config_btn = QPushButton("Configure signals…")
    start_btn = QPushButton("Start")
    stop_btn = QPushButton("Stop")
    save_btn = QPushButton("Save config")
    load_cfg_btn = QPushButton("Load config")
    top.addWidget(load_btn)
    top.addWidget(QLabel("Node filter:"))
    top.addWidget(node_combo)
    top.addWidget(config_btn)
    top.addStretch(1)
    top.addWidget(load_cfg_btn)
    top.addWidget(save_btn)
    top.addWidget(start_btn)
    top.addWidget(stop_btn)
    layout.addLayout(top)
    layout.addWidget(plugin_shell.help_label(
        "Toggle Enable per message. Each row has its own cycle. Ctrl+S saves sim config."))

    dbc_label = QLabel("No DBC")
    dbc_label.setStyleSheet("color:#888;font-weight:bold;")
    layout.addWidget(dbc_label)

    load_lbl = QLabel("TX: 0 | ~0 frame/s")
    layout.addWidget(load_lbl)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Enable", "Message", "ID", "Sender", "Cycle ms",
                          "Signals", "Sent", "Last data"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    log_view.setMaximumHeight(110)
    layout.addWidget(log_view)

    def _log(text):
        log_view.append("[%s] %s" % (time.strftime("%H:%M:%S"), text))

    def _refresh():
        tree.clear()
        filt = node_combo.currentData()
        for s in _sims:
            msg = s["msg"]
            if filt and msg.sender != filt:
                continue
            item = QTreeWidgetItem([
                "Y" if s.get("enabled", True) else "N",
                msg.name, "0x%X" % msg.can_id, msg.sender or "-",
                str(s["cycle"]), str(len(msg.signals)), str(s["sent"]),
                " ".join("%02X" % b for b in s.get("last", b""))[:47],
            ])
            tree.addTopLevelItem(item)

    def _on_item_double(item, col):
        # Map visible row back to _sims with filter
        filt = node_combo.currentData()
        visible = [s for s in _sims if not filt or s["msg"].sender == filt]
        idx = tree.indexOfTopLevelItem(item)
        if idx < 0 or idx >= len(visible):
            return
        s = visible[idx]
        if col == 0:
            s["enabled"] = not s.get("enabled", True)
        elif col == 4:
            dlg = QDialog(win)
            dlg.setWindowTitle("Cycle")
            fl = QVBoxLayout(dlg)
            sp = QSpinBox()
            sp.setRange(1, 60000)
            sp.setValue(s["cycle"])
            sp.setSuffix(" ms")
            fl.addWidget(sp)
            bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                  QDialogButtonBox.StandardButton.Cancel)
            fl.addWidget(bb)
            bb.accepted.connect(dlg.accept)
            bb.rejected.connect(dlg.reject)
            if dlg.exec() == QDialog.DialogCode.Accepted:
                s["cycle"] = sp.value()
        _refresh()

    tree.itemDoubleClicked.connect(_on_item_double)

    def _rebuild_nodes():
        cur = node_combo.currentData()
        node_combo.blockSignals(True)
        node_combo.clear()
        node_combo.addItem("(all nodes)", "")
        if _dbc:
            for n in sorted(set(m.sender for m in _dbc.messages.values() if m.sender)):
                node_combo.addItem(n, n)
        idx = node_combo.findData(cur)
        node_combo.setCurrentIndex(max(0, idx))
        node_combo.blockSignals(False)

    def _on_load():
        global _dbc, _dbc_path
        path = dbc_picker.pick_dbc(win)
        if not path:
            return
        db = dbcparse.parse_file(path)
        cyclic = [m for m in db.messages.values() if m.cycle_time > 0]
        if not cyclic:
            # Fall back: all messages with default 100 ms
            cyclic = list(db.messages.values())
            if not cyclic:
                QMessageBox.warning(win, "DBC", "No messages")
                return
        _dbc = db
        _dbc_path = path
        del _sims[:]
        _next_due.clear()
        for m in cyclic:
            _sims.append({
                "msg": m, "cycle": m.cycle_time or 100,
                "phases": {}, "modes": {}, "sent": 0, "last": b"",
                "enabled": True,
            })
        dbc_label.setText("%s — %d messages in sim" % (
            path.split("\\")[-1].split("/")[-1], len(_sims)))
        dbc_label.setStyleSheet("color:#2e7d32;font-weight:bold;")
        _rebuild_nodes()
        _refresh()
        _log("DBC loaded: %d messages" % len(_sims))
        plugin_shell.set_status(win, "DBC ready")

    def _on_config():
        if not _sims:
            QMessageBox.information(win, "Sim", "Load a DBC first")
            return
        dlg = QDialog(win)
        dlg.setWindowTitle("Signal modes")
        dlg.resize(760, 560)
        dl = QVBoxLayout(dlg)
        tree2 = QTreeWidget()
        tree2.setHeaderLabels(["Message / signal", "Mode", "Base / phase"])
        mode_combos = {}
        param_edits = {}
        for s in _sims:
            m = s["msg"]
            parent = QTreeWidgetItem(["%s (0x%X)" % (m.name, m.can_id)])
            en = QCheckBox("Enable")
            en.setChecked(s.get("enabled", True))
            tree2.addTopLevelItem(parent)
            tree2.setItemWidget(parent, 1, en)
            mode_combos[(m.can_id, "__enable__")] = en
            for sig in m.signals:
                child = QTreeWidgetItem([sig.name])
                parent.addChild(child)
                combo = QComboBox()
                combo.addItems(MODE_LABELS)
                cur = MODE_MAP.get(s["modes"].get(sig.name, "const"), "const")
                combo.setCurrentText(cur)
                mode_combos[(m.can_id, sig.name)] = combo
                tree2.setItemWidget(child, 1, combo)
                edit = QLineEdit("%.2f" % s["phases"].get(sig.name, sig.minimum))
                param_edits[(m.can_id, sig.name)] = edit
                tree2.setItemWidget(child, 2, edit)
        tree2.expandAll()
        dl.addWidget(tree2, 1)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                              QDialogButtonBox.StandardButton.Cancel)
        dl.addWidget(bb)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        for s in _sims:
            m = s["msg"]
            en_w = mode_combos.get((m.can_id, "__enable__"))
            if en_w:
                s["enabled"] = en_w.isChecked()
            for sig in m.signals:
                key = (m.can_id, sig.name)
                s["modes"][sig.name] = mode_combos[key].currentText()
                try:
                    s["phases"][sig.name] = float(param_edits[key].text())
                except ValueError:
                    s["phases"][sig.name] = sig.minimum
        _refresh()
        _log("Signal config updated")

    def _compute_value(s, sig, t):
        mode = MODE_MAP.get(s["modes"].get(sig.name, "const"), "const")
        base = s["phases"].get(sig.name, sig.minimum)
        amp = max(abs(sig.maximum - sig.minimum) / 2.0, 1.0)
        mid = (sig.maximum + sig.minimum) / 2.0
        if mode == "const":
            v = base
        elif mode == "inc":
            step = max(amp / 50.0, abs(sig.factor) or 0.01)
            v = base + (t * step)
        elif mode == "sine":
            v = mid + amp * math.sin(t * 2.0 * math.pi / 5.0 + base)
        elif mode == "rand":
            v = random.uniform(sig.minimum, sig.maximum)
        elif mode == "ramp":
            v = sig.minimum + (sig.maximum - sig.minimum) * ((t % 10.0) / 10.0)
        else:
            v = base
        if sig.factor >= 0:
            v = max(sig.minimum, min(sig.maximum, v))
        return v

    def _send_sim(s, t):
        global _tx_count
        msg = s["msg"]
        values = {sig.name: _compute_value(s, sig, t) for sig in msg.signals}
        for sig in msg.signals:
            if MODE_MAP.get(s["modes"].get(sig.name), "const") == "inc":
                s["phases"][sig.name] = values[sig.name]
        data = bytes(dbcparse.encode_message(msg, values, dlc=max(msg.dlc, 8)))
        s["last"] = data
        s["sent"] += 1
        _tx_count += 1
        sin.frames.send(msg.can_id, data, extended=msg.extended)

    sim_timer = QTimer()

    def _on_tick():
        global _t0
        t = time.time() - _t0
        now = time.monotonic()
        filt = node_combo.currentData()
        for s in _sims:
            if not s.get("enabled", True):
                continue
            if filt and s["msg"].sender != filt:
                continue
            key = id(s)
            if now >= _next_due.get(key, 0):
                _send_sim(s, t)
                _next_due[key] = now + (s["cycle"] / 1000.0)
        elapsed = max(0.001, time.time() - _t_start)
        load_lbl.setText("TX: %d | ~%.0f frame/s" % (_tx_count, _tx_count / elapsed))
        _refresh()

    def _on_start():
        global _running, _t0, _tx_count, _t_start
        if not _sims:
            QMessageBox.information(win, "Sim", "Load a DBC first")
            return
        _running = True
        _t0 = time.time()
        _t_start = _t0
        _tx_count = 0
        now = time.monotonic()
        for s in _sims:
            _next_due[id(s)] = now
        sim_timer.timeout.connect(_on_tick)
        sim_timer.start(5)
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        plugin_shell.set_status(win, "Simulation running")
        _log("Sim start")

    def _on_stop():
        global _running
        _running = False
        sim_timer.stop()
        try:
            sim_timer.timeout.disconnect(_on_tick)
        except TypeError:
            pass
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        plugin_shell.set_status(win, "Stopped")
        _log("Sim stop")

    def _serialize():
        return {
            "dbc_path": _dbc_path,
            "node_filter": node_combo.currentData() or "",
            "messages": [{
                "can_id": s["msg"].can_id,
                "cycle": s["cycle"],
                "enabled": s.get("enabled", True),
                "modes": s.get("modes", {}),
                "phases": s.get("phases", {}),
            } for s in _sims],
        }

    def _apply_config(data):
        global _dbc, _dbc_path
        if not data:
            return
        path = data.get("dbc_path") or ""
        if path:
            try:
                _dbc = dbcparse.parse_file(path)
                _dbc_path = path
            except OSError as ex:
                QMessageBox.warning(win, "Config", str(ex))
                return
            del _sims[:]
            by_id = {m.can_id: m for m in _dbc.messages.values()}
            for entry in data.get("messages", []):
                m = by_id.get(int(entry["can_id"]))
                if not m:
                    continue
                _sims.append({
                    "msg": m, "cycle": int(entry.get("cycle", m.cycle_time or 100)),
                    "phases": {k: float(v) for k, v in (entry.get("phases") or {}).items()},
                    "modes": dict(entry.get("modes") or {}),
                    "sent": 0, "last": b"",
                    "enabled": bool(entry.get("enabled", True)),
                })
            dbc_label.setText("%s — %d messages" % (
                path.split("\\")[-1].split("/")[-1], len(_sims)))
            dbc_label.setStyleSheet("color:#2e7d32;font-weight:bold;")
            _rebuild_nodes()
            filt = data.get("node_filter") or ""
            idx = node_combo.findData(filt)
            if idx >= 0:
                node_combo.setCurrentIndex(idx)
            _refresh()

    def _on_save():
        path = state_store.save_state(PLUGIN_ID, _serialize(), "sim.json")
        plugin_shell.set_status(win, "Saved %s" % path, 4000)

    def _on_load_cfg():
        data = state_store.load_state(PLUGIN_ID, "sim.json")
        if not data:
            QMessageBox.information(win, "Config", "No saved config")
            return
        _apply_config(data)
        _log("Config loaded")

    context.register_command(
        "canSimulator.open", plugin_shell.bind_raise(win), "Simulation: Restbus")

    load_btn.clicked.connect(_on_load)
    config_btn.clicked.connect(_on_config)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    save_btn.clicked.connect(_on_save)
    load_cfg_btn.clicked.connect(_on_load_cfg)
    node_combo.currentIndexChanged.connect(_refresh)
    stop_btn.setEnabled(False)
    plugin_shell.bind_shortcut(win, "Ctrl+S", _on_save)

    saved = state_store.load_state(PLUGIN_ID, "sim.json")
    if saved:
        _apply_config(saved)

    win.show()
    sin.output.append("can-simulator ready (per-msg enable/cycle, node filter)")


def deactivate():
    global _running
    _running = False
    sin.output.append("can-simulator deactivated")
