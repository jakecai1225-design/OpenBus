# -*- coding: utf-8 -*-
"""j1939-analyzer — SAE J1939 protocol analysis.

PGN / priority / SA / DA decode, built-in SPN tables, external J1939 DBC
SPN decode, DM1/DM2, Address Claim, TP BAM/RTS-CTS reassembly, RQST send.
"""

from __future__ import annotations

import os
import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QGuiApplication
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QTreeWidget,
    QTreeWidgetItem, QTextEdit, QHeaderView, QTabWidget, QLineEdit,
    QSpinBox, QMessageBox, QMenu,
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
_dm_rows = []        # (ts, pgn, sa, spn, text, lamps)
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


def snapshot_tp():
    return list(_tp_pdus)


def snapshot_dm():
    return list(_dm_rows)


def snapshot_addr():
    return sorted(_addr_claims.items())


def _store_dm(ts, pgn, sa, data):
    lamps = ""
    for spn, name, value, _unit in _decode_dm1(data):
        if name == "Lamp status":
            lamps = str(value)
        elif name == "DTC":
            _dm_rows.append((ts, pgn, sa, spn, str(value), lamps))
    if len(_dm_rows) > 800:
        del _dm_rows[:300]


def _apply_decodes(ts, pgn, data, can_id=None, sa=0):
    if pgn in (0xFECA, 0xFECB):
        _store_dm(ts, pgn, sa, data)
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
            _apply_decodes(ts, pgn2, payload, sa=sa)
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

    _apply_decodes(ts, pgn, data, can_id=cid, sa=sa)


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


def load_dbc(path: str) -> bool:
    """Public wrapper for File menu / run_action."""
    return _load_dbc_path(path)


