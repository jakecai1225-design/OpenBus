# -*- coding: utf-8 -*-
"""obd2-scanner — OBD-II (ISO 15031-5 / SAE J1979) with ISO-TP.

Mode 01 live data, Mode 02 freeze frame, 03/04/07 DTCs, 09 VIN,
PID history table, English UI.
"""

from __future__ import annotations

import time
from collections import deque

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QTreeWidget,
    QTreeWidgetItem, QTextEdit, QMessageBox, QHeaderView, QTabWidget,
    QComboBox, QSpinBox, QTableWidget, QTableWidgetItem,
)

import sin
from _shared import plugin_shell, state_store
from _shared.isotp_client import IsotpClient

PLUGIN_ID = "obd2-scanner"


def _d_a(data):
    return data[0]


def _d_pct(data):
    return "%.1f" % (data[0] * 100.0 / 255.0)


def _d_temp(data):
    return data[0] - 40


def _d_rpm(data):
    return (data[0] * 256 + data[1]) // 4


def _d_speed(data):
    return data[0]


def _d_16_100(data):
    return "%.3f" % ((data[0] * 256 + data[1]) / 100.0)


def _d_16_1000(data):
    return "%.1f" % ((data[0] * 256 + data[1]) / 1000.0)


def _d_16_4(data):
    return (data[0] * 256 + data[1]) / 4.0


def _d_16_20(data):
    return "%.2f" % ((data[0] * 256 + data[1]) / 20.0)


def _d_afr(data):
    return "%.2f" % ((data[0] * 256 + data[1]) / 32768.0 * 14.7)


def _d_secs(data):
    return data[0] * 256 + data[1]


PIDS = {
    0x03: ("Fuel system status", lambda d: "0x%02X" % d[0], ""),
    0x04: ("Calculated load", _d_pct, "%"),
    0x05: ("Coolant temp", _d_temp, "C"),
    0x06: ("STFT B1", lambda d: "%.1f" % ((d[0] - 128) * 100.0 / 128.0), "%"),
    0x07: ("LTFT B1", lambda d: "%.1f" % ((d[0] - 128) * 100.0 / 128.0), "%"),
    0x0A: ("Fuel pressure", lambda d: d[0] * 3, "kPa"),
    0x0B: ("MAP", _d_a, "kPa"),
    0x0C: ("Engine RPM", _d_rpm, "rpm"),
    0x0D: ("Vehicle speed", _d_speed, "km/h"),
    0x0E: ("Timing advance", lambda d: "%.1f" % (d[0] / 2.0 - 64.0), "deg"),
    0x0F: ("Intake temp", _d_temp, "C"),
    0x10: ("MAF", _d_16_100, "g/s"),
    0x11: ("Throttle position", _d_pct, "%"),
    0x1F: ("Run time since start", _d_secs, "s"),
    0x21: ("Distance with MIL", _d_16_4, "km"),
    0x2F: ("Fuel level", _d_pct, "%"),
    0x31: ("Distance traveled", _d_16_4, "km"),
    0x33: ("Barometric pressure", _d_a, "kPa"),
    0x42: ("Control module voltage", _d_16_1000, "V"),
    0x43: ("Absolute load", lambda d: "%.1f" % (d[0] * 100.0 / 255.0 * 2.5), "%"),
    0x44: ("AFR (lambda->14.7)", _d_afr, ""),
    0x45: ("Relative throttle", _d_pct, "%"),
    0x46: ("Ambient temp", _d_temp, "C"),
    0x5C: ("Oil temp", _d_temp, "C"),
    0x5E: ("Fuel rate", _d_16_20, "L/h"),
}

DTC_TEXTS = {
    "P0300": "Random/multiple cylinder misfire",
    "P0420": "Catalyst efficiency below threshold (B1)",
    "P0171": "System too lean (B1)",
    "P0172": "System too rich (B1)",
    "U0100": "Lost communication with ECM/PCM",
}

_supported = {}
_values = {}
_history = {}  # pid -> deque of (ts, text)
_freeze = []   # [(pid, name, value, unit)]
_dtcs = []
_vin = ""
_log = []
_tx_id = 0x7DF
_rx_id = 0x7E8
_polling = False
_poll_interval_ms = 500
_running = True
_isotp = None
_pending_kind = None


def _lg(text):
    _log.append((time.time(), text))
    if len(_log) > 800:
        del _log[:400]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _decode_dtc(a, b):
    prefix = "PCBU"[(a >> 6) & 0x03]
    return "%s%04X" % (prefix, ((a & 0x3F) << 8) | b)


