# -*- coding: utf-8 -*-
"""can-stress — CAN stress / load generator.

Flood at target bus load %%, malformed DLC / zero-length / FD length
sweep, duration + min-gap guards, live stats + CSV export.
WARNING: injects high traffic — bench / isolated networks only.
"""

from __future__ import annotations

import random
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QGroupBox, QFormLayout, QComboBox, QSpinBox, QDoubleSpinBox,
    QMessageBox, QLineEdit,
)

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "can-stress"

_state = {
    "running": False, "sent": 0, "started": 0.0, "duration": 0,
    "mode": 0, "target_fps": 0.0, "errors": 0, "interval_ms": 0,
}
_timer = None
_fd_seq = [8, 12, 16, 20, 24, 32, 48, 64]
_fd_idx = 0
_counter = 0


def _next_payload():
    global _counter, _fd_idx
    mode = _state["mode"]
    if mode == 0:  # flood with incrementing counter
        _counter = (_counter + 1) & 0xFFFFFFFF
        return bytes([(_counter >> s) & 0xFF for s in (24, 16, 8, 0)] + [0x00] * 4)
    if mode == 1:  # random DLC 0-8
        n = random.randint(0, 8)
        return bytes(random.getrandbits(8) for _ in range(n))
    if mode == 2:  # zero-length
        return b""
    # FD length sequence
    n = _fd_seq[_fd_idx % len(_fd_seq)]
    _fd_idx += 1
    return bytes([i & 0xFF for i in range(n)])


def activate(context):
    global _timer, _fd_idx, _counter
    _state.update({
        "running": False, "sent": 0, "started": 0.0,
        "duration": 0, "mode": 0, "target_fps": 0.0, "errors": 0,
        "interval_ms": 0,
    })
    _fd_idx = 0
    _counter = 0

    win = sin.ui.create_window("CAN Stress")
    win.resize(880, 560)
    plugin_shell.attach_status_bar(
        win, "Idle — will not transmit until Start")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

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
    load_spin.setValue(50)
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
    gap_spin.setValue(0)
    gap_spin.setSuffix(" ms (min gap)")
    cfg_l.addRow("Min frame gap:", gap_spin)
    layout.addWidget(cfg)

    layout.addWidget(plugin_shell.help_label(
        "Load target maps to frame rate assuming ~128-bit classic frames "
        "(~700-bit for FD sweep). Min gap overrides a too-aggressive "
        "interval. Stop aborts immediately."))

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

    log = QLabel("")
    log.setWordWrap(True)
    layout.addWidget(log)

    saved = state_store.load_state(PLUGIN_ID, "settings.json", default={}) or {}
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
        }, "settings.json")

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

    def _set_running(running):
        _state["running"] = running
        start_btn.setEnabled(not running)
        stop_btn.setEnabled(running)
        for w in (id_edit, mode_combo, load_spin, bitrate_spin,
                  duration_spin, gap_spin):
            w.setEnabled(not running)

    def _tick():
        if not _state["running"]:
            return
        if _state["duration"] and time.time() - _state["started"] >= _state["duration"]:
            _on_stop()
            stats.setText(
                "Duration done · sent %d frames / %.1fs"
                % (_state["sent"], time.time() - _state["started"]))
            plugin_shell.set_status(win, "Duration complete", 5000)
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
            _state["sent"] += 1
        except Exception as e:
            _state["errors"] += 1
            log.setText("Send error: %s" % e)

    def _on_start():
        if _parse_id() is None:
            QMessageBox.warning(win, "Config error", "Target ID must be hex")
            return
        _state["mode"] = mode_combo.currentIndex()
        _state["target_fps"] = _fps_from_load()
        _state["duration"] = duration_spin.value()
        if _state["target_fps"] <= 0:
            QMessageBox.warning(win, "Config error", "Target FPS computed as 0")
            return
        if load_spin.value() >= 80:
            reply = QMessageBox.warning(
                win, "High load",
                "Load target is %.0f%% — this can saturate the bus.\n"
                "Continue?" % load_spin.value(),
                QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Cancel)
            if reply != QMessageBox.StandardButton.Ok:
                return
        else:
            reply = QMessageBox.warning(
                win, "Confirm stress",
                "This will inject frames on the connected bus.\n"
                "Continue only on a safe test bench.",
                QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Cancel)
            if reply != QMessageBox.StandardButton.Ok:
                return
        _persist()
        interval_ms = 1000.0 / _state["target_fps"]
        if gap_spin.value() and interval_ms < gap_spin.value():
            interval_ms = gap_spin.value()
        interval_ms = max(1, int(interval_ms))
        _state["interval_ms"] = interval_ms
        _state["started"] = time.time()
        _state["sent"] = 0
        _state["errors"] = 0
        _set_running(True)
        _timer.start(interval_ms)
        stats.setText(
            "Stressing · target %.0f fps · interval %d ms"
            % (_state["target_fps"], interval_ms))
        plugin_shell.set_status(
            win, "Stress at ~%.0f fps" % _state["target_fps"])

    def _on_stop():
        _state["running"] = False
        if _timer is not None:
            _timer.stop()
        _set_running(False)
        elapsed = time.time() - _state["started"] if _state["started"] else 0
        stats.setText(
            "Stopped · sent %d / %.1fs · ~%.0f fps · errors %d"
            % (_state["sent"], elapsed,
               _state["sent"] / elapsed if elapsed > 0.5 else 0,
               _state["errors"]))
        plugin_shell.set_status(win, "Stopped")

    def _on_export():
        elapsed = time.time() - _state["started"] if _state["started"] else 0
        actual = _state["sent"] / elapsed if elapsed > 0.5 else 0
        rows = [[
            time.strftime("%Y-%m-%d %H:%M:%S"),
            id_edit.text().strip(),
            mode_combo.currentText(),
            "%.1f" % load_spin.value(),
            bitrate_spin.currentText(),
            _state["duration"],
            _state["interval_ms"],
            "%.1f" % _state["target_fps"],
            "%.1f" % actual,
            _state["sent"],
            _state["errors"],
            "%.3f" % elapsed,
        ]]
        path = plugin_shell.export_csv(
            win,
            ["Time", "ID", "Mode", "Load%", "Bitrate", "Duration_s",
             "Interval_ms", "Target_fps", "Actual_fps", "Sent", "Errors",
             "Elapsed_s"],
            rows, "stress_stats.csv")
        if path:
            plugin_shell.set_status(win, "Stats exported", 5000)

    def _refresh():
        if _state["running"]:
            elapsed = time.time() - _state["started"]
            actual = _state["sent"] / elapsed if elapsed > 0.5 else 0
            remain = ""
            if _state["duration"]:
                remain = " · %d s left" % max(
                    0, int(_state["duration"] - elapsed))
            stats.setText(
                "Stressing · sent %d · target %.0f fps · actual ~%.0f fps"
                " · errors %d%s"
                % (_state["sent"], _state["target_fps"], actual,
                   _state["errors"], remain))

    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)

    _timer = QTimer()
    _timer.timeout.connect(_tick)

    refresher = QTimer()
    refresher.timeout.connect(_refresh)
    refresher.start(500)

    context.register_command(
        "canStress.open", plugin_shell.bind_raise(win),
        "Security: CAN Stress")

    win.show()
    sin.output.append(
        "CAN Stress loaded (no TX until Start; load-target limited)")


def deactivate():
    global _timer
    _state["running"] = False
    if _timer is not None:
        _timer.stop()
    sin.output.append("CAN Stress deactivated")