def build(parent, session, log_fn):
    global _running, _dbc, _dbc_path, _total
    _sessions.clear()
    _pgn_stats.clear()
    _addr_claims.clear()
    del _decoded[:]
    del _tp_pdus[:]
    del _dm_rows[:]
    del _events[:]
    _total = 0
    _running = True

    from pages import _ui

    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    summary = _ui.muted_label("No J1939 traffic yet")
    summary.setToolTip("Live frame / PGN summary")
    dbc_label = _ui.muted_label("DBC: (none)")
    dbc_label.setToolTip("Loaded J1939 DBC")
    dbc_btn = _ui.ghost_btn("Load DBC…", "Load J1939 DBC for SPN decode", "export")
    clear_btn = _ui.ghost_btn("Clear", "Clear live tables", "clear")
    export_btn = _ui.ghost_btn("Export CSV", "Export analysis CSV", "export")
    filter_edit = QLineEdit()
    filter_edit.setPlaceholderText("Filter PGN / name…")
    filter_edit.setClearButtonEnabled(True)
    filter_edit.setToolTip("Filter PGN stats")
    layout.addWidget(_ui.tool_strip(
        summary, dbc_label, dbc_btn, clear_btn, export_btn))
    layout.addWidget(_ui.inline_filter(filter_edit))

    target_sa = QSpinBox()
    target_sa.setRange(0, 255)
    target_sa.setDisplayIntegerBase(16)
    target_sa.setPrefix("0x")
    target_sa.setValue(0x00)
    target_sa.setToolTip("RQST target SA")
    our_sa = QSpinBox()
    our_sa.setRange(0, 255)
    our_sa.setDisplayIntegerBase(16)
    our_sa.setPrefix("0x")
    our_sa.setValue(0xF9)
    our_sa.setToolTip("Our source address")
    pgn_edit = QLineEdit("0xF004")
    pgn_edit.setPlaceholderText("Requested PGN e.g. 0xF004")
    pgn_edit.setToolTip("PGN to request via 0xEA00")
    send_rqst_btn = _ui.primary_btn("Send RQST", "Send PGN request", "arrow-right")
    layout.addWidget(_ui.tool_strip(
        _ui.field_label("RQST"), _ui.field_label("Target"), target_sa,
        _ui.field_label("Our SA"), our_sa, _ui.field_label("PGN"), pgn_edit,
        send_rqst_btn))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)
    _ui.style_page_tabs(tabs)

    empty = _ui.empty_state(
        "No frames yet",
        "Wait for J1939 traffic on the bus.")

    pgn_tab = QWidget()
    pv = QVBoxLayout(pgn_tab)
    pv.setContentsMargins(0, 0, 0, 0)
    pgn_tree = QTreeWidget()
    pgn_tree.setHeaderLabels(["PGN", "Name", "Count", "Last SA", "Last time"])
    _ui.style_tree(pgn_tree, header_hidden=False)
    pgn_tree.setRootIsDecorated(False)
    pgn_tree.setSortingEnabled(True)
    pgn_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    pv.addWidget(empty)
    pv.addWidget(pgn_tree, 1)
    pgn_tree.hide()

    dec_tab = QWidget()
    dv = QVBoxLayout(dec_tab)
    dv.setContentsMargins(0, 0, 0, 0)
    dec_tree = QTreeWidget()
    dec_tree.setHeaderLabels(["Timestamp (s)", "PGN", "Decoded"])
    _ui.style_tree(dec_tree, header_hidden=False)
    dec_tree.setRootIsDecorated(False)
    dec_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    dv.addWidget(dec_tree, 1)

    tp_tab = QWidget()
    tv = QVBoxLayout(tp_tab)
    tv.setContentsMargins(0, 0, 0, 0)
    tp_tree = QTreeWidget()
    tp_tree.setHeaderLabels(
        ["Timestamp (s)", "Kind", "SA", "DA", "PGN", "Bytes", "Payload (hex)"])
    _ui.style_tree(tp_tree, header_hidden=False)
    tp_tree.setRootIsDecorated(False)
    tp_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tp_empty = _ui.empty_state(
        "No reassembled TP PDUs yet",
        "BAM / RTS-CTS PDUs appear when transport traffic arrives.")
    tv.addWidget(tp_empty)
    tv.addWidget(tp_tree, 1)
    tp_tree.hide()

    addr_tab = QWidget()
    av = QVBoxLayout(addr_tab)
    av.setContentsMargins(0, 0, 0, 0)
    addr_tree = QTreeWidget()
    addr_tree.setHeaderLabels(
        ["Source addr", "NAME (hex)", "Manufacturer", "Function code",
         "Function", "Claim time"])
    _ui.style_tree(addr_tree, header_hidden=False)
    addr_tree.setRootIsDecorated(False)
    addr_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    av.addWidget(addr_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    lv.setContentsMargins(0, 0, 0, 0)
    log_view = QTextEdit()
    log_view.setObjectName("SuiteCode")
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
            dbc_label.setText(
                "DBC: %s (%d msgs)"
                % (base, len(_dbc.messages) if _dbc else 0))
        else:
            dbc_label.setText("DBC: (none)")

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
        needle = (filter_edit.text() or "").strip().lower()
        focus_pgn = int(getattr(session, "focus_pgn", 0) or 0)
        focus_item = None
        for pgn, st in _pgn_stats.items():
            row_txt = "0x%04X %s" % (pgn, st["name"])
            if needle and needle not in row_txt.lower():
                continue
            it = QTreeWidgetItem([
                "0x%04X" % pgn, st["name"], str(st["count"]),
                "%02X" % st["sa"], "%.3f" % st["last_ts"]])
            it.setData(0, Qt.ItemDataRole.UserRole, int(pgn))
            pgn_tree.addTopLevelItem(it)
            if focus_pgn and pgn == focus_pgn and focus_item is None:
                focus_item = it
        if focus_item is not None:
            pgn_tree.setCurrentItem(focus_item)
        pgn_tree.setSortingEnabled(True)

        if _dm_rows and hasattr(session, "note_dm"):
            session.note_dm()
        if _tp_pdus and hasattr(session, "note_tp"):
            session.note_tp()
        if _addr_claims and hasattr(session, "note_claim"):
            session.note_claim()
        if hasattr(session, "_sync_next_hint") is False and hasattr(parent, "_sync_next_hint"):
            pass


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
        if hasattr(parent, "run_action"):
            parent.run_action("j1939.load_dbc")
            return
        path = dbc_picker.pick_dbc(parent, "Load J1939 DBC")
        if not path:
            return
        if not _load_dbc_path(path):
            QMessageBox.warning(parent, "DBC", "No messages found in file")
            return
        if hasattr(session, "note_dbc"):
            session.note_dbc(path)
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
            QMessageBox.warning(
                parent, "RQST", "Invalid PGN (use hex 0xF004 or decimal)")
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

    def on_pgn_activated(item, _col):
        try:
            data = item.data(0, Qt.ItemDataRole.UserRole)
            pgn = int(data) if data is not None else int(item.text(0), 0)
        except Exception:
            return
        if hasattr(session, "set_focus"):
            session.set_focus(pgn=pgn)

    def select_pgn(pgn: int):
        key = int(pgn) & 0x3FFFF
        tabs.setCurrentWidget(pgn_tab)
        for i in range(pgn_tree.topLevelItemCount()):
            it = pgn_tree.topLevelItem(i)
            if it is None:
                continue
            data = it.data(0, Qt.ItemDataRole.UserRole)
            try:
                cur = int(data) if data is not None else int(it.text(0), 0)
            except Exception:
                continue
            if cur == key:
                pgn_tree.setCurrentItem(it)
                pgn_tree.scrollToItem(it)
                return

    def _copy_text(text: str):
        if not text:
            return
        QGuiApplication.clipboard().setText(str(text))
        plugin_shell.set_status(parent, "Copied", 1500)

    def _pgn_menu(pos):
        item = pgn_tree.itemAt(pos)
        menu = QMenu(pgn_tree)
        if item is None:
            menu.addAction("Load DBC…", on_load_dbc)
            menu.addAction("Clear", on_clear)
        else:
            pgn_tree.setCurrentItem(item)
            try:
                data = item.data(0, Qt.ItemDataRole.UserRole)
                pgn = int(data) if data is not None else int(item.text(0), 0)
            except Exception:
                pgn = 0
            session.set_focus(pgn=pgn)
            menu.addAction(
                "Copy PGN", lambda: _copy_text("0x%04X" % pgn))
            menu.addAction(
                "Copy name", lambda: _copy_text(item.text(1)))
            menu.addSeparator()
            pgn_edit.setText("0x%04X" % pgn)
            menu.addAction("Send RQST for PGN", on_send_rqst)
            menu.addSeparator()
            menu.addAction(
                "Open Diagnostics…",
                lambda: parent.run_action("j1939.goto", page="diagnostics")
                if hasattr(parent, "run_action")
                else parent.goto_page("diagnostics"))
            menu.addAction(
                "Open Transport…",
                lambda: parent.run_action("j1939.goto", page="transport")
                if hasattr(parent, "run_action")
                else parent.goto_page("transport"))
            menu.addAction(
                "Filter this PGN",
                lambda: filter_edit.setText("0x%04X" % pgn))
        if menu.actions():
            menu.exec(pgn_tree.viewport().mapToGlobal(pos))

    def _on_focus(pgn, _sa):
        if int(pgn or 0):
            select_pgn(int(pgn))

    if hasattr(session, "on_focus"):
        session.on_focus(_on_focus)

    def refresh_dbc_label():
        _set_dbc_label()
        if hasattr(session, "note_dbc") and _dbc_path:
            session.note_dbc(_dbc_path)

    dbc_btn.clicked.connect(on_load_dbc)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)
    send_rqst_btn.clicked.connect(on_send_rqst)
    filter_edit.textChanged.connect(lambda _t: refresh())
    pgn_tree.itemClicked.connect(on_pgn_activated)
    pgn_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    pgn_tree.customContextMenuRequested.connect(_pgn_menu)

    def _dec_menu(pos):
        item = dec_tree.itemAt(pos)
        menu = QMenu(dec_tree)
        if item is not None:
            dec_tree.setCurrentItem(item)
            try:
                pgn = int(item.text(1), 0)
            except Exception:
                pgn = 0
            menu.addAction(
                "Copy decoded", lambda: _copy_text(item.text(2)))
            if pgn:
                session.set_focus(pgn=pgn)
                menu.addAction(
                    "Copy PGN", lambda: _copy_text("0x%04X" % pgn))
                menu.addAction(
                    "Select in PGN stats", lambda: select_pgn(pgn))
                menu.addSeparator()
                menu.addAction(
                    "Open Diagnostics…",
                    lambda: parent.run_action(
                        "j1939.goto", page="diagnostics")
                    if hasattr(parent, "run_action")
                    else parent.goto_page("diagnostics"))
        if menu.actions():
            menu.exec(dec_tree.viewport().mapToGlobal(pos))

    def _tp_menu(pos):
        item = tp_tree.itemAt(pos)
        menu = QMenu(tp_tree)
        if item is not None:
            tp_tree.setCurrentItem(item)
            try:
                pgn = int(item.text(4), 0)
            except Exception:
                pgn = 0
            menu.addAction(
                "Copy payload", lambda: _copy_text(item.text(6)))
            if pgn:
                session.set_focus(pgn=pgn)
                menu.addAction(
                    "Copy PGN", lambda: _copy_text("0x%04X" % pgn))
                menu.addAction(
                    "Open Transport leaf…",
                    lambda: parent.run_action(
                        "j1939.goto", page="transport")
                    if hasattr(parent, "run_action")
                    else parent.goto_page("transport"))
        if menu.actions():
            menu.exec(tp_tree.viewport().mapToGlobal(pos))

    dec_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    dec_tree.customContextMenuRequested.connect(_dec_menu)
    tp_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    tp_tree.customContextMenuRequested.connect(_tp_menu)
    plugin_shell.bind_shortcut(parent, "Ctrl+E", on_export)

    saved = state_store.load_state(SUITE_ID, "settings.json") or {}
    if saved.get("dbc_path"):
        if _load_dbc_path(saved["dbc_path"]):
            _set_dbc_label()
            if hasattr(session, "note_dbc"):
                session.note_dbc(saved["dbc_path"])
            plugin_shell.set_status(
                parent, "Restored DBC %s" % os.path.basename(_dbc_path), 3000)

    root.select_pgn = select_pgn  # type: ignore[attr-defined]
    root.refresh_dbc_label = refresh_dbc_label  # type: ignore[attr-defined]
    _ui.polish_work_surface(root)
    return root
