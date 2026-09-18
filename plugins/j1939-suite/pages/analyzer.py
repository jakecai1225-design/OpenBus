# -*- coding: utf-8 -*-
"""j1939-analyzer — SAE J1939 protocol analysis.

PGN / priority / SA / DA decode, built-in SPN tables, external J1939 DBC
SPN decode, DM1/DM2, Address Claim, TP BAM/RTS-CTS reassembly, RQST send.
"""

from __future__ import annotations

import os
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QTreeWidget,
    QTreeWidgetItem, QTextEdit, QHeaderView, QTabWidget, QLineEdit,
    QSpinBox, QGroupBox, QFormLayout, QMessageBox,
)

import sin
from _shared import dbcparse, dbc_picker, plugin_shell, state_store

SUITE_ID = "j1939-suite"
PAGE_KEY = "analyzer"

PGN_NAMES = {
    0xEE00: "Address Claimed",
    0xFECA: "DM1 Active DTCs",
    0xFECB: "DM2 Previously Active DTCs",
    0xF004: "EEC1 Electronic Engine Controller 1",
    0xFEF1: "CCVS Cruise Control / Vehicle Speed",
    0xFEEE: "ET1 Engine Temperature 1",
    0xFEE9: "LFE1 Fuel Economy",
    0xFEEA: "Engine Hours / Revolutions",
    0xF003: "EEC2 Electronic Engine Controller 2",
    0xFEF2: "CCVS2",
    0xFDC1: "EBC1 Electronic Brake Controller 1",
    0xFEF5: "Ambient Conditions",
    0xFEF7: "Air Inlet Temperature",
    0xFED0: "Vehicle Dynamics",
    0xFDA9: "CCVS3",
    0xEB00: "TP.DT Transport Protocol Data",
    0xEC00: "TP.CM Transport Protocol Command",
    0xEA00: "RQST Request",
    0xFE6C: "ET1 Engine Temperature Ext",
    0xFDC2: "EBC2 Electronic Brake Controller 2",
    0xFEF0: "CCVS1 Vehicle Speed",
}

LAMP_BITS = [
    (0, "Protect (red)"), (1, "Amber warning"), (2, "Red stop"),
    (3, "Malfunction (MIL)"),
]

FUNC_NAMES = {
    0: "Non-specific", 3: "Transmission", 4: "Instrument cluster",
    5: "Instrument #2", 8: "Axle retarder", 9: "Transmission retarder",
    11: "Engine retarder", 16: "Fuel system", 27: "Engine #2",
    30: "Engine #3", 32: "Power take-off", 33: "Engine (primary)",
    35: "Brake / ABS", 36: "Instrument cluster #2", 37: "Body controller",
    38: "Cab controller", 39: "Trailer brake", 40: "Vehicle management",
    45: "Lighting", 85: "Hybrid system", 98: "Battery management",
    128: "Aftertreatment #1", 129: "Aftertreatment #2",
    133: "Generator", 134: "Motor #1", 135: "Motor #2",
}

# PGN -> [(start_byte 1-based, length, SPN, name, factor, offset, unit)]
SPN_DECODES = {
    0xF004: [
        (2, 1, 512, "Driver demand torque %", 1, -125, "%"),
        (3, 1, 513, "Actual engine torque %", 1, -125, "%"),
        (4, 2, 190, "Engine speed", 0.125, 0, "rpm"),
    ],
    0xFEF1: [
        (1, 2, 84, "Wheel-based vehicle speed", 0.00390625, 0, "km/h"),
    ],
    0xFEEE: [
        (1, 1, 110, "Engine coolant temperature", 1, -40, "C"),
        (2, 1, 174, "Fuel temperature", 1, -40, "C"),
    ],
    0xFEE9: [
        (1, 2, 183, "Engine fuel rate", 0.05, 0, "L/h"),
    ],
    0xFEEA: [
        (1, 4, 247, "Total engine hours", 0.05, 0, "h"),
    ],
    0xFDC1: [
        (8, 2, 520, "Relative speed", 0.00390625, 0, "km/h"),
    ],
    0xFEF7: [
        (1, 1, 171, "Ambient air temperature", 1, -40, "C"),
        (3, 1, 172, "Air inlet temperature", 1, -40, "C"),
    ],
}

