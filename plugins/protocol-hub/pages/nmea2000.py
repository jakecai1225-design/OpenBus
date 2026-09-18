# -*- coding: utf-8 -*-
"""nmea2000-decoder — NMEA 2000 marine PGN decoder.

29-bit ID decode, Fast Packet reassembly for known long PGNs,
built-in field decode for common navigation/engine PGNs, CSV export.
Passive monitor only.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QTextEdit, QHeaderView, QTabWidget,
    QMessageBox,
)

import sin
from _shared import plugin_shell, state_store

SUITE_ID = "protocol-hub"
PAGE_KEY = "nmea2000"

PGN_NAMES = {
    59392: "ISO Acknowledgement",
    59904: "ISO Request (RQST)",
    60928: "ISO Address Claimed",
    126992: "System Time",
    126996: "Product Information (long)",
    126998: "Configuration Information (long)",
    127245: "Rudder",
    127250: "Vessel Heading",
    127251: "Rate of Turn",
    127257: "Attitude",
    127488: "Engine Parameters Rapid",
    127493: "Transmission Parameters",
    127505: "Fluid Level",
    128259: "Speed (water-referenced)",
    128267: "Water Depth",
    129025: "Position Rapid Update",
    129026: "COG & SOG Rapid Update",
    129029: "GNSS Position Data (long)",
    129283: "Cross Track Error",
    129284: "Navigation Data (long)",
    129539: "GNSS DOPs (long)",
    129794: "AIS Class A Static/Voyage (long)",
    129793: "AIS Class A Position Report",
    129809: "AIS Class B Static Data (long)",
    129810: "AIS Class B Position Report",
    130306: "Wind Data",
    130310: "Environmental Parameters (legacy)",
    130311: "Environmental Parameters",
    130312: "Temperature",
    130314: "Actual Pressure",
    130316: "Temperature Extended Range (long)",
    130577: "Direction Data",
}

# pgn -> [(offset 0-based, length, name, factor, unit, note)]
PGN_DECODES = {
    127245: [(1, 2, "Rudder angle", 0.0001, "rad", "")],
    127250: [
        (1, 2, "Heading", 0.0001, "rad", ""),
        (3, 2, "Deviation", 0.0001, "rad", ""),
        (5, 2, "Variation", 0.0001, "rad", ""),
    ],
    127251: [(1, 4, "Rate of turn", 3.125e-06, "rad/s", "")],
    127488: [
        (1, 2, "Engine speed", 0.25, "rpm", ""),
        (3, 2, "Boost pressure", 0.1, "hPa", ""),
    ],
    127505: [(1, 4, "Fluid level", 0.0000039, "ratio", "0-100%")],
    128259: [
        (1, 2, "Speed water ref", 0.01, "kn", ""),
        (3, 2, "Speed longitudinal", 0.01, "m/s", ""),
    ],
    128267: [(1, 4, "Water depth", 0.01, "m", "")],
    129025: [
        (0, 4, "Latitude", 1e-07, "°", ""),
        (4, 4, "Longitude", 1e-07, "°", ""),
    ],
    129026: [
        (1, 2, "COG", 0.00573, "°", ""),
        (3, 2, "SOG", 0.01, "kn", ""),
    ],
    130306: [
        (1, 2, "Wind speed", 0.01, "m/s", ""),
        (3, 2, "Wind angle", 0.0001, "rad", ""),
    ],
    130311: [
        (1, 2, "Temperature", 0.01, "K", ""),
        (3, 2, "Humidity", 0.004, "%", ""),
    ],
    130312: [(1, 2, "Temperature", 0.01, "K", "")],
}

RAD_TO_DEG = 57.29577951308232
K_TO_C = -273.15

FAST_PACKET_PGNS = {
    126996, 126998, 129029, 129284, 129539, 129794, 129809, 130316,
}

_fp_sessions = {}
_pgn_stats = {}
_decoded = []
_events = []
_total = 0
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1000:
        del _events[:500]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _le(data, off, length):
    if off + length > len(data):
        return None
    v = 0
    for i in range(length):
        v |= data[off + i] << (8 * i)
    return v


def _decode(pgn, data):
    out = []
    for (off, length, name, factor, unit, _note) in PGN_DECODES.get(pgn, []):
        raw = _le(data, off, length)
        if raw is None:
            continue
        value = raw * factor
        if unit == "rad":
            out.append((name, "%.1f°" % (value * RAD_TO_DEG)))
        elif unit == "K":
            out.append((name, "%.1f °C" % (value + K_TO_C)))
        else:
            out.append((name, "%g %s" % (value, unit)))
    return out


def _on_frame(frame):
    global _total
    if not _running or not frame.extended:
        return
    data = frame.data
    if not data:
        return
    _total += 1
    cid = frame.id & 0x1FFFFFFF
    pf = (cid >> 16) & 0xFF
    sa = cid & 0xFF
    pgn = (cid >> 8) & 0x3FFFF
    if pf < 0xF0:
        pgn &= 0x3FF00
    ts = frame.timestamp

    st = _pgn_stats.setdefault(pgn, {
        "count": 0, "name": PGN_NAMES.get(pgn, ""),
        "last_ts": ts, "sa": sa,
    })
    st["count"] += 1
    st["last_ts"] = ts

    payload = data
    if pgn in FAST_PACKET_PGNS and len(data) == 8:
        idx = (data[0] >> 5) & 0x07
        counter = data[0] & 0x1F
        key = (pgn, sa)
        if idx == 0:
            expected = data[1]
            _fp_sessions[key] = {
                "expected": expected, "buf": {0: bytes(data[2:8])},
                "counter": counter, "ts": time.time(),
            }
            return
        sess = _fp_sessions.get(key)
        if sess is not None and sess.get("counter") == counter:
            sess["buf"][idx] = bytes(data[1:8])
            sess["ts"] = time.time()
            have = sum(len(v) for v in sess["buf"].values())
            if have >= sess["expected"]:
                payload = b""
                for i in sorted(sess["buf"].keys()):
                    payload += sess["buf"][i]
                payload = payload[:sess["expected"]]
                del _fp_sessions[key]
                _ev("FP", "PGN %d Fast Packet done %d B" % (pgn, len(payload)))
            else:
                return

    if pgn == 60928:
        _ev("AC", "Node %d Address Claimed NAME=%s" % (sa, _hex(data[:8])))

    for field, value in _decode(pgn, payload):
        _decoded.append((ts, pgn, "%s: %s" % (field, value)))
        if len(_decoded) > 2000:
            del _decoded[:800]


def build(parent, session, log_fn):
    global _running, _total
    _fp_sessions.clear()
    _pgn_stats.clear()
    del _decoded[:]
    del _events[:]
    _total = 0

    saved = state_store.load_state(SUITE_ID, "settings.json", default={}) or {}
    last_tab = int(saved.get("last_tab", 0) or 0)

    _running = True
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    top = QHBoxLayout()
    summary = QLabel("Waiting for NMEA 2000 (29-bit extended)…")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Passive NMEA 2000: Fast Packet reassembly for known long PGNs, "
        "field decode for heading/speed/position/depth/wind/engine. No TX."))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    dec_tab = QWidget()
    dv = QVBoxLayout(dec_tab)
    empty = plugin_shell.empty_state_label(
        "No decoded values yet — wait for known PGNs.")
    dec_tree = QTreeWidget()
    dec_tree.setHeaderLabels(["Timestamp (s)", "PGN", "Name", "Decoded"])
    dec_tree.setRootIsDecorated(False)
    dec_tree.setAlternatingRowColors(True)
    dec_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    dv.addWidget(empty)
    dv.addWidget(dec_tree, 1)
    dec_tree.hide()

    stat_tab = QWidget()
    sv = QVBoxLayout(stat_tab)
    stat_tree = QTreeWidget()
    stat_tree.setHeaderLabels(["PGN", "Name", "Frames", "Last SA", "Last time"])
    stat_tree.setRootIsDecorated(False)
    stat_tree.setAlternatingRowColors(True)
    stat_tree.setSortingEnabled(True)
    stat_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(stat_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(dec_tab, "Decoded stream")
    tabs.addTab(stat_tab, "PGN stats")
    tabs.addTab(log_tab, "Event log")
    if 0 <= last_tab < tabs.count():
        tabs.setCurrentIndex(last_tab)

    session.on_bus_frame(_on_frame)

    def _persist():
        state_store.save_state(SUITE_ID, {
            "last_tab": tabs.currentIndex(),
        }, "settings.json")

    def refresh():
        summary.setText(
            "Extended frames %d    PGNs %d    Decoded %d    Fast Packet %d"
            % (_total, len(_pgn_stats), len(_decoded), len(_fp_sessions)))

        if _decoded:
            empty.hide()
            dec_tree.show()
        else:
            dec_tree.hide()
            empty.show()

        dec_tree.setSortingEnabled(False)
        dec_tree.clear()
        for ts, pgn, text in _decoded[-400:]:
            dec_tree.addTopLevelItem(QTreeWidgetItem([
                "%.3f" % ts, str(pgn), PGN_NAMES.get(pgn, ""), text]))
        dec_tree.scrollToBottom()
        dec_tree.setSortingEnabled(True)

        stat_tree.setSortingEnabled(False)
        stat_tree.clear()
        for pgn, st in _pgn_stats.items():
            stat_tree.addTopLevelItem(QTreeWidgetItem([
                str(pgn), st["name"], str(st["count"]), str(st["sa"]),
                "%.3f" % st["last_ts"]]))
        stat_tree.setSortingEnabled(True)

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (
                    time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-120:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

        plugin_shell.set_status(
            parent, "Live · %d frames · %d PGNs · %d decoded"
            % (_total, len(_pgn_stats), len(_decoded)))

    timer = QTimer(root)
    timer.timeout.connect(refresh)
    timer.start(600)

    def on_clear():
        global _total
        _fp_sessions.clear()
        _pgn_stats.clear()
        del _decoded[:]
        del _events[:]
        _total = 0
        refresh()
        plugin_shell.set_status(parent, "Cleared", 3000)

    def on_export():
        rows = []
        for ts, pgn, text in _decoded:
            rows.append(["%.3f" % ts, pgn, PGN_NAMES.get(pgn, ""), text])
        for pgn, st in _pgn_stats.items():
            rows.append([pgn, st["name"], st["count"], ""])
        path = plugin_shell.export_csv(
            parent,
            ["Timestamp / PGN", "PGN / name", "Name / frames", "Decoded"],
            rows,
            "nmea2000_decode.csv",
        )
        if path:
            _persist()
            plugin_shell.set_status(parent, "Exported: %s" % path, 5000)
            QMessageBox.information(parent, "Export", "Saved:\n%s" % path)


    tabs.currentChanged.connect(lambda _=None: _persist())
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    return root
