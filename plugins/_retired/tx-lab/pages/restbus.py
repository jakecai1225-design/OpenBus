# -*- coding: utf-8 -*-
"""Restbus workspace — message enable+cycle simulation (from can-simulator)."""

from __future__ import annotations

import math
import random
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
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
from _shared import dbcparse, plugin_shell, state_store

PLUGIN_ID = "tx-lab"

MODE_MAP = {
    "const": "const", "constant": "const",
    "inc": "inc", "increment": "inc",
    "sine": "sine",
    "rand": "rand", "random": "rand",
    "ramp": "ramp",
}
MODE_LABELS = ["const", "inc", "sine", "rand", "ramp"]


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    sims: list = []
    next_due: dict = {}
    running = {"v": False}
    tx_count = {"n": 0}
    t0 = {"v": 0.0}
    t_start = {"v": 0.0}

    top = QHBoxLayout()
    rebuild_btn = QPushButton("Rebuild from DBC")
    node_combo = QComboBox()
    node_combo.addItem("(all nodes)", "")
    config_btn = QPushButton("Configure signals…")
    start_btn = QPushButton("Start")
    stop_btn = QPushButton("Stop")
    save_btn = QPushButton("Save config")
    load_cfg_btn = QPushButton("Load config")
    top.addWidget(rebuild_btn)
    top.addWidget(QLabel("Node filter:"))
    top.addWidget(node_combo)
    top.addWidget(config_btn)
    top.addStretch(1)
    top.addWidget(load_cfg_btn)
    top.addWidget(save_btn)
    top.addWidget(start_btn)
    top.addWidget(stop_btn)
    layout.addLayout(top)
    layout.addWidget(QLabel(
        "Toggle Enable per message. Each row has its own cycle. "
        "Node filter uses the shared DBC."))

    dbc_label = QLabel("No DBC — load from shared strip")
    dbc_label.setStyleSheet("color:#888;font-weight:bold;")
    layout.addWidget(dbc_label)

    load_lbl = QLabel("TX: 0 | ~0 frame/s")
    layout.addWidget(load_lbl)

    tree = QTreeWidget()
    tree.setHeaderLabels([
        "Enable", "Message", "ID", "Sender", "Cycle ms",
        "Signals", "Sent", "Last data",
    ])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    def _log(text, color=None):
        log_fn("RESTBUS", text, color)

    def _refresh():
        tree.clear()
        filt = node_combo.currentData()
        for s in sims:
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
        filt = node_combo.currentData()
        visible = [s for s in sims if not filt or s["msg"].sender == filt]
        idx = tree.indexOfTopLevelItem(item)
        if idx < 0 or idx >= len(visible):
            return
        s = visible[idx]
        if col == 0:
            s["enabled"] = not s.get("enabled", True)
        elif col == 4:
            dlg = QDialog(root)
            dlg.setWindowTitle("Cycle")
            fl = QVBoxLayout(dlg)
            sp = QSpinBox()
            sp.setRange(1, 60000)
            sp.setValue(s["cycle"])
            sp.setSuffix(" ms")
            fl.addWidget(sp)
            bb = QDialogButtonBox(
                QDialogButtonBox.StandardButton.Ok
                | QDialogButtonBox.StandardButton.Cancel)
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
        db = session.dbc
        if db:
            for n in sorted(
                    set(m.sender for m in db.messages.values() if m.sender)):
                node_combo.addItem(n, n)
        idx = node_combo.findData(cur)
        node_combo.setCurrentIndex(max(0, idx))
        node_combo.blockSignals(False)

    def _rebuild_from_dbc(preserve_cfg=None):
        db = session.dbc
        if not db:
            del sims[:]
            next_due.clear()
            dbc_label.setText("No DBC — load from shared strip")
            dbc_label.setStyleSheet("color:#888;font-weight:bold;")
            _rebuild_nodes()
            _refresh()
            return
        cyclic = [m for m in db.messages.values() if m.cycle_time > 0]
        if not cyclic:
            cyclic = list(db.messages.values())
        by_id = {}
        if preserve_cfg:
            for entry in preserve_cfg:
                by_id[int(entry["can_id"])] = entry
        del sims[:]
        next_due.clear()
        for m in cyclic:
            entry = by_id.get(m.can_id, {})
            sims.append({
                "msg": m,
                "cycle": int(entry.get("cycle", m.cycle_time or 100)),
                "phases": {
                    k: float(v) for k, v in (entry.get("phases") or {}).items()
                },
                "modes": dict(entry.get("modes") or {}),
                "sent": 0, "last": b"",
                "enabled": bool(entry.get("enabled", True)),
            })
        path = session.dbc_path or ""
        name = path.split("\\")[-1].split("/")[-1] if path else "DBC"
        dbc_label.setText("%s — %d messages in sim" % (name, len(sims)))
        dbc_label.setStyleSheet("color:#2e7d32;font-weight:bold;")
        _rebuild_nodes()
        _refresh()
        _log("Restbus rebuilt: %d messages" % len(sims))

    def _on_config():
        if not sims:
            QMessageBox.information(root, "Sim", "Load a DBC first")
            return
        dlg = QDialog(root)
        dlg.setWindowTitle("Signal modes")
        dlg.resize(760, 560)
        dl = QVBoxLayout(dlg)
        tree2 = QTreeWidget()
        tree2.setHeaderLabels(["Message / signal", "Mode", "Base / phase"])
        mode_combos = {}
        param_edits = {}
        for s in sims:
            m = s["msg"]
            parent_item = QTreeWidgetItem(["%s (0x%X)" % (m.name, m.can_id)])
            en = QCheckBox("Enable")
            en.setChecked(s.get("enabled", True))
            tree2.addTopLevelItem(parent_item)
            tree2.setItemWidget(parent_item, 1, en)
            mode_combos[(m.can_id, "__enable__")] = en
            for sig in m.signals:
                child = QTreeWidgetItem([sig.name])
                parent_item.addChild(child)
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
        bb = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel)
        dl.addWidget(bb)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        for s in sims:
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
        msg = s["msg"]
        values = {sig.name: _compute_value(s, sig, t) for sig in msg.signals}
        for sig in msg.signals:
            if MODE_MAP.get(s["modes"].get(sig.name), "const") == "inc":
                s["phases"][sig.name] = values[sig.name]
        data = bytes(dbcparse.encode_message(msg, values, dlc=max(msg.dlc, 8)))
        s["last"] = data
        s["sent"] += 1
        tx_count["n"] += 1
        sin.frames.send(msg.can_id, data, extended=msg.extended)

    sim_timer = QTimer(root)

    def _on_tick():
        t = time.time() - t0["v"]
        now = time.monotonic()
        filt = node_combo.currentData()
        for s in sims:
            if not s.get("enabled", True):
                continue
            if filt and s["msg"].sender != filt:
                continue
            key = id(s)
            if now >= next_due.get(key, 0):
                _send_sim(s, t)
                next_due[key] = now + (s["cycle"] / 1000.0)
        elapsed = max(0.001, time.time() - t_start["v"])
        load_lbl.setText(
            "TX: %d | ~%.0f frame/s" % (tx_count["n"], tx_count["n"] / elapsed))
        _refresh()

    def _do_start() -> bool:
        if not sims:
            return False
        running["v"] = True
        t0["v"] = time.time()
        t_start["v"] = t0["v"]
        tx_count["n"] = 0
        now = time.monotonic()
        for s in sims:
            next_due[id(s)] = now
        try:
            sim_timer.timeout.disconnect(_on_tick)
        except TypeError:
            pass
        sim_timer.timeout.connect(_on_tick)
        sim_timer.start(5)
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        plugin_shell.set_status(parent, "Restbus running")
        _log("Sim start")
        session.refresh_bus_status()
        return True

    def _do_stop():
        running["v"] = False
        sim_timer.stop()
        try:
            sim_timer.timeout.disconnect(_on_tick)
        except TypeError:
            pass
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        plugin_shell.set_status(parent, "Restbus stopped")
        _log("Sim stop")
        session.refresh_bus_status()

    def _on_start():
        if not _do_start():
            QMessageBox.information(root, "Sim", "Load a DBC first")

    def _serialize():
        return {
            "dbc_path": session.dbc_path or "",
            "node_filter": node_combo.currentData() or "",
            "messages": [{
                "can_id": s["msg"].can_id,
                "cycle": s["cycle"],
                "enabled": s.get("enabled", True),
                "modes": s.get("modes", {}),
                "phases": s.get("phases", {}),
            } for s in sims],
        }

    def _apply_config(data):
        if not data:
            return
        path = data.get("dbc_path") or ""
        if path and (not session.dbc_path or session.dbc_path != path):
            if not session.load_dbc(path):
                return
        _rebuild_from_dbc(preserve_cfg=data.get("messages"))
        filt = data.get("node_filter") or ""
        idx = node_combo.findData(filt)
        if idx >= 0:
            node_combo.setCurrentIndex(idx)
        _refresh()

    def _on_save():
        path = state_store.save_state(PLUGIN_ID, _serialize(), "sim.json")
        plugin_shell.set_status(parent, "Saved %s" % path, 4000)

    def _on_load_cfg():
        data = state_store.load_state(PLUGIN_ID, "sim.json")
        if not data:
            QMessageBox.information(root, "Config", "No saved config")
            return
        _apply_config(data)
        _log("Config loaded")

    rebuild_btn.clicked.connect(lambda: _rebuild_from_dbc())
    config_btn.clicked.connect(_on_config)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_do_stop)
    save_btn.clicked.connect(_on_save)
    load_cfg_btn.clicked.connect(_on_load_cfg)
    node_combo.currentIndexChanged.connect(_refresh)
    stop_btn.setEnabled(False)
    plugin_shell.bind_shortcut(parent, "Ctrl+Shift+S", _on_save)

    session.register_tx_controller(
        "Restbus",
        _do_start,
        _do_stop,
        lambda: running["v"],
    )
    session.on_dbc_changed(lambda: _rebuild_from_dbc())

    saved = state_store.load_state(PLUGIN_ID, "sim.json")
    if saved:
        _apply_config(saved)
    elif session.dbc:
        _rebuild_from_dbc()

    return root