_sessions = {}       # (da, sa) -> {total, npkts, pgn, buf, ts, kind}
_pgn_stats = {}      # pgn -> {count, name, last_ts, sa}
_addr_claims = {}    # sa -> claim dict
_decoded = []        # (ts, pgn, text)
_tp_pdus = []        # (ts, sa, da, pgn, payload, kind)
_events = []
_total = 0
_running = True
_dbc = None
_dbc_path = ""
_dbc_by_pgn = {}     # pgn -> Message


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1500:
        del _events[:800]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _le(data, start, length):
    """1-based start byte, little-endian integer."""
    idx = start - 1
    if idx + length > len(data):
        return None
    v = 0
    for i in range(length):
        v |= data[idx + i] << (8 * i)
    return v


def _pgn_from_id(cid):
    pf = (cid >> 16) & 0xFF
    ps = (cid >> 8) & 0xFF
    return (pf << 8 | ps) if pf >= 0xF0 else (pf << 8)


def _pgn_name(pgn):
    if pgn in PGN_NAMES:
        return PGN_NAMES[pgn]
    msg = _dbc_by_pgn.get(pgn)
    if msg is not None:
        return msg.name
    return ""


def _rebuild_dbc_index():
    global _dbc_by_pgn
    _dbc_by_pgn = {}
    if _dbc is None:
        return
    for mid, msg in _dbc.messages.items():
        pgn = _pgn_from_id(mid)
        _dbc_by_pgn[pgn] = msg
        if mid <= 0x3FFFF:
            _dbc_by_pgn[mid & 0x3FFFF] = msg
        if mid <= 0xFFFF:
            _dbc_by_pgn[mid] = msg


def _msg_for_pgn(pgn, can_id=None):
    if _dbc is None:
        return None
    if can_id is not None and can_id in _dbc.messages:
        return _dbc.messages[can_id]
    if pgn in _dbc.messages:
        return _dbc.messages[pgn]
    return _dbc_by_pgn.get(pgn)


def _decode_spns_builtin(pgn, data):
    out = []
    for start, length, spn, name, factor, offset, unit in SPN_DECODES.get(pgn, []):
        raw = _le(data, start, length)
        if raw is None:
            continue
        out.append((spn, name, raw * factor + offset, unit))
    return out


def _decode_spns_dbc(pgn, data, can_id=None):
    msg = _msg_for_pgn(pgn, can_id)
    if msg is None:
        return []
    out = []
    decoded = dbcparse.decode_message(msg, data)
    for sig in msg.signals:
        if sig.name not in decoded:
            continue
        v = decoded[sig.name]
        unit = sig.unit or ""
        out.append((None, sig.name, v, unit))
    return out


def _decode_dm1(data):
    out = []
    if len(data) >= 1:
        lamp = data[0]
        lamps = [name for bit, name in LAMP_BITS if lamp & (1 << bit)]
        out.append((None, "Lamp status", ", ".join(lamps) if lamps else "All off", ""))
    n = (len(data) - 1) // 4 if len(data) > 1 else 0
    for i in range(n):
        chunk = data[1 + 4 * i:5 + 4 * i]
        spn = chunk[0] | (chunk[1] << 8) | ((chunk[2] & 0x07) << 16)
        fmi = (chunk[2] >> 3) & 0x1F
        oc = chunk[3] if chunk[3] < 127 else "N/A"
        out.append((spn, "DTC", "SPN%d / FMI%d / OC%s" % (spn, fmi, oc), ""))
    return out


def _decode_addr_claim(data):
    if len(data) < 8:
        return None
    ident = data[0] | (data[1] << 8) | ((data[2] & 0x1F) << 16)
    manufacturer = ((data[2] >> 5) | (data[3] << 3)) & 0x7FF
    function = data[4]
    return {
        "name_hex": _hex(data),
        "ident": ident,
        "manufacturer": manufacturer,
        "function": function,
        "function_name": FUNC_NAMES.get(function, "Function %d" % function),
    }