def _handle_pdu(pdu: bytes):
    global _vin, _pending_kind
    if not pdu:
        return
    mode = pdu[0] - 0x40
    ts = time.time()
    _lg("RX PDU mode=%02X: %s" % (mode, _hex(pdu[:32])))

    if mode in (1, 2) and len(pdu) >= 2:
        pid = pdu[1]
        payload = pdu[2:]
        if mode == 1:
            if pid in (0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0):
                base = pid + 1
                for i in range(min(32, len(payload) * 8)):
                    if payload[i // 8] & (0x80 >> (i % 8)):
                        _supported[base + i] = True
            elif pid == 0x01 and payload:
                mil = "ON" if payload[0] & 0x80 else "OFF"
                cnt = payload[0] & 0x7F
                text = "MIL:%s DTCs:%d" % (mil, cnt)
                _values[0x01] = (text, "", ts)
            elif pid in PIDS:
                name, fn, unit = PIDS[pid]
                try:
                    text = str(fn(payload))
                except Exception:
                    text = "decode err: %s" % _hex(payload)
                _values[pid] = (text, unit, ts)
                hist = _history.setdefault(pid, deque(maxlen=120))
                hist.append((ts, text))
        elif mode == 2:
            # Freeze frame: PID after frame number byte in some ECUs — keep simple
            frame_no = payload[0] if payload else 0
            rest = payload[1:] if len(payload) > 1 else b""
            if pid in PIDS and rest:
                name, fn, unit = PIDS[pid]
                try:
                    text = str(fn(rest))
                except Exception:
                    text = _hex(rest)
                _freeze.append((pid, name, text, unit, frame_no))
            elif pid == 0x02:
                _lg("Freeze frame number response: %s" % _hex(payload))
    elif mode in (3, 7) and len(pdu) >= 2:
        cnt = pdu[1]
        for i in range((len(pdu) - 2) // 2):
            code = _decode_dtc(pdu[2 + 2 * i], pdu[3 + 2 * i])
            _dtcs.append((code, DTC_TEXTS.get(code, ""), "03" if mode == 3 else "07"))
        _lg("DTCs counted=%d" % cnt)
    elif mode == 4:
        _lg("Clear DTCs acknowledged")
    elif mode == 9 and len(pdu) >= 2:
        info = pdu[1]
        if info == 0x02:
            # VIN may arrive as multi-byte after count
            raw = pdu[2:]
            if raw and raw[0] <= 0x05:
                raw = raw[1:]  # skip message count
            chars = "".join(chr(b) for b in raw if 0x20 <= b < 0x7F)
            if chars:
                _vin = (_vin + chars) if _vin and not chars.startswith("1") else chars
                if len(_vin) > 17:
                    _vin = _vin[-17:]
    _pending_kind = None


def activate(context):
    global _running, _polling, _tx_id, _rx_id, _isotp, _vin
    _supported.clear()
    _values.clear()
    _history.clear()
    del _freeze[:]
    del _dtcs[:]
    _log.clear()
    _vin = ""
    _polling = False
    _running = True

    win = sin.ui.create_window("OBD-II Scanner")
    win.resize(980, 660)
    plugin_shell.attach_status_bar(win, "Idle — requests only on button click")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    top.addWidget(QLabel("Request ID:"))
    req_combo = QComboBox()
    req_combo.addItem("0x7DF functional", 0x7DF)
    req_combo.addItem("0x7E0 physical", 0x7E0)
    top.addWidget(req_combo)
    top.addWidget(QLabel("Response ID:"))
    rx_spin = QSpinBox()
    rx_spin.setRange(0x7E8, 0x7EF)
    rx_spin.setDisplayIntegerBase(16)
    rx_spin.setPrefix("0x")
    rx_spin.setValue(0x7E8)
    top.addWidget(rx_spin)
    poll_spin = QSpinBox()
    poll_spin.setRange(100, 5000)
    poll_spin.setValue(_poll_interval_ms)
    poll_spin.setSuffix(" ms")
    top.addWidget(QLabel("Poll"))
    top.addWidget(poll_spin)
    top.addStretch(1)

    support_btn = QPushButton("Discover PIDs")
    dtc_btn = QPushButton("Read DTCs (03)")
    pending_btn = QPushButton("Pending (07)")
    clear_btn = QPushButton("Clear DTCs (04)")
    vin_btn = QPushButton("Read VIN")
    freeze_btn = QPushButton("Freeze frame (02)")
    poll_btn = QPushButton("Start poll")
    for b in (support_btn, dtc_btn, pending_btn, clear_btn, vin_btn, freeze_btn, poll_btn):
        top.addWidget(b)
    layout.addLayout(top)
    layout.addWidget(plugin_shell.help_label(
        "ISO-TP reassembles multi-frame VIN/DTC. Mode 02 fills Freeze tab. "
        "History keeps last samples per polled PID."))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    data_tab = QWidget()
    dv = QVBoxLayout(data_tab)
    val_tree = QTreeWidget()
    val_tree.setHeaderLabels(["PID", "Name", "Value", "Unit", "Time"])
    val_tree.setRootIsDecorated(False)
    val_tree.setAlternatingRowColors(True)
    dv.addWidget(val_tree, 1)
    export_btn = QPushButton("Export CSV")
    dv.addWidget(export_btn)
    tabs.addTab(data_tab, "Live data")

    hist_tab = QWidget()
    hv = QVBoxLayout(hist_tab)
    hist_table = QTableWidget(0, 3)
    hist_table.setHorizontalHeaderLabels(["Time", "PID", "Value"])
    hist_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
    hv.addWidget(hist_table)
    tabs.addTab(hist_tab, "PID history")

    freeze_tab = QWidget()
    fv = QVBoxLayout(freeze_tab)
    freeze_tree = QTreeWidget()
    freeze_tree.setHeaderLabels(["PID", "Name", "Value", "Unit", "Frame#"])
    freeze_tree.setRootIsDecorated(False)
    fv.addWidget(freeze_tree)
    tabs.addTab(freeze_tab, "Freeze frame")

    dtc_tab = QWidget()
    tv = QVBoxLayout(dtc_tab)
    dtc_tree = QTreeWidget()
    dtc_tree.setHeaderLabels(["Code", "Description", "Mode"])
    dtc_tree.setRootIsDecorated(False)
    tv.addWidget(dtc_tree)
    tabs.addTab(dtc_tab, "DTCs")

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view)
    tabs.addTab(log_tab, "Log")

    vin_label = QLabel("VIN: -")
    vin_label.setStyleSheet("font-weight:bold;")
    layout.addWidget(vin_label)

    def _send_frame(can_id, data):
        sin.frames.send(can_id, bytes(data))

    _isotp = IsotpClient(_send_frame, qt_parent=win)
    _isotp.tx_id = 0x7E0
    _isotp.func_id = 0x7DF
    _isotp.rx_id = 0x7E8
    _isotp.on_received = _handle_pdu
    _isotp.on_error = lambda m: _lg("ISO-TP: %s" % m)
    _isotp.on_log = lambda d, cid, fr, note: _lg("%s 0x%X %s [%s]" % (d, cid, _hex(fr), note))

    def _request(mode, pid=0, functional=True):
        global _pending_kind
        if mode in (1, 2, 9):
            pdu = bytes([mode, pid])
        else:
            pdu = bytes([mode])
        _isotp.tx_id = 0x7E0 if not functional else _tx_id
        _isotp.func_id = _tx_id
        _isotp.rx_id = _rx_id
        _pending_kind = (mode, pid)
        _isotp.send(pdu, functional=functional and _tx_id == 0x7DF)
        _lg("TX mode=%02X pid=%02X via ISO-TP" % (mode, pid))

    def on_frame(frame):
        if not _running:
            return
        if not (0x7E8 <= frame.id <= 0x7EF):
            return
        _isotp.rx_id = frame.id
        _isotp.on_frame(frame.id, bytes(frame.data))

    context.on_frame(on_frame)

    def refresh():
        val_tree.clear()
        for pid, (text, unit, ts) in sorted(_values.items()):
            name = "MIL / DTC count" if pid == 0x01 else (
                PIDS[pid][0] if pid in PIDS else "PID %02X" % pid)
            val_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%02X" % pid, name, text, unit, "%.1f" % ts]))
        freeze_tree.clear()
        for row in _freeze:
            freeze_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%02X" % row[0], row[1], row[2], row[3], str(row[4])]))
        dtc_tree.clear()
        for code, text, mode in _dtcs:
            dtc_tree.addTopLevelItem(QTreeWidgetItem([code, text, mode]))
        hist_table.setRowCount(0)
        for pid, hist in sorted(_history.items()):
            for ts, text in list(hist)[-40:]:
                r = hist_table.rowCount()
                hist_table.insertRow(r)
                hist_table.setItem(r, 0, QTableWidgetItem(time.strftime("%H:%M:%S", time.localtime(ts))))
                hist_table.setItem(r, 1, QTableWidgetItem("0x%02X" % pid))
                hist_table.setItem(r, 2, QTableWidgetItem(text))
        vin_label.setText("VIN: %s" % (_vin.strip() or "-"))
        if _log:
            log_view.setPlainText("\n".join(
                "[%s] %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), t)
                for ts, t in _log[-160:]))

    refresher = QTimer(win)
    refresher.timeout.connect(refresh)
    refresher.start(400)

    poll_timer = QTimer(win)

    def on_poll_tick():
        if not _polling:
            return
        targets = sorted(p for p in _supported if p in PIDS) or sorted(PIDS)
        for pid in targets[:12]:
            _request(1, pid, functional=(_tx_id == 0x7DF))

    poll_timer.timeout.connect(on_poll_tick)

    def on_support():
        _supported.clear()
        for base in (0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0):
            _request(1, base)

    def on_freeze():
        del _freeze[:]
        # Request freeze frame 0 for common PIDs
        for pid in (0x0C, 0x0D, 0x05, 0x04, 0x11):
            _request(2, pid, functional=False)

    def on_clear():
        if QMessageBox.question(
                win, "Confirm",
                "Clear DTCs and freeze frames?\nThis affects readiness monitors.") != \
                QMessageBox.StandardButton.Yes:
            return
        _request(4, 0)

    def on_poll():
        global _polling
        _polling = not _polling
        poll_btn.setText("Stop poll" if _polling else "Start poll")
        if _polling:
            poll_timer.start(poll_spin.value())
            plugin_shell.set_status(win, "Polling")
        else:
            poll_timer.stop()
            plugin_shell.set_status(win, "Idle")

    def on_export():
        rows = []
        for pid, (text, unit, ts) in sorted(_values.items()):
            name = PIDS[pid][0] if pid in PIDS else "PID %02X" % pid
            rows.append(["0x%02X" % pid, name, text, unit, "%.1f" % ts])
        path = plugin_shell.export_csv(
            win, ["PID", "Name", "Value", "Unit", "Time"], rows, "obd2_data.csv")
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    def on_tx_changed(index):
        global _tx_id
        _tx_id = req_combo.itemData(index) or 0x7DF
        _isotp.func_id = _tx_id
        state_store.save_state(PLUGIN_ID, {
            "tx_id": _tx_id, "rx_id": rx_spin.value(),
            "poll_ms": poll_spin.value(),
        }, "settings.json")

    def on_rx_changed(v):
        global _rx_id
        _rx_id = int(v)
        _isotp.rx_id = _rx_id

    context.register_command(
        "obd2Scanner.open", plugin_shell.bind_raise(win), "Diagnostic: OBD-II")

    req_combo.currentIndexChanged.connect(on_tx_changed)
    rx_spin.valueChanged.connect(on_rx_changed)
    support_btn.clicked.connect(on_support)
    dtc_btn.clicked.connect(lambda: _request(3, 0))
    pending_btn.clicked.connect(lambda: _request(7, 0))
    clear_btn.clicked.connect(on_clear)

    def on_vin():
        global _vin
        _vin = ""
        _request(9, 0x02, functional=False)

    vin_btn.clicked.connect(on_vin)
    freeze_btn.clicked.connect(on_freeze)
    poll_btn.clicked.connect(on_poll)
    export_btn.clicked.connect(on_export)
    plugin_shell.bind_shortcut(win, "Ctrl+E", on_export)

    saved = state_store.load_state(PLUGIN_ID, "settings.json") or {}
    if saved.get("tx_id"):
        idx = req_combo.findData(int(saved["tx_id"]))
        if idx >= 0:
            req_combo.setCurrentIndex(idx)
    if saved.get("rx_id"):
        rx_spin.setValue(int(saved["rx_id"]))
    if saved.get("poll_ms"):
        poll_spin.setValue(int(saved["poll_ms"]))

    win.show()
    sin.output.append("obd2-scanner ready (ISO-TP, freeze frame, PID history)")


def deactivate():
    global _running, _polling, _isotp
    _running = False
    _polling = False
    if _isotp:
        try:
            _isotp.shutdown()
        except Exception:
            pass
        _isotp = None
    sin.output.append("obd2-scanner deactivated")
