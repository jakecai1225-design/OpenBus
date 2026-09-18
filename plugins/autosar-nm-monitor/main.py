# -*- coding: utf-8 -*-
"""autosar-nm-monitor — AUTOSAR CAN Network Management (NM) monitor.

Configurable NM ID base (default 0x400, low byte = node ID), CBV decode,
node state inference (RepeatMessage / Normal / ReadySleep / BusSleep),
timeline + node table + CSV export. Passive monitor only.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QTextEdit, QHeaderView, QTabWidget, QSpinBox,
    QMessageBox,
)

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "autosar-nm-monitor"

CBV_BITS = [
    (0, "Repeat Message Request"),
    (3, "NM Coordinator Sleep Ready"),
    (4, "Active Wakeup"),
    (5, "Partial Network Info (PNI)"),
]

NM_TIMEOUT_S = 2.0

_nodes = {}
_events = []
_base_id = 0x400
_mask_low = 0xFF
_total_nm = 0
_running = True
_timeout_s = NM_TIMEOUT_S


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1500:
        del _events[:800]


def _cbv_text(cbv):
    bits = [name for bit, name in CBV_BITS if cbv & (1 << bit)]
    return "; ".join(bits) if bits else "(none)"


def _on_frame(frame):
    global _total_nm
    if not _running:
        return
    fid = frame.id
    if not (_base_id <= fid <= _base_id + 0xFF):
        return
    data = frame.data
    if not data:
        return
    node = data[0]
    if node != (fid & _mask_low):
        _ev("WARN", "NM 0x%03X byte0=%02X mismatch with ID low byte" % (fid, node))
    cbv = data[-1] if data else 0
    user = data[1:-1] if len(data) > 2 else b""
    ts = time.time()

    _total_nm += 1
    st = _nodes.get(node)
    if st is None:
        st = {
            "last_ts": ts, "periods": [], "last_cbv": cbv,
            "state": "RepeatMessage", "count": 0, "last_user": user,
        }
        _nodes[node] = st
        _ev("UP", "Node %d online (ID 0x%03X)" % (node, fid))
    else:
        dt = (ts - st["last_ts"]) * 1000.0
        if 0 < dt < 5000:
            st["periods"].append(dt)
            if len(st["periods"]) > 64:
                del st["periods"][:32]
        st["last_ts"] = ts
        old_cbv = st["last_cbv"]
        if old_cbv != cbv:
            _ev("CBV", "Node %d CBV: %s → %s" % (
                node, _cbv_text(old_cbv), _cbv_text(cbv)))
    st["last_cbv"] = cbv
    st["count"] += 1
    st["last_user"] = user

    new_state = "RepeatMessage" if (cbv & 0x01) else "Normal"
    if st["state"] != new_state:
        _ev("ST", "Node %d: %s → %s" % (node, st["state"], new_state))
        st["state"] = new_state


def _infer_states():
    now = time.time()
    online = any(now - st["last_ts"] <= _timeout_s for st in _nodes.values())
    result = {}
    for node, st in _nodes.items():
        age = now - st["last_ts"]
        if age <= _timeout_s:
            state = st["state"]
        elif online:
            state = "ReadySleep (silent)"
        else:
            state = "BusSleep (network silent)"
        result[node] = (st, state, age)
    return result


def activate(context):
    global _running, _base_id, _timeout_s, _total_nm
    _nodes.clear()
    del _events[:]
    _total_nm = 0

    saved = state_store.load_state(PLUGIN_ID, "settings.json", default={}) or {}
    try:
        _base_id = int(saved.get("base_id", 0x400))
    except (TypeError, ValueError):
        _base_id = 0x400
    try:
        _timeout_s = float(saved.get("timeout_s", NM_TIMEOUT_S))
    except (TypeError, ValueError):
        _timeout_s = NM_TIMEOUT_S

    _running = True
    win = sin.ui.create_window("AUTOSAR CAN NM Monitor")
    win.resize(940, 610)
    plugin_shell.attach_status_bar(win, "Waiting for NM frames…")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    summary = QLabel("Waiting for NM (default base 0x400, low byte = node ID)…")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    top.addWidget(QLabel("NM base (hex)"))
    base_spin = QSpinBox()
    base_spin.setPrefix("0x")
    base_spin.setDisplayIntegerBase(16)
    base_spin.setRange(0x100, 0x7F00)
    base_spin.setValue(_base_id)
    top.addWidget(base_spin)
    top.addWidget(QLabel("Silence (s)"))
    timeout_spin = QSpinBox()
    timeout_spin.setRange(1, 30)
    timeout_spin.setValue(int(_timeout_s))
    top.addWidget(timeout_spin)
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "State: transmitting (CBV bit0=1)=RepeatMessage, bit0=0=Normal; "
        "silent > timeout with peers online=ReadySleep; all silent=BusSleep. "
        "Passive monitor — no frames sent."))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    node_tab = QWidget()
    nv = QVBoxLayout(node_tab)
    empty = plugin_shell.empty_state_label(
        "No NM nodes yet — wait for frames in the configured ID range.")
    node_tree = QTreeWidget()
    node_tree.setHeaderLabels([
        "Node ID", "State", "Frames", "Period avg (ms)",
        "Period jitter σ (ms)", "Last CBV", "Silent (s)",
    ])
    node_tree.setRootIsDecorated(False)
    node_tree.setAlternatingRowColors(True)
    node_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    nv.addWidget(empty)
    nv.addWidget(node_tree, 1)
    node_tree.hide()

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(node_tab, "Node state")
    tabs.addTab(log_tab, "Timeline")

    context.on_frame(_on_frame)

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "base_id": int(base_spin.value()),
            "timeout_s": float(timeout_spin.value()),
        }, "settings.json")

    def refresh():
        states = _infer_states()
        active = sum(
            1 for _, (_, s, _) in states.items()
            if s in ("RepeatMessage", "Normal"))
        summary.setText(
            "NM frames %d    Online %d/%d    Silent %d"
            % (_total_nm, active, len(states), len(states) - active))

        if states:
            empty.hide()
            node_tree.show()
        else:
            node_tree.hide()
            empty.show()

        node_tree.clear()
        for node, (st, state, age) in sorted(states.items()):
            periods = st["periods"]
            if periods:
                pavg = sum(periods) / len(periods)
                var = sum((p - pavg) ** 2 for p in periods) / len(periods)
                jitter = var ** 0.5
                pavg_s, jitter_s = "%.1f" % pavg, "%.1f" % jitter
            else:
                pavg_s, jitter_s = "-", "-"
            node_tree.addTopLevelItem(QTreeWidgetItem([
                str(node), state, str(st["count"]), pavg_s, jitter_s,
                _cbv_text(st["last_cbv"]), "%.1f" % age]))

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (
                    time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-200:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

        plugin_shell.set_status(
            win, "Live · %d NM frames · %d nodes · base 0x%X"
            % (_total_nm, len(states), _base_id))

    timer = QTimer(win)
    timer.timeout.connect(refresh)
    timer.start(500)

    def on_base_changed(v):
        global _base_id
        _base_id = int(v)
        _persist()

    def on_timeout_changed(v):
        global _timeout_s
        _timeout_s = float(v)
        _persist()

    def on_clear():
        global _total_nm
        _nodes.clear()
        del _events[:]
        _total_nm = 0
        refresh()
        plugin_shell.set_status(win, "Cleared", 3000)

    def on_export():
        states = _infer_states()
        rows = []
        for node, (st, state, age) in sorted(states.items()):
            periods = st["periods"]
            pavg = ("%.1f" % (sum(periods) / len(periods))) if periods else "-"
            jitter = "-"
            if periods:
                mean = sum(periods) / len(periods)
                var = sum((p - mean) ** 2 for p in periods) / len(periods)
                jitter = "%.1f" % (var ** 0.5)
            rows.append([
                node, state, st["count"], pavg, jitter,
                _cbv_text(st["last_cbv"]), "%.1f" % age,
            ])
        for ts, k, t in _events:
            rows.append([
                time.strftime("%H:%M:%S", time.localtime(ts)), k, t,
                "", "", "", "",
            ])
        path = plugin_shell.export_csv(
            win,
            ["Node ID", "State", "Frames", "Period avg ms", "Jitter ms",
             "Last CBV", "Silent s"],
            rows,
            "autosar_nm.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported: %s" % path, 5000)
            QMessageBox.information(win, "Export", "Saved:\n%s" % path)

    raise_fn = plugin_shell.bind_raise(win)
    context.register_command(
        "autosarNmMonitor.open", raise_fn, "Protocol: AUTOSAR NM")

    base_spin.valueChanged.connect(on_base_changed)
    timeout_spin.valueChanged.connect(on_timeout_changed)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append(
        "AUTOSAR NM monitor loaded (passive NM state machine)")


def deactivate():
    global _running
    _running = False
    sin.output.append("AUTOSAR NM monitor deactivated")
