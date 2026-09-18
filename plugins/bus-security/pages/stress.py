# -*- coding: utf-8 -*-
"""Stress workspace — load / malformed inject (rate-limited TX)."""

from __future__ import annotations

import random
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QComboBox,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "bus-security"
SETTINGS_KEY = "stress.json"

_FD_SEQ = [8, 12, 16, 20, 24, 32, 48, 64]


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    state = {
        "running": False, "sent": 0, "started": 0.0, "duration": 0,
        "mode": 0, "target_fps": 0.0, "errors": 0, "interval_ms": 0,
    }
    counters = {"fd_idx": 0, "counter": 0}

    warn = QLabel(
        "WARNING: Stress injects high bus load and/or malformed frames. "
        "Use only on a test bench or isolated network. May disrupt ECUs "
        "and diagnostics. Prefer a low load target and a short duration first.")
    warn.setWordWrap(True)
    warn.setStyleSheet(
        "background:#ffebee;color:#c62828;border:1px solid #c62828;"
        "padding:8px;font-weight:bold;")
    layout.addWidget(warn)

    cfg = QGroupBox("Stress settings")
    cfg_l = QFormLayout(cfg)
    id_edit = QLineEdit("0x66")
    cfg_l.addRow("Target ID:", id_edit)
    mode_combo = QComboBox()
    mode_combo.addItems([
        "Flood (incrementing payload)",
        "Malformed: random DLC (0–8)",
        "Malformed: zero-length (DLC=0)",
        "CAN FD length sweep (8→64)",
    ])
    cfg_l.addRow("Inject mode:", mode_combo)
    load_spin = QDoubleSpinBox()
    load_spin.setRange(1, 100)
    load_spin.setValue(session.default_load_pct)
    load_spin.setSuffix(" %")
    cfg_l.addRow("Load target:", load_spin)
    bitrate_spin = QComboBox()
    bitrate_spin.addItems(["125 kbps", "250 kbps", "500 kbps", "1 Mbps"])
    bitrate_spin.setCurrentIndex(2)
    cfg_l.addRow("Bus bitrate:", bitrate_spin)
    duration_spin = QSpinBox()
    duration_spin.setRange(0, 3600)
    duration_spin.setValue(10)
    duration_spin.setSpecialValueText("unlimited")
    duration_spin.setSuffix(" s")
    cfg_l.addRow("Duration:", duration_spin)
    gap_spin = QSpinBox()
    gap_spin.setRange(0, 10000)
    gap_spin.setValue(session.default_gap_ms)
    gap_spin.setSuffix(" ms (min gap)")
    cfg_l.addRow("Min frame gap:", gap_spin)
    layout.addWidget(cfg)

    layout.addWidget(plugin_shell.help_label(
        "Load target maps to frame rate assuming ~128-bit classic frames "
        "(~700-bit for FD sweep). Suite strip defaults apply on Apply. "
        "Min gap overrides a too-aggressive interval. Stop / Stop All aborts."))

    btns = QHBoxLayout()
    start_btn = QPushButton("Start stress")
    stop_btn = QPushButton("Stop")
    export_btn = QPushButton("Export stats CSV…")
    btns.addWidget(start_btn)
    btns.addWidget(stop_btn)
    btns.addStretch(1)
    btns.addWidget(export_btn)
    layout.addLayout(btns)
    stop_btn.setEnabled(False)

    stats = QLabel("Ready (no frames sent)")
    stats.setStyleSheet("font-weight:bold;")
    layout.addWidget(stats)

    err_log = QLabel("")
    err_log.setWordWrap(True)
    layout.addWidget(err_log)
    layout.addStretch()

    timer = QTimer(root)
    refresher = QTimer(root)

    saved = state_store.load_state(PLUGIN_ID, SETTINGS_KEY, default={}) or {}
    if saved.get("can_id"):
        id_edit.setText(str(saved["can_id"]))
    if "mode" in saved:
        mode_combo.setCurrentIndex(int(saved["mode"]))
    if "load_pct" in saved:
        load_spin.setValue(float(saved["load_pct"]))
    if "bitrate_idx" in saved:
        bitrate_spin.setCurrentIndex(int(saved["bitrate_idx"]))
    if "duration_s" in saved:
        duration_spin.setValue(int(saved["duration_s"]))
    if "gap_ms" in saved:
        gap_spin.setValue(int(saved["gap_ms"]))

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "can_id": id_edit.text().strip(),
            "mode": mode_combo.currentIndex(),
            "load_pct": load_spin.value(),
            "bitrate_idx": bitrate_spin.currentIndex(),
            "duration_s": duration_spin.value(),
            "gap_ms": gap_spin.value(),
        }, SETTINGS_KEY)

    def _parse_id():
        try:
            v = int(id_edit.text(), 0)
            if 0 <= v <= 0x1FFFFFFF:
                return v
        except ValueError:
            pass
        return None

    def _fps_from_load():
        bitrate_kbps = [125, 250, 500, 1000][bitrate_spin.currentIndex()]
        mode = mode_combo.currentIndex()
        bits_per_frame = 128 if mode != 3 else 700
        return bitrate_kbps * 1000.0 * (load_spin.value() / 100.0) / bits_per_frame

    def _next_payload():
        mode = state["mode"]
        if mode == 0:
            counters["counter"] = (counters["counter"] + 1) & 0xFFFFFFFF
            c = counters["counter"]
            return bytes([(c >> s) & 0xFF for s in (24, 16, 8, 0)] + [0x00] * 4)
        if mode == 1:
            n = random.randint(0, 8)
            return bytes(random.getrandbits(8) for _ in range(n))
        if mode == 2:
            return b""
        n = _FD_SEQ[counters["fd_idx"] % len(_FD_SEQ)]
        counters["fd_idx"] += 1
        return bytes([i & 0xFF for i in range(n)])

    def _set_running(running):
        state["running"] = running
        start_btn.setEnabled(not running)
        stop_btn.setEnabled(running)
        for w in (id_edit, mode_combo, load_spin, bitrate_spin,
                  duration_spin, gap_spin):
            w.setEnabled(not running)

    def _on_stop():
        if not state["running"]:
            return
        state["running"] = False
        timer.stop()
        _set_running(False)
        elapsed = time.time() - state["started"] if state["started"] else 0
        stats.setText(
            "Stopped · sent %d / %.1fs · ~%.0f fps · errors %d"
            % (state["sent"], elapsed,
               state["sent"] / elapsed if elapsed > 0.5 else 0,
               state["errors"]))
        plugin_shell.set_status(parent, "Stress stopped")
        log_fn("Stress", "Stopped · sent %d · errors %d" % (
            state["sent"], state["errors"]))

    def _tick():
        if not state["running"]:
            return
        if state["duration"] and time.time() - state["started"] >= state["duration"]:
            _on_stop()
            stats.setText(
                "Duration done · sent %d frames / %.1fs"
                % (state["sent"], time.time() - state["started"]))
            plugin_shell.set_status(parent, "Stress duration complete", 5000)
            return
        mode = mode_combo.currentIndex()
        is_fd = mode == 3
        payload = _next_payload()
        cid = _parse_id()
        if cid is None:
            _on_stop()
            stats.setText("Bad target ID — stopped")
            return
        try:
            sin.frames.send(cid, payload, False, is_fd)
            state["sent"] += 1
        except Exception as e:
            state["errors"] += 1
            err_log.setText("Send error: %s" % e)
            log_fn("ERR", "Stress send error: %s" % e, "ERR")

    def _on_start():
        if _parse_id() is None:
            QMessageBox.warning(parent, "Config error", "Target ID must be hex")
            return
        state["mode"] = mode_combo.currentIndex()
        state["target_fps"] = _fps_from_load()
        state["duration"] = duration_spin.value()
        if state["target_fps"] <= 0:
            QMessageBox.warning(parent, "Config error", "Target FPS computed as 0")
            return
        if load_spin.value() >= 80:
            title, text = (
                "High load",
                "Load target is %.0f%% — this can saturate the bus.\n"
                "Continue only on a safe test bench." % load_spin.value(),
            )
        else:
            title, text = (
                "Confirm stress",
                "This will inject frames on the connected bus.\n"
                "Continue only on a safe test bench.",
            )
        if hasattr(parent, "confirm_danger"):
            ok = parent.confirm_danger(title, text)
        else:
            ok = QMessageBox.warning(
                parent, title, text,
                QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Cancel) == QMessageBox.StandardButton.Ok
        if not ok:
            return
        _persist()
        interval_ms = 1000.0 / state["target_fps"]
        if gap_spin.value() and interval_ms < gap_spin.value():
            interval_ms = gap_spin.value()
        interval_ms = max(1, int(interval_ms))
        state["interval_ms"] = interval_ms
        state["started"] = time.time()
        state["sent"] = 0
        state["errors"] = 0
        counters["fd_idx"] = 0
        counters["counter"] = 0
        _set_running(True)
        timer.start(interval_ms)
        stats.setText(
            "Stressing · target %.0f fps · interval %d ms"
            % (state["target_fps"], interval_ms))
        plugin_shell.set_status(
            parent, "Stress at ~%.0f fps" % state["target_fps"])
        log_fn("Stress", "Started · ~%.0f fps · %s" % (
            state["target_fps"], mode_combo.currentText()), "TX")

    def _on_export():
        elapsed = time.time() - state["started"] if state["started"] else 0
        actual = state["sent"] / elapsed if elapsed > 0.5 else 0
        rows = [[
            time.strftime("%Y-%m-%d %H:%M:%S"),
            id_edit.text().strip(),
            mode_combo.currentText(),
            "%.1f" % load_spin.value(),
            bitrate_spin.currentText(),
            state["duration"],
            state["interval_ms"],
            "%.1f" % state["target_fps"],
            "%.1f" % actual,
            state["sent"],
            state["errors"],
            "%.3f" % elapsed,
        ]]
        path = plugin_shell.export_csv(
            parent,
            ["Time", "ID", "Mode", "Load%", "Bitrate", "Duration_s",
             "Interval_ms", "Target_fps", "Actual_fps", "Sent", "Errors",
             "Elapsed_s"],
            rows, "stress_stats.csv")
        if path:
            plugin_shell.set_status(parent, "Stats exported", 5000)

    def _refresh():
        if state["running"]:
            elapsed = time.time() - state["started"]
            actual = state["sent"] / elapsed if elapsed > 0.5 else 0
            remain = ""
            if state["duration"]:
                remain = " · %d s left" % max(
                    0, int(state["duration"] - elapsed))
            stats.setText(
                "Stressing · sent %d · target %.0f fps · actual ~%.0f fps"
                " · errors %d%s"
                % (state["sent"], state["target_fps"], actual,
                   state["errors"], remain))

    def _on_defaults():
        if not state["running"]:
            load_spin.setValue(session.default_load_pct)
            gap_spin.setValue(session.default_gap_ms)

    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)

    timer.timeout.connect(_tick)
    refresher.timeout.connect(_refresh)
    refresher.start(500)

    session.register_stop_handler(_on_stop)
    session.on_defaults_changed(_on_defaults)
    return root