def _feed_tp(sa, da, data):
    if len(data) < 1:
        return None
    cmd = data[0]
    key = (da, sa)
    if cmd == 0x20:  # BAM
        if len(data) >= 8:
            total = data[1] | (data[2] << 8)
            npkts = data[3]
            pgn = data[5] | (data[6] << 8) | (data[7] << 16)
            _sessions[key] = {
                "total": total, "npkts": npkts, "pgn": pgn,
                "buf": {}, "ts": time.time(), "kind": "BAM",
            }
            _ev("TP", "BAM: SA=%02X DA=%02X PGN=0x%04X %d bytes %d packets"
                % (sa, da, pgn, total, npkts))
    elif cmd == 0x10:  # RTS
        if len(data) >= 8:
            total = data[1] | (data[2] << 8)
            npkts = data[3]
            pgn = data[5] | (data[6] << 8) | (data[7] << 16)
            _sessions[key] = {
                "total": total, "npkts": npkts, "pgn": pgn,
                "buf": {}, "ts": time.time(), "kind": "RTS",
            }
            _ev("TP", "RTS: SA=%02X DA=%02X PGN=0x%04X %d bytes"
                % (sa, da, pgn, total))
    elif cmd == 0x11:  # CTS
        _ev("TP", "CTS: SA=%02X DA=%02X" % (sa, da))
    elif cmd == 0x13:  # EndOfMsgAck
        _ev("TP", "EOMA: SA=%02X DA=%02X" % (sa, da))
    elif cmd == 0xFF:  # Abort
        reason = data[1] if len(data) > 1 else -1
        _ev("TP", "Abort: SA=%02X DA=%02X reason=%d" % (sa, da, reason))
        _sessions.pop(key, None)
    return None


def _feed_tp_dt(sa, da, data):
    key = (da, sa)
    st = _sessions.get(key)
    if not st or len(data) < 1:
        return None
    seq = data[0]
    st["buf"][seq] = bytes(data[1:])
    st["ts"] = time.time()
    have = sum(len(v) for v in st["buf"].values())
    if have >= st["total"] and len(st["buf"]) >= st["npkts"]:
        payload = b""
        for i in sorted(st["buf"].keys()):
            payload += st["buf"][i]
        payload = payload[:st["total"]]
        kind = st.get("kind", "TP")
        pgn = st["pgn"]
        del _sessions[key]
        _ev("TP", "Reassembled: SA=%02X PGN=0x%04X %d bytes (%s)"
            % (sa, pgn, len(payload), kind))
        return pgn, payload, kind
    return None


def _record_decoded(ts, pgn, text):
    _decoded.append((ts, pgn, text))
    if len(_decoded) > 3000:
        del _decoded[:1000]


def _apply_decodes(ts, pgn, data, can_id=None):
    if pgn in (0xFECA, 0xFECB):
        for spn, name, value, unit in _decode_dm1(data):
            _record_decoded(ts, pgn, "%s: %s" % (name, value))
    for spn, name, value, unit in _decode_spns_builtin(pgn, data):
        _record_decoded(
            ts, pgn, "SPN%d %s = %g %s" % (spn, name, value, unit))
    for spn, name, value, unit in _decode_spns_dbc(pgn, data, can_id):
        unit_s = (" " + unit) if unit else ""
        if isinstance(value, float):
            text = "%s = %g%s" % (name, value, unit_s)
        else:
            text = "%s = %s%s" % (name, value, unit_s)
        _record_decoded(ts, pgn, "DBC " + text)


