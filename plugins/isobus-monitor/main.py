# -*- coding: utf-8 -*-
"""isobus-monitor — ISOBUS (ISO 11783) agricultural bus monitor.

29-bit ID decode, Address Claimed (PGN 0xEE00) NAME parse → online nodes,
TP.CM / TP.DT passive reassembly, common PGN names, stats + CSV.
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

PLUGIN_ID = "isobus-monitor"

PGN_NAMES = {
    0xEE00: "Address Claimed",
    0xFE08: "Cannot Claim Address",
    0xEC00: "TP.CM Transport Protocol Command",
    0xEB00: "TP.DT Transport Protocol Data",
    0xE700: "ECU → VT",
    0xE600: "VT → ECU",
    0xCB00: "TC server → client",
    0xCC00: "TC client → server",
    0xFE0C: "ECU → TC",
    0xFDDF: "Diagnostic Protocol ID",
    0xFDE6: "ECU → Diagnostic",
    0xFDE5: "Diagnostic → ECU",
    0xFEE6: "Time / Date",
    0xFE0D: "Working Set Master",
    0xC700: "VT Status",
    0xDA00: "ISO reserved",
    0xE500: "Language / Units",
    0xFEEB: "Software Identification (long)",
}

DEVICE_CLASSES = {
    0: "Non-specific", 1: "Non-specific implement", 2: "Tractor",
    3: "Harvester", 4: "Trailer", 5: "Implement trailer",
    6: "Self-propelled sprayer", 7: "Tractor + implement",
    8: "Non-vehicle unit", 9: "Sensor", 10: "Navigation",
    11: "Non-specific mobile", 12: "Engine", 13: "Power take-off",
    14: "Implement front", 15: "Non-specific vehicle", 16: "Generic controller",
    25: "Virtual Terminal (VT)", 126: "Task Controller (TC)",
    128: "TC-BAS", 129: "TC-GEO", 130: "TC-SC", 131: "TC-PRO", 132: "TC-TC",
}

FUNC_NAMES = {
    0: "Non-specific", 25: "Virtual Terminal", 126: "Task Controller",
    128: "TC basic server", 129: "TC geo server",
    130: "TC section control", 132: "TC task data server",
}

_sessions = {}
_nodes = {}
_pgn_stats = {}
_events = []
_decoded = []
_total = 0
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1200:
        del _events[:600]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _decode_name(data):
    if len(data) < 8:
        return None
    ident = data[0] | (data[1] << 8) | ((data[2] & 0x1F) << 16)
    manufacturer = ((data[2] >> 5) | (data[3] << 3)) & 0x7FF
    ecu_instance = (data[4] & 0x07)
    function_instance = (data[4] >> 3) & 0x0F
    function = data[5]
    device_class = data[6] & 0x3F
    device_class_instance = (data[6] >> 6) & 0x03
    industry_group = (data[7] & 0x07)
    sca = bool(data[7] & 0x80)
    return {
        "name_hex": _hex(data),
        "ident": ident,
        "manufacturer": manufacturer,
        "ecu_instance": ecu_instance,
        "function": function,
        "function_name": FUNC_NAMES.get(function, "Function %d" % function),
        "function_instance": function_instance,
        "device_class": device_class,
        "device_class_name": DEVICE_CLASSES.get(
            device_class, "Device class %d" % device_class),
        "device_class_instance": device_class_instance,
        "industry_group": industry_group,
        "self_configurable": sca,
    }


def _feed_tp(sa, da, data):
    if not data:
        return None
    key = (da, sa)
    cmd = data[0]
    if cmd == 0x20 and len(data) >= 8:
        total = data[1] | (data[2] << 8)
        npkts = data[3]
        pgn = data[5] | (data[6] << 8) | (data[7] << 16)
        _sessions[key] = {"total": total, "npkts": npkts, "pgn": pgn, "buf": {}}
        _ev("TP", "BAM: SA=%02X PGN=0x%04X %d B" % (sa, pgn, total))
        return None
    if cmd == 0x10 and len(data) >= 8:
        total = data[1] | (data[2] << 8)
        npkts = data[3]
        pgn = data[5] | (data[6] << 8) | (data[7] << 16)
        _sessions[key] = {"total": total, "npkts": npkts, "pgn": pgn, "buf": {}}
        _ev("TP", "RTS: SA=%02X DA=%02X PGN=0x%04X %d B" % (sa, da, pgn, total))
        return None
    if cmd == 0x17:
        _ev("TP", "CTS: SA=%02X DA=%02X" % (sa, da))
        return None
    if cmd == 0x13:
        _ev("TP", "EOMA: SA=%02X DA=%02X" % (sa, da))
        return None
    if cmd == 0xFF:
        _ev("TP", "Abort: SA=%02X reason=%d" % (
            sa, data[1] if len(data) > 1 else -1))
        return None
    return None


def _feed_tp_dt(sa, da, data):
    key = (da, sa)
    st = _sessions.get(key)
    if not st or not data:
        return None
    st["buf"][data[0]] = bytes(data[1:])
    have = sum(len(v) for v in st["buf"].values())
    if have >= st["total"] and len(st["buf"]) >= st["npkts"]:
        payload = b""
        for i in sorted(st["buf"].keys()):
            payload += st["buf"][i]
        payload = payload[:st["total"]]
        del _sessions[key]
        return st["pgn"], payload
    return None


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
    ps = (cid >> 8) & 0xFF
    sa = cid & 0xFF
    pgn = (pf << 8 | ps) if pf >= 0xF0 else (pf << 8)
    da = None if pf >= 0xF0 else ps
    ts = frame.timestamp

    st = _pgn_stats.setdefault(pgn, {
        "count": 0, "name": PGN_NAMES.get(pgn, ""),
        "last_ts": ts, "sa": sa, "da": da,
    })
    st["count"] += 1
    st["last_ts"] = ts

    if pgn == 0xEC00:
        _feed_tp(sa, ps, data)
        return
    if pgn == 0xEB00:
        result = _feed_tp_dt(sa, ps, data)
        if result:
            pgn2, payload = result
            _ev("TP", "Reassembled SA=%02X PGN=0x%04X %d B: %s" % (
                sa, pgn2, len(payload), _hex(payload[:24])))
        return

    if pgn == 0xEE00:
        info = _decode_name(data)
        if info:
            info["ts"] = time.time()
            _nodes[sa] = info
            _ev("AC", "Node %02X claimed: %s / %s (mfr %d)" % (
                sa, info["device_class_name"], info["function_name"],
                info["manufacturer"]))
            _decoded.append((
                ts, "Address Claimed",
                "SA=%02X %s/%s mfr%d" % (
                    sa, info["device_class_name"], info["function_name"],
                    info["manufacturer"])))
    elif pgn in PGN_NAMES:
        _decoded.append((ts, PGN_NAMES[pgn], "SA=%02X %s" % (sa, _hex(data[:16]))))
    if len(_decoded) > 1500:
        del _decoded[:600]


def activate(context):
    global _running, _total
    _sessions.clear()
    _nodes.clear()
    _pgn_stats.clear()
    del _events[:]
    del _decoded[:]
    _total = 0

    saved = state_store.load_state(PLUGIN_ID, "settings.json", default={}) or {}
    last_tab = int(saved.get("last_tab", 0) or 0)

    _running = True
    win = sin.ui.create_window("ISOBUS (ISO 11783) Monitor")
    win.resize(960, 630)
    plugin_shell.attach_status_bar(win, "Waiting for ISOBUS extended frames…")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    summary = QLabel("Waiting for ISOBUS (29-bit extended)…")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Passive ISO 11783: Address Claimed NAME → online nodes, "
        "TP.CM/TP.DT reassembly, common VT/TC PGN stats. No TX."))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    node_tab = QWidget()
    nv = QVBoxLayout(node_tab)
    empty = plugin_shell.empty_state_label(
        "No address claims yet — wait for PGN 0xEE00.")
    node_tree = QTreeWidget()
    node_tree.setHeaderLabels([
        "Source addr", "NAME hex", "Device class", "Function",
        "Manufacturer", "Identity", "Claim time",
    ])
    node_tree.setRootIsDecorated(False)
    node_tree.setAlternatingRowColors(True)
    node_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    nv.addWidget(empty)
    nv.addWidget(node_tree, 1)
    node_tree.hide()

    pgn_tab = QWidget()
    pv = QVBoxLayout(pgn_tab)
    pgn_tree = QTreeWidget()
    pgn_tree.setHeaderLabels(["PGN", "Name", "Frames", "Last SA", "Last time"])
    pgn_tree.setRootIsDecorated(False)
    pgn_tree.setAlternatingRowColors(True)
    pgn_tree.setSortingEnabled(True)
    pgn_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    pv.addWidget(pgn_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(node_tab, "Online nodes")
    tabs.addTab(pgn_tab, "PGN stats")
    tabs.addTab(log_tab, "Event log")
    if 0 <= last_tab < tabs.count():
        tabs.setCurrentIndex(last_tab)

    context.on_frame(_on_frame)

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "last_tab": tabs.currentIndex(),
        }, "settings.json")

    def refresh():
        summary.setText(
            "Extended frames %d    Nodes %d    PGNs %d    TP sessions %d"
            % (_total, len(_nodes), len(_pgn_stats), len(_sessions)))

        if _nodes:
            empty.hide()
            node_tree.show()
        else:
            node_tree.hide()
            empty.show()

        node_tree.clear()
        for sa, n in sorted(_nodes.items()):
            node_tree.addTopLevelItem(QTreeWidgetItem([
                "%02X" % sa, n["name_hex"], n["device_class_name"],
                n["function_name"], str(n["manufacturer"]), str(n["ident"]),
                time.strftime("%H:%M:%S", time.localtime(n["ts"]))]))

        pgn_tree.setSortingEnabled(False)
        pgn_tree.clear()
        for pgn, st in _pgn_stats.items():
            pgn_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%04X" % pgn, st["name"], str(st["count"]),
                "%02X" % st["sa"], "%.3f" % st["last_ts"]]))
        pgn_tree.setSortingEnabled(True)

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (
                    time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-150:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

        plugin_shell.set_status(
            win, "Live · %d frames · %d nodes · %d PGNs"
            % (_total, len(_nodes), len(_pgn_stats)))

    timer = QTimer(win)
    timer.timeout.connect(refresh)
    timer.start(600)

    def on_clear():
        global _total
        _sessions.clear()
        _nodes.clear()
        _pgn_stats.clear()
        del _events[:]
        del _decoded[:]
        _total = 0
        refresh()
        plugin_shell.set_status(win, "Cleared", 3000)

    def on_export():
        rows = []
        for sa, n in sorted(_nodes.items()):
            rows.append([
                "%02X" % sa, n["name_hex"], n["device_class_name"],
                n["function_name"], n["manufacturer"], n["ident"],
            ])
        for pgn, st in _pgn_stats.items():
            rows.append(["0x%04X" % pgn, st["name"], st["count"], "", "", ""])
        path = plugin_shell.export_csv(
            win,
            ["SA / PGN", "NAME / name", "Device class / frames",
             "Function", "Manufacturer", "Identity"],
            rows,
            "isobus_monitor.csv",
        )
        if path:
            _persist()
            plugin_shell.set_status(win, "Exported: %s" % path, 5000)
            QMessageBox.information(win, "Export", "Saved:\n%s" % path)

    raise_fn = plugin_shell.bind_raise(win)
    context.register_command(
        "isobusMonitor.open", raise_fn, "Protocol: ISOBUS Monitor")

    tabs.currentChanged.connect(lambda _=None: _persist())
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append(
        "ISOBUS monitor loaded (address claim + TP reassembly)")


def deactivate():
    global _running
    _running = False
    sin.output.append("ISOBUS monitor deactivated")
