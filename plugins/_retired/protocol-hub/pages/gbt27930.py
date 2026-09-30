# -*- coding: utf-8 -*-
"""gbt27930-monitor — GB/T 27930 EV charging protocol monitor.

BMS (0xF4) / charger (0x56) message ID table, field decode for key PDUs,
J1939 TP BAM/RTS-CTS reassembly for long messages, charge-stage FSM,
stats + CSV. Passive monitor only.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QTextEdit, QHeaderView, QTabWidget,
    QFrame, QMessageBox,
)

import sin
from _shared import plugin_shell, state_store

SUITE_ID = "protocol-hub"
PAGE_KEY = "gbt27930"

MSG_TABLE = {
    0x1827F456: "CHM Charger handshake",
    0x182756F4: "BHM BMS handshake",
    0x1801F456: "CRM Charger recognition",
    0x1CEB56F4: "BRM BMS recognition (long)",
    0x180156F4: "BCP Battery charge parameters",
    0x1802F456: "CML Charger max output",
    0x180456F4: "BRO Battery ready",
    0x1804F456: "CRO Charger ready",
    0x180556F4: "BCL Battery charge demand",
    0x1CEC56F4: "BCS Battery charge status (long)",
    0x1806F456: "CCS Charger charge status",
    0x180756F4: "BSM Battery status",
    0x1808F456: "CST Charger terminate",
    0x180856F4: "BST BMS terminate",
    0x180956F4: "BSD BMS statistics",
    0x1809F456: "CSD Charger statistics (long)",
    0x180B56F4: "BEM BMS error",
    0x180BF456: "CEM Charger error",
}

STAGES = [
    ("Handshake", ["CHM", "BHM"]),
    ("Recognition", ["CRM", "BRM"]),
    ("Parameter", ["BCP", "CTS", "CML", "BRO", "CRO"]),
    ("Charging", ["BCL", "BCS", "CCS", "BSM"]),
    ("End", ["BST", "CST", "BSD", "CSD", "BEM", "CEM"]),
]

_sessions = {}
_stats = {}
_decoded = []
_events = []
_stage = "Unknown"
_total = 0
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1200:
        del _events[:600]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _le(data, start, length, scale=1.0, offset=0.0):
    idx = start - 1
    if idx + length > len(data):
        return None
    v = 0
    for i in range(length):
        v |= data[idx + i] << (8 * i)
    return v * scale + offset


def _decode_fields(cid, data):
    out = []
    if cid == 0x1827F456:
        v = _le(data, 2, 2, 0.1)
        out.append(("Protocol version", "GB/T 27930-%d" % data[0] if data else "?"))
        out.append(("Max allowed charge voltage", "%.1f V" % v if v is not None else "?"))
    elif cid == 0x182756F4:
        v = _le(data, 1, 2, 0.1)
        c = _le(data, 3, 2, 0.1)
        out.append(("Max allowed total voltage", "%.1f V" % v if v is not None else "?"))
        out.append(("Battery rated capacity", "%.1f Ah" % c if c is not None else "?"))
    elif cid == 0x1801F456:
        out.append((
            "Recognition result",
            "OK (0xAA)" if data and data[0] == 0xAA
            else "In progress (0x%02X)" % (data[0] if data else 0)))
        out.append(("Charger number", "%d" % data[1] if len(data) > 1 else "?"))
        out.append(("Recognition msg number", "%d" % data[2] if len(data) > 2 else "?"))
    elif cid == 0x180456F4:
        out.append((
            "Battery ready",
            "Ready (0xAA)" if data and data[0] == 0xAA
            else "Not ready (0x%02X)" % (data[0] if data else 0)))
    elif cid == 0x1804F456:
        out.append((
            "Charger ready",
            "Ready (0xAA)" if data and data[0] == 0xAA
            else "Not ready (0x%02X)" % (data[0] if data else 0)))
    elif cid == 0x180556F4:
        v = _le(data, 1, 2, 0.1)
        i = _le(data, 3, 2, 0.1, -400)
        out.append(("Demand voltage", "%.1f V" % v if v is not None else "?"))
        out.append(("Demand current", "%.1f A" % i if i is not None else "?"))
        if len(data) > 5:
            out.append(("Charge mode", {1: "CC", 2: "CV"}.get(data[5], "%d" % data[5])))
    elif cid == 0x1806F456:
        v = _le(data, 1, 2, 0.1)
        i = _le(data, 3, 2, 0.1, -400)
        out.append(("Output voltage", "%.1f V" % v if v is not None else "?"))
        out.append(("Output current", "%.1f A" % i if i is not None else "?"))
        if len(data) > 5:
            mins = data[5] | (data[6] << 8) if len(data) > 6 else data[5]
            out.append(("Elapsed charge time", "%d min" % mins))
    elif cid == 0x180756F4:
        v = _le(data, 1, 2, 0.01)
        t = data[3] if len(data) > 3 else None
        out.append(("Max cell voltage", "%.2f V" % v if v is not None else "?"))
        out.append(("Max battery temperature", "%d °C" % t if t is not None else "?"))
        if len(data) > 4:
            out.append(("Min voltage probe index", "%d" % data[4]))
        if len(data) > 5:
            out.append(("SOC estimate", "%d %%" % (
                data[5] // 2 if data[5] <= 200 else data[5])))
    elif cid in (0x180856F4, 0x1808F456):
        if data:
            out.append(("Terminate reason bytes", _hex(data[:min(4, len(data))])))
    return out


def _feed_tp(sa, da, data):
    if not data:
        return None
    key = (da, sa)
    cmd = data[0]
    if cmd in (0x20, 0x10) and len(data) >= 8:
        total = data[1] | (data[2] << 8)
        npkts = data[3]
        pgn = data[5] | (data[6] << 8) | (data[7] << 16)
        _sessions[key] = {"total": total, "npkts": npkts, "pgn": pgn, "buf": {}}
        _ev("TP", "%s: SA=%02X PGN=0x%04X %d B" % (
            "BAM" if cmd == 0x20 else "RTS", sa, pgn, total))
        return None
    if cmd == 0xFF:
        _ev("TP", "Abort: SA=%02X" % sa)
        return None
    st = _sessions.get(key)
    if not st:
        return None
    seq = data[0]
    st["buf"][seq] = bytes(data[1:])
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
    global _total, _stage
    if not _running or not frame.extended:
        return
    data = frame.data
    if not data:
        return
    cid = frame.id & 0x1FFFFFFF
    ts = frame.timestamp
    pf = (cid >> 16) & 0xFF
    ps = (cid >> 8) & 0xFF
    sa = cid & 0xFF
    pgn = (pf << 8 | ps) if pf >= 0xF0 else (pf << 8)

    if pgn in (0xEC00, 0xEB00):
        _total += 1
        if pgn == 0xEC00:
            _feed_tp(sa, ps, data)
        else:
            result = _feed_tp(sa, ps, data)
            if result:
                pgn2, payload = result
                name = "TP reassembled PGN 0x%04X" % pgn2
                _decoded.append((
                    ts, name, "%d B: %s" % (len(payload), _hex(payload[:24]))))
                if len(_decoded) > 2000:
                    del _decoded[:800]
        return

    name = MSG_TABLE.get(cid)
    if name is None:
        if sa in (0xF4, 0x56):
            name = "PGN 0x%04X SA=%02X" % (pgn, sa)
        else:
            return
    _total += 1
    st = _stats.setdefault(cid, {"name": name, "count": 0, "last_ts": ts})
    st["count"] += 1
    st["last_ts"] = ts

    for field, text in _decode_fields(cid, data):
        _decoded.append((ts, name, "%s: %s" % (field, text)))
    if len(_decoded) > 3000:
        del _decoded[:1000]

    for stage, keys in STAGES:
        if any(name.startswith(k) for k in keys):
            if _stage != stage:
                _stage = stage
                _ev("STAGE", "Entered stage: %s (%s)" % (stage, name))
            break


def build(parent, session, log_fn):
    global _running, _stage, _total
    _sessions.clear()
    _stats.clear()
    del _decoded[:]
    del _events[:]
    _stage = "Unknown"
    _total = 0

    saved = state_store.load_state(SUITE_ID, "settings.json", default={}) or {}
    last_tab = int(saved.get("last_tab", 0) or 0)

    _running = True
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    stage_row = QHBoxLayout()
    stage_labels = {}
    for i, (stage, _) in enumerate(STAGES):
        box = QFrame()
        box.setFixedHeight(38)
        box.setStyleSheet(
            "QFrame{border:1px solid #bbb;border-radius:6px;background:#f2f2f2;}")
        lbl = QLabel(stage)
        lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay = QHBoxLayout(box)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(lbl)
        stage_labels[stage] = (box, lbl)
        stage_row.addWidget(box, 1)
        if i < len(STAGES) - 1:
            arrow = QLabel("→")
            arrow.setFixedWidth(18)
            stage_row.addWidget(arrow)
    layout.addLayout(stage_row)

    top = QHBoxLayout()
    summary = QLabel(
        "Waiting for charge frames (ext. ID, BMS=0xF4 / charger=0x56)…")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Passive GB/T 27930: CHM→CST message table, key field decode, "
        "TP reassembly for long PDUs, handshake→charge→end stage FSM. No TX."))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    dec_tab = QWidget()
    dv = QVBoxLayout(dec_tab)
    empty = plugin_shell.empty_state_label(
        "No decoded charge messages yet.")
    dec_tree = QTreeWidget()
    dec_tree.setHeaderLabels(["Timestamp (s)", "Message", "Decoded"])
    dec_tree.setRootIsDecorated(False)
    dec_tree.setAlternatingRowColors(True)
    dec_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    dv.addWidget(empty)
    dv.addWidget(dec_tree, 1)
    dec_tree.hide()

    stat_tab = QWidget()
    sv = QVBoxLayout(stat_tab)
    stat_tree = QTreeWidget()
    stat_tree.setHeaderLabels(["CAN ID", "Message", "Frames", "Last time"])
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
    tabs.addTab(stat_tab, "Message stats")
    tabs.addTab(log_tab, "Event log")
    if 0 <= last_tab < tabs.count():
        tabs.setCurrentIndex(last_tab)

    session.on_bus_frame(_on_frame)

    ACTIVE_CSS = (
        "QFrame{border:2px solid #16a34a;border-radius:6px;"
        "background:#dcfce7;font-weight:bold;}")
    IDLE_CSS = (
        "QFrame{border:1px solid #bbb;border-radius:6px;background:#f2f2f2;}")

    def _persist():
        state_store.save_state(SUITE_ID, {
            "last_tab": tabs.currentIndex(),
        }, "settings.json")

    def refresh():
        summary.setText(
            "Extended frames %d    Message types %d    Decoded %d    Stage: %s"
            % (_total, len(_stats), len(_decoded), _stage))
        for stage, (box, _lbl) in stage_labels.items():
            box.setStyleSheet(ACTIVE_CSS if stage == _stage else IDLE_CSS)

        if _decoded:
            empty.hide()
            dec_tree.show()
        else:
            dec_tree.hide()
            empty.show()

        dec_tree.setSortingEnabled(False)
        dec_tree.clear()
        for ts, name, text in _decoded[-400:]:
            dec_tree.addTopLevelItem(QTreeWidgetItem([
                "%.3f" % ts, name, text]))
        dec_tree.scrollToBottom()
        dec_tree.setSortingEnabled(True)

        stat_tree.setSortingEnabled(False)
        stat_tree.clear()
        for cid, st in _stats.items():
            stat_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%08X" % cid, st["name"], str(st["count"]),
                "%.3f" % st["last_ts"]]))
        stat_tree.setSortingEnabled(True)

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (
                    time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-150:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

        plugin_shell.set_status(
            parent, "Live · %d frames · stage %s" % (_total, _stage))

    timer = QTimer(root)
    timer.timeout.connect(refresh)
    timer.start(600)

    def on_clear():
        global _stage, _total
        _sessions.clear()
        _stats.clear()
        del _decoded[:]
        del _events[:]
        _stage = "Unknown"
        _total = 0
        refresh()
        plugin_shell.set_status(parent, "Cleared", 3000)

    def on_export():
        rows = []
        for ts, name, text in _decoded:
            rows.append(["%.3f" % ts, name, text])
        for cid, st in _stats.items():
            rows.append(["0x%08X" % cid, st["name"], st["count"]])
        path = plugin_shell.export_csv(
            parent,
            ["Timestamp / CAN ID", "Message", "Decoded / frames"],
            rows,
            "gbt27930_monitor.csv",
        )
        if path:
            _persist()
            plugin_shell.set_status(parent, "Exported: %s" % path, 5000)
            QMessageBox.information(parent, "Export", "Saved:\n%s" % path)


    tabs.currentChanged.connect(lambda _=None: _persist())
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    return root