def _on_frame(frame):
    global _total
    if not _running:
        return
    data = frame.data
    if not data or not getattr(frame, "extended", False):
        return
    _total += 1
    cid = frame.id
    pf = (cid >> 16) & 0xFF
    ps = (cid >> 8) & 0xFF
    sa = cid & 0xFF
    pgn = _pgn_from_id(cid)
    ts = frame.timestamp

    if pgn == 0xEC00:
        _feed_tp(sa, ps, data)
        st = _pgn_stats.setdefault(
            pgn, {"count": 0, "name": _pgn_name(pgn), "last_ts": ts, "sa": sa})
        st["count"] += 1
        st["last_ts"] = ts
        st["name"] = _pgn_name(pgn) or st["name"]
        return

    if pgn == 0xEB00:
        result = _feed_tp_dt(sa, ps, data)
        st = _pgn_stats.setdefault(
            pgn, {"count": 0, "name": _pgn_name(pgn), "last_ts": ts, "sa": sa})
        st["count"] += 1
        st["last_ts"] = ts
        if result:
            pgn2, payload, kind = result
            _tp_pdus.append((ts, sa, ps, pgn2, payload, kind))
            if len(_tp_pdus) > 500:
                del _tp_pdus[:200]
            _record_decoded(
                ts, pgn2,
                "TP %s %d bytes: %s" % (kind, len(payload), _hex(payload[:32])))
            _apply_decodes(ts, pgn2, payload)
        return

    if pgn == 0xEE00:
        claim = _decode_addr_claim(data)
        if claim:
            claim["ts"] = time.time()
            _addr_claims[sa] = claim
            _ev("AC", "Node %02X claimed: mfr=%d function=%s"
                % (sa, claim["manufacturer"], claim["function_name"]))

    st = _pgn_stats.setdefault(
        pgn, {"count": 0, "name": _pgn_name(pgn), "last_ts": ts, "sa": sa})
    st["count"] += 1
    st["last_ts"] = ts
    st["sa"] = sa
    st["name"] = _pgn_name(pgn) or st["name"]

    _apply_decodes(ts, pgn, data, can_id=cid)


def _load_dbc_path(path):
    global _dbc, _dbc_path
    if not path or not os.path.isfile(path):
        return False
    db = dbcparse.parse_file(path)
    if not db.messages:
        return False
    _dbc = db
    _dbc_path = path
    _rebuild_dbc_index()
    state_store.save_state(SUITE_ID, {"dbc_path": path}, "settings.json")
    return True


