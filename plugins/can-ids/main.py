# -*- coding: utf-8 -*-
"""can-ids — CAN intrusion detection (research / IDS style).

Learn-mode whitelist (period mu/sigma, static payload), monitor-mode
anomaly alerts (new ID / rate / payload), event timeline + CSV export.
Read-only: never transmits.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QMessageBox, QHeaderView,
    QSpinBox, QGroupBox, QFormLayout,
)

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "can-ids"

_whitelist = {}
_learn_cfg = {"learning": False, "deadline": 0.0, "secs": 30}
_monitor = False
_events = []  # [(ts, level, kind, detail)]
_alert_ts = {}
_runtime = {}
_learn_stats = {}
_summary = {"learned_ids": 0, "learn_frames": 0, "alerts": 0, "checked": 0}
_running = True


def _feed(stats, frame):
    now = time.time()
    st = stats.get(frame.id)
    if st is None:
        stats[frame.id] = {
            "count": 1, "chg": 0, "last": now,
            "periods": [], "data": frame.data, "ext": frame.extended,
        }
    else:
        dt = (now - st["last"]) * 1000.0
        if 0 < dt < 60000:
            st["periods"].append(dt)
            if len(st["periods"]) > 128:
                del st["periods"][:64]
        if frame.data != st["data"]:
            st["chg"] += 1
        st["data"] = frame.data
        st["last"] = now
        st["count"] += 1


def _alert(cid, level, kind, detail):
    key = (cid, kind)
    now = time.time()
    if now - _alert_ts.get(key, 0) < 10.0:
        return
    _alert_ts[key] = now
    _events.append((now, level, kind, "ID 0x%X %s" % (cid, detail)))
    _summary["alerts"] += 1


def _finish_learn():
    global _whitelist
    _learn_cfg["learning"] = False
    total_frames = sum(st["count"] for st in _learn_stats.values())
    _whitelist = {}
    for cid, st in _learn_stats.items():
        mu = sigma = 0.0
        if len(st["periods"]) >= 4:
            mu = sum(st["periods"]) / len(st["periods"])
            var = sum((p - mu) ** 2 for p in st["periods"]) / len(st["periods"])
            sigma = var ** 0.5
        chg_prob = st["chg"] / max(1, st["count"])
        _whitelist[cid] = {
            "mu": mu, "sigma": sigma,
            "static_payload": st["data"] if chg_prob < 0.02 else None,
            "chg_prob": chg_prob, "count": st["count"],
            "ext": st.get("ext", False),
        }
    _summary["learned_ids"] = len(_whitelist)
    _summary["learn_frames"] = total_frames
    _events.append((
        time.time(), "INFO", "learn_done",
        "whitelist %d IDs / %d frames" % (len(_whitelist), total_frames)))


def _on_frame(frame):
    if not _running:
        return
    if _learn_cfg["learning"]:
        _feed(_learn_stats, frame)
        return
    if not _monitor:
        return
    _summary["checked"] += 1
    base = _whitelist.get(frame.id)
    if base is None:
        _alert(frame.id, "HIGH", "new_id", "not in learned whitelist")
        _feed(_runtime, frame)
        return
    now = time.time()
    st = _runtime.get(frame.id)
    if st is None:
        _runtime[frame.id] = {
            "count": 1, "chg": 0, "last": now,
            "periods": [], "data": frame.data,
        }
        return
    dt = (now - st["last"]) * 1000.0
    st["last"] = now
    st["count"] += 1
    if base["mu"] > 0 and 0 < dt < 60000:
        tol = max(3.0 * base["sigma"], 0.10 * base["mu"], 1.0)
        if abs(dt - base["mu"]) > tol:
            _alert(
                frame.id, "MED", "rate_anomaly",
                "interval %.1fms (baseline %.1f±%.1fms)" % (
                    dt, base["mu"], tol))
    if base["static_payload"] is not None and frame.data != base["static_payload"]:
        if frame.data != st["data"]:
            _alert(
                frame.id, "HIGH", "payload_anomaly",
                "static learned payload changed")
    if frame.data != st["data"]:
        st["chg"] += 1
    st["data"] = frame.data


def activate(context):
    global _monitor, _running
    _running = True
    _monitor = False
    _learn_cfg["learning"] = False
    del _events[:]
    _runtime.clear()
    _learn_stats.clear()
    _whitelist.clear()
    _alert_ts.clear()
    _summary.update({
        "learned_ids": 0, "learn_frames": 0, "alerts": 0, "checked": 0,
    })

    win = sin.ui.create_window("CAN IDS")
    win.resize(960, 620)
    plugin_shell.attach_status_bar(
        win, "Learn baseline first — read-only, no TX")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    layout.addWidget(plugin_shell.help_label(
        "Learn captures normal traffic into an ID whitelist with period "
        "mu/sigma and optional static payload. Monitor flags new IDs, "
        "rate outliers (mu±3σ), and static-payload changes. Alerts are "
        "throttled to 1 per ID/kind every 10 s."))

    cfg = QGroupBox("Baseline learn")
    cfg_l = QFormLayout(cfg)
    secs_spin = QSpinBox()
    secs_spin.setRange(5, 600)
    secs_spin.setValue(30)
    secs_spin.setSuffix(" s")
    cfg_l.addRow("Learn duration:", secs_spin)
    layout.addWidget(cfg)

    saved = state_store.load_state(PLUGIN_ID, "settings.json", default={}) or {}
    if "learn_secs" in saved:
        secs_spin.setValue(int(saved["learn_secs"]))

    btns = QHBoxLayout()
    learn_btn = QPushButton("Start learn")
    stop_learn_btn = QPushButton("Stop learn")
    monitor_btn = QPushButton("Start monitor")
    stop_btn = QPushButton("Stop monitor")
    export_btn = QPushButton("Export anomalies CSV…")
    export_wl_btn = QPushButton("Export whitelist CSV…")
    clear_btn = QPushButton("Clear events")
    for w in (learn_btn, stop_learn_btn, monitor_btn, stop_btn):
        btns.addWidget(w)
    btns.addStretch(1)
    btns.addWidget(export_btn)
    btns.addWidget(export_wl_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)
    stop_learn_btn.setEnabled(False)
    monitor_btn.setEnabled(False)
    stop_btn.setEnabled(False)

    status = QLabel("Learn a baseline, then start monitoring (read-only)")
    status.setStyleSheet("font-weight:bold;")
    layout.addWidget(status)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Time", "Level", "Kind", "Detail"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "learn_secs": secs_spin.value(),
        }, "settings.json")

    def _on_learn():
        global _monitor
        _monitor = False
        _persist()
        _learn_cfg["secs"] = secs_spin.value()
        _learn_cfg["learning"] = True
        _learn_cfg["deadline"] = time.time() + secs_spin.value()
        _learn_stats.clear()
        learn_btn.setEnabled(False)
        stop_learn_btn.setEnabled(True)
        monitor_btn.setEnabled(False)
        stop_btn.setEnabled(False)
        status.setText(
            "Learning: capturing %d s of normal traffic…" % secs_spin.value())
        plugin_shell.set_status(win, "Learning baseline")
        _events.append((
            time.time(), "INFO", "learn_start",
            "duration %d s" % secs_spin.value()))

    def _on_stop_learn():
        if not _learn_cfg["learning"]:
            return
        _finish_learn()
        learn_btn.setEnabled(True)
        stop_learn_btn.setEnabled(False)
        monitor_btn.setEnabled(bool(_whitelist))
        status.setText(
            "Learn finished early: whitelist %d IDs / %d frames"
            % (_summary["learned_ids"], _summary["learn_frames"]))
        plugin_shell.set_status(win, "Learn done", 5000)

    def _on_monitor():
        global _monitor
        if not _whitelist:
            QMessageBox.information(win, "Need baseline", "Run learn first")
            return
        if _learn_cfg["learning"]:
            QMessageBox.information(win, "Busy", "Learn still running")
            return
        _monitor = True
        _runtime.clear()
        monitor_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        learn_btn.setEnabled(False)
        status.setText(
            "Monitoring · whitelist %d IDs · threshold mu±3σ"
            % len(_whitelist))
        plugin_shell.set_status(win, "Monitoring")
        _events.append((time.time(), "INFO", "monitor_start", ""))

    def _on_stop():
        global _monitor
        _monitor = False
        stop_btn.setEnabled(False)
        monitor_btn.setEnabled(bool(_whitelist))
        learn_btn.setEnabled(True)
        status.setText("Monitor stopped")
        plugin_shell.set_status(win, "Monitor stopped")

    def _on_export():
        if not _events:
            QMessageBox.information(win, "No data", "No events to export")
            return
        rows = []
        for ts, level, kind, detail in _events:
            rows.append([
                time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts)),
                level, kind, detail,
            ])
        path = plugin_shell.export_csv(
            win, ["Time", "Level", "Kind", "Detail"], rows, "ids_events.csv")
        if path:
            plugin_shell.set_status(
                win, "Exported %d events" % len(_events), 5000)

    def _on_export_wl():
        if not _whitelist:
            QMessageBox.information(win, "No data", "No whitelist yet")
            return
        rows = []
        for cid, r in sorted(_whitelist.items()):
            rows.append([
                "0x%X" % cid,
                "%.2f" % r["mu"],
                "%.2f" % r["sigma"],
                "%.4f" % r["chg_prob"],
                r["count"],
                "static" if r["static_payload"] is not None else "variable",
            ])
        path = plugin_shell.export_csv(
            win,
            ["ID", "mu_ms", "sigma_ms", "chg_prob", "count", "payload"],
            rows, "ids_whitelist.csv")
        if path:
            plugin_shell.set_status(win, "Whitelist exported", 5000)

    def _on_clear():
        del _events[:]
        tree.clear()
        _summary["alerts"] = 0

    def _refresh():
        if _learn_cfg["learning"]:
            remain = max(0, int(_learn_cfg["deadline"] - time.time()))
            n_frames = sum(st["count"] for st in _learn_stats.values())
            status.setText(
                "Learning: %d s left · %d frames / %d IDs"
                % (remain, n_frames, len(_learn_stats)))
            if remain <= 0:
                _finish_learn()
                learn_btn.setEnabled(True)
                stop_learn_btn.setEnabled(False)
                monitor_btn.setEnabled(True)
                status.setText(
                    "Learn done: whitelist %d IDs / %d frames — start monitor"
                    % (_summary["learned_ids"], _summary["learn_frames"]))
                plugin_shell.set_status(win, "Learn done", 5000)
        elif _monitor:
            status.setText(
                "Monitoring · checked %d · alerts %d · whitelist %d"
                % (_summary["checked"], _summary["alerts"], len(_whitelist)))
        if _events:
            tree.clear()
            colors = {
                "HIGH": QColor("#c62828"),
                "MED": QColor("#ef6c00"),
                "INFO": QColor("#1565c0"),
            }
            for ts, level, kind, detail in _events[-300:]:
                item = QTreeWidgetItem([
                    time.strftime("%H:%M:%S", time.localtime(ts)),
                    level, kind, detail,
                ])
                item.setForeground(1, colors.get(level, QColor("#333")))
                tree.addTopLevelItem(item)
            sb = tree.verticalScrollBar()
            sb.setValue(sb.maximum())

    learn_btn.clicked.connect(_on_learn)
    stop_learn_btn.clicked.connect(_on_stop_learn)
    monitor_btn.clicked.connect(_on_monitor)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)
    export_wl_btn.clicked.connect(_on_export_wl)
    clear_btn.clicked.connect(_on_clear)

    timer = QTimer()
    timer.timeout.connect(_refresh)
    timer.start(500)

    context.on_frame(_on_frame)
    context.register_command(
        "canIds.open", plugin_shell.bind_raise(win), "Security: CAN IDS")

    win.show()
    sin.output.append("CAN IDS loaded (learn + monitor, read-only)")


def deactivate():
    global _running, _monitor
    _running = False
    _monitor = False
    _learn_cfg["learning"] = False
    sin.output.append("CAN IDS deactivated")