def build(parent, session, log_fn):
    global _running, _dbc, _dbc_path, _total
    _sessions.clear()
    _pgn_stats.clear()
    _addr_claims.clear()
    del _decoded[:]
    del _tp_pdus[:]
    del _events[:]
    _total = 0
    _running = True

    root = QWidget(parent)
    layout = QVBoxLayout(root)

    top = QHBoxLayout()
    summary = QLabel("No J1939 traffic yet")
    summary.setStyleSheet("font-weight:bold;")
    top.addWidget(summary, 1)
    dbc_label = QLabel("DBC: (none)")
    dbc_label.setStyleSheet("color:#78909c;")
    top.addWidget(dbc_label)
    dbc_btn = QPushButton("Load DBC…")
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    top.addWidget(dbc_btn)
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Monitors SAE J1939 extended frames. Load a J1939 DBC for SPN/signal "
        "decode beyond built-in tables. RQST sends PGN 0xEA00 (59904). "
        "TP tab shows BAM/RTS-CTS reassembled PDUs."))

    rqst_box = QGroupBox("RQST (PGN 59904 / 0xEA00)")
    rqst_form = QFormLayout(rqst_box)
    target_sa = QSpinBox()
    target_sa.setRange(0, 255)
    target_sa.setDisplayIntegerBase(16)
    target_sa.setPrefix("0x")
    target_sa.setValue(0x00)
    our_sa = QSpinBox()
    our_sa.setRange(0, 255)
    our_sa.setDisplayIntegerBase(16)
    our_sa.setPrefix("0x")
    our_sa.setValue(0xF9)
    pgn_edit = QLineEdit("0xF004")
    pgn_edit.setPlaceholderText("Requested PGN e.g. 0xF004 or 61444")
    send_rqst_btn = QPushButton("Send RQST")
    rqst_row = QHBoxLayout()
    rqst_row.addWidget(QLabel("Target SA"))
    rqst_row.addWidget(target_sa)
    rqst_row.addWidget(QLabel("Our SA"))
    rqst_row.addWidget(our_sa)
    rqst_row.addWidget(QLabel("PGN"))
    rqst_row.addWidget(pgn_edit, 1)
    rqst_row.addWidget(send_rqst_btn)
    rqst_form.addRow(rqst_row)
    layout.addWidget(rqst_box)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    empty = plugin_shell.empty_state_label(
        "No frames yet — start capture or wait for J1939 traffic on the bus.")

    pgn_tab = QWidget()
    pv = QVBoxLayout(pgn_tab)
    pgn_tree = QTreeWidget()
    pgn_tree.setHeaderLabels(["PGN", "Name", "Count", "Last SA", "Last time"])
    pgn_tree.setRootIsDecorated(False)
    pgn_tree.setAlternatingRowColors(True)
    pgn_tree.setSortingEnabled(True)
    pgn_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    pv.addWidget(empty)
    pv.addWidget(pgn_tree, 1)
    pgn_tree.hide()

    dec_tab = QWidget()
    dv = QVBoxLayout(dec_tab)
    dec_tree = QTreeWidget()
    dec_tree.setHeaderLabels(["Timestamp (s)", "PGN", "Decoded"])
    dec_tree.setRootIsDecorated(False)
    dec_tree.setAlternatingRowColors(True)
    dec_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    dv.addWidget(dec_tree, 1)

    tp_tab = QWidget()
    tv = QVBoxLayout(tp_tab)
    tp_tree = QTreeWidget()
    tp_tree.setHeaderLabels(
        ["Timestamp (s)", "Kind", "SA", "DA", "PGN", "Bytes", "Payload (hex)"])
    tp_tree.setRootIsDecorated(False)
    tp_tree.setAlternatingRowColors(True)
    tp_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tp_empty = plugin_shell.empty_state_label(
        "No reassembled TP PDUs yet (BAM / RTS-CTS).")
    tv.addWidget(tp_empty)
    tv.addWidget(tp_tree, 1)
    tp_tree.hide()

    addr_tab = QWidget()
    av = QVBoxLayout(addr_tab)
    addr_tree = QTreeWidget()
    addr_tree.setHeaderLabels(
        ["Source addr", "NAME (hex)", "Manufacturer", "Function code",
         "Function", "Claim time"])
    addr_tree.setRootIsDecorated(False)
    addr_tree.setAlternatingRowColors(True)
    addr_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    av.addWidget(addr_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(pgn_tab, "PGN stats")
    tabs.addTab(dec_tab, "Decoded")
    tabs.addTab(tp_tab, "TP PDUs")
    tabs.addTab(addr_tab, "Address claim")
    tabs.addTab(log_tab, "Event log")

    session.on_bus_frame(_on_frame)

    def _set_dbc_label():
        if _dbc_path:
            base = os.path.basename(_dbc_path)
            dbc_label.setText("DBC: %s (%d msgs)" % (base, len(_dbc.messages) if _dbc else 0))
            dbc_label.setStyleSheet("color:#2e7d32;")
        else:
            dbc_label.setText("DBC: (none)")
            dbc_label.setStyleSheet("color:#78909c;")

    def refresh():
        summary.setText(
            "Extended frames %d    PGNs %d    Decoded %d    Nodes %d    "
            "TP sessions %d    TP PDUs %d"
            % (_total, len(_pgn_stats), len(_decoded), len(_addr_claims),
               len(_sessions), len(_tp_pdus)))

        if _pgn_stats:
            empty.hide()
            pgn_tree.show()
        else:
            pgn_tree.hide()
            empty.show()

        pgn_tree.setSortingEnabled(False)
        pgn_tree.clear()
        for pgn, st in _pgn_stats.items():
            pgn_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%04X" % pgn, st["name"], str(st["count"]),
                "%02X" % st["sa"], "%.3f" % st["last_ts"]]))
        pgn_tree.setSortingEnabled(True)

        dec_tree.setSortingEnabled(False)
        dec_tree.clear()
        for ts, pgn, text in _decoded[-400:]:
            dec_tree.addTopLevelItem(QTreeWidgetItem([
                "%.3f" % ts, "0x%04X" % pgn, text]))
        dec_tree.scrollToBottom()
        dec_tree.setSortingEnabled(True)

        if _tp_pdus:
            tp_empty.hide()
            tp_tree.show()
        else:
            tp_tree.hide()
            tp_empty.show()
        tp_tree.clear()
        for ts, sa, da, pgn, payload, kind in _tp_pdus[-200:]:
            tp_tree.addTopLevelItem(QTreeWidgetItem([
                "%.3f" % ts, kind, "%02X" % sa, "%02X" % da,
                "0x%04X" % pgn, str(len(payload)), _hex(payload[:48])]))
        tp_tree.scrollToBottom()

        addr_tree.clear()
        for sa, c in sorted(_addr_claims.items()):
            addr_tree.addTopLevelItem(QTreeWidgetItem([
                "%02X" % sa, c["name_hex"], str(c["manufacturer"]),
                str(c["function"]), c["function_name"],
                time.strftime("%H:%M:%S", time.localtime(c["ts"]))]))

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (
                    time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-150:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

        plugin_shell.set_status(
            parent,
            "Live · %d frames · DBC %s"
            % (_total, os.path.basename(_dbc_path) if _dbc_path else "none"))

    timer = QTimer(root)
    timer.timeout.connect(refresh)
    timer.start(500)

    def on_load_dbc():
        path = dbc_picker.pick_dbc(parent, "Load J1939 DBC")
        if not path:
            return
        if not _load_dbc_path(path):
            QMessageBox.warning(parent, "DBC", "No messages found in file")
            return
        _set_dbc_label()
        plugin_shell.set_status(parent, "DBC loaded: %s" % path, 4000)
        _ev("DBC", "Loaded %s (%d messages)" % (path, len(_dbc.messages)))

    def on_clear():
        global _total
        _sessions.clear()
        _pgn_stats.clear()
        _addr_claims.clear()
        del _decoded[:]
        del _tp_pdus[:]
        del _events[:]
        _total = 0
        empty.show()
        pgn_tree.hide()
        tp_empty.show()
        tp_tree.hide()
        summary.setText("No J1939 traffic yet")
        plugin_shell.set_status(parent, "Cleared")

    def on_export():
        rows = []
        for pgn, st in sorted(_pgn_stats.items()):
            rows.append(["PGN", "0x%04X" % pgn, st["name"], st["count"], ""])
        for sa, c in sorted(_addr_claims.items()):
            rows.append([
                "AddressClaim", "%02X" % sa, c["name_hex"],
                c["manufacturer"], c["function_name"]])
        for ts, pgn, text in _decoded:
            rows.append(["Decoded", "%.3f" % ts, "0x%04X" % pgn, text, ""])
        for ts, sa, da, pgn, payload, kind in _tp_pdus:
            rows.append([
                "TP", "%.3f" % ts, kind,
                "SA=%02X DA=%02X PGN=0x%04X" % (sa, da, pgn),
                _hex(payload)])
        path = plugin_shell.export_csv(
            parent,
            ["Section", "Col1", "Col2", "Col3", "Col4"],
            rows,
            "j1939_analysis.csv",
        )
        if path:
            plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    def on_send_rqst():
        try:
            text = pgn_edit.text().strip()
            req_pgn = int(text, 0) if text else 0
        except ValueError:
            QMessageBox.warning(parent, "RQST", "Invalid PGN (use hex 0xF004 or decimal)")
            return
        req_pgn &= 0x3FFFF
        da = target_sa.value() & 0xFF
        sa = our_sa.value() & 0xFF
        priority = 6
        can_id = (priority << 26) | (0xEA << 16) | (da << 8) | sa
        data = bytes([
            req_pgn & 0xFF,
            (req_pgn >> 8) & 0xFF,
            (req_pgn >> 16) & 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        ])
        try:
            sin.frames.send(can_id, data, extended=True)
        except TypeError:
            sin.frames.send(can_id, data)
        _ev("RQST", "Sent to DA=%02X request PGN=0x%05X (ID=0x%08X)"
            % (da, req_pgn, can_id))
        plugin_shell.set_status(
            parent, "RQST sent → DA %02X PGN 0x%05X" % (da, req_pgn), 3000)


    dbc_btn.clicked.connect(on_load_dbc)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)
    send_rqst_btn.clicked.connect(on_send_rqst)
    plugin_shell.bind_shortcut(parent, "Ctrl+E", on_export)

    saved = state_store.load_state(SUITE_ID, "settings.json") or {}
    if saved.get("dbc_path"):
        if _load_dbc_path(saved["dbc_path"]):
            _set_dbc_label()
            plugin_shell.set_status(
                parent, "Restored DBC %s" % os.path.basename(_dbc_path), 3000)

    return root
