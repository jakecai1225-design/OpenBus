# -*- coding: utf-8 -*-
"""iso-tp-monitor — ISO-TP (ISO 15765-2) passive session monitor.

SF / FF / CF / FC decode, multi-frame reassembly by CAN ID, sequence checks,
session timeout, PDU list + event log + CSV. Passive only — no TX.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QTextEdit, QHeaderView, QSpinBox,
    QTabWidget, QMessageBox,
)

import sin
from _shared import plugin_shell, state_store

SUITE_ID = "protocol-hub"
PAGE_KEY = "isotp"

_sessions = {}
_pdus = []
_events = []
_total_frames = 0
_fc_frames = 0
_timeout_ms = 1000.0
_running = True
_enabled = True

FS_NAMES = {0: "CTS", 1: "WAIT", 2: "OVFLW"}


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 2000:
        del _events[:1000]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _stmin_text(raw):
    if raw <= 0x7F:
        return "%d ms" % raw
    if 0xF1 <= raw <= 0xF9:
        return "%d×100 µs" % (raw - 0xF0)
    return "reserved (%02X)" % raw


def _on_frame(frame):
    global _total_frames, _fc_frames
    if not _running or not _enabled:
        return
    data = frame.data
    if not data:
        return
    _total_frames += 1
    fid = frame.id
    ts = frame.timestamp
    pci = data[0]
    kind = pci & 0xF0

    if kind == 0x00:
        length = pci & 0x0F
        if length == 0:
            return
        pdu = bytes(data[1:1 + length])
        if pdu:
            _pdus.append((ts, fid, _hex(pdu)))
            if len(_pdus) > 5000:
                del _pdus[:1000]
            _ev("PDU", "SF 0x%X done %d B: %s" % (fid, length, _hex(pdu[:24])))
    elif kind == 0x10:
        expected = ((pci & 0x0F) << 8) | (data[1] if len(data) > 1 else 0)
        got = max(0, len(data) - 2)
        _sessions[fid] = {
            "expected": expected, "got": got, "last_sn": 0,
            "last_ts": ts, "done": 0, "state": "Receiving",
            "buf": bytearray(data[2:]),
        }
        _ev("FF", "0x%X FF expect %d B (first %d)" % (fid, expected, got))
    elif kind == 0x20:
        sn = pci & 0x0F
        st = _sessions.get(fid)
        if st is None:
            return
        st["last_ts"] = ts
        chunk = bytes(data[1:])
        st["got"] += len(chunk)
        if "buf" in st:
            st["buf"].extend(chunk)
        if sn == ((st["last_sn"] + 1) & 0x0F):
            st["last_sn"] = sn
        else:
            _ev("SN", "0x%X CF SN error: expect %d got %d" % (
                fid, (st["last_sn"] + 1) & 0x0F, sn))
            st["last_sn"] = sn
        if st["got"] >= st["expected"]:
            st["done"] += 1
            st["state"] = "Complete"
            payload = bytes(st.get("buf", b""))[:st["expected"]]
            hx = _hex(payload) if payload else "<multi-frame %d B>" % st["expected"]
            _pdus.append((ts, fid, hx))
            if len(_pdus) > 5000:
                del _pdus[:1000]
            _ev("PDU", "0x%X multi-frame done %d B (#%d)" % (
                fid, st["expected"], st["done"]))
    elif kind == 0x30:
        _fc_frames += 1
        fs = pci & 0x0F
        bs = data[1] if len(data) > 1 else 0
        stmin = data[2] if len(data) > 2 else 0
        _ev("FC", "0x%X FC: FS=%s BS=%d STmin=%s" % (
            fid, FS_NAMES.get(fs, "%d" % fs), bs, _stmin_text(stmin)))


def build(parent, session, log_fn):
    global _running, _enabled, _timeout_ms, _total_frames, _fc_frames
    _sessions.clear()
    del _pdus[:]
    del _events[:]
    _total_frames = 0
    _fc_frames = 0

    saved = state_store.load_state(SUITE_ID, "settings.json", default={}) or {}
    try:
        _timeout_ms = float(saved.get("timeout_ms", 1000))
    except (TypeError, ValueError):
        _timeout_ms = 1000.0

    _running = True
    _enabled = True

    root = QWidget(parent)
    layout = QVBoxLayout(root)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    sess_tab = QWidget()
    sv = QVBoxLayout(sess_tab)

    top = QHBoxLayout()
    summary = QLabel("Waiting…")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    top.addWidget(QLabel("Session timeout (ms)"))
    timeout_spin = QSpinBox()
    timeout_spin.setRange(200, 10000)
    timeout_spin.setSingleStep(100)
    timeout_spin.setValue(int(_timeout_ms))
    top.addWidget(timeout_spin)
    pause_btn = QPushButton("Pause")
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    top.addWidget(pause_btn)
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    sv.addLayout(top)

    sv.addWidget(plugin_shell.help_label(
        "Passive: SF/FF/CF/FC decode, multi-frame reassembly by CAN ID, "
        "SN checks and session timeout. No frames are sent."))

    empty = plugin_shell.empty_state_label(
        "No ISO-TP sessions yet — wait for First Frame or Single Frame.")
    sess_tree = QTreeWidget()
    sess_tree.setHeaderLabels([
        "CAN ID", "State", "Expected B", "Received B", "Completions",
        "Last SN", "Last activity (s)",
    ])
    sess_tree.setRootIsDecorated(False)
    sess_tree.setAlternatingRowColors(True)
    sess_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(empty)
    sv.addWidget(sess_tree, 2)
    sess_tree.hide()

    pdu_tab = QWidget()
    pv = QVBoxLayout(pdu_tab)
    pdu_tree = QTreeWidget()
    pdu_tree.setHeaderLabels(["Timestamp (s)", "CAN ID", "PDU hex"])
    pdu_tree.setRootIsDecorated(False)
    pdu_tree.setAlternatingRowColors(True)
    pdu_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    pv.addWidget(pdu_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(sess_tab, "Sessions")
    tabs.addTab(pdu_tab, "PDU list")
    tabs.addTab(log_tab, "Event log")

    session.on_bus_frame(_on_frame)

    def _persist():
        state_store.save_state(SUITE_ID, {
            "timeout_ms": int(timeout_spin.value()),
        }, "settings.json")

    def refresh():
        now = time.time()
        summary.setText(
            "Frames %d    Sessions %d    PDUs %d    FC %d"
            % (_total_frames, len(_sessions), len(_pdus), _fc_frames))

        for fid, st in _sessions.items():
            if st["state"] == "Receiving" and now - st["last_ts"] > _timeout_ms / 1000.0:
                st["state"] = "Timeout"
                _ev("TO", "0x%X session timeout (%d/%d B)" % (
                    fid, st["got"], st["expected"]))

        if _sessions:
            empty.hide()
            sess_tree.show()
        else:
            sess_tree.hide()
            empty.show()

        sess_tree.setSortingEnabled(False)
        sess_tree.clear()
        for fid, st in _sessions.items():
            sess_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % fid, st["state"], str(st["expected"]), str(st["got"]),
                str(st["done"]), str(st["last_sn"]),
                "%.3f" % (now - st["last_ts"])]))
        sess_tree.setSortingEnabled(True)

        pdu_tree.setSortingEnabled(False)
        pdu_tree.clear()
        for ts, fid, hx in _pdus[-300:]:
            pdu_tree.addTopLevelItem(QTreeWidgetItem([
                "%.6f" % ts, "0x%X" % fid, hx]))
        pdu_tree.scrollToBottom()
        pdu_tree.setSortingEnabled(True)

        if _events:
            lines = [
                "[%s] %s %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), kind, text)
                for ts, kind, text in _events[-200:]
            ]
            log_view.setPlainText("\n".join(lines))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

        plugin_shell.set_status(
            parent, "%s · %d frames · %d PDUs · timeout %d ms"
            % ("Paused" if not _enabled else "Live",
               _total_frames, len(_pdus), int(_timeout_ms)))

    timer = QTimer(root)
    timer.timeout.connect(refresh)
    timer.start(500)

    def on_pause():
        global _enabled
        _enabled = not _enabled
        pause_btn.setText("Resume" if not _enabled else "Pause")
        plugin_shell.set_status(
            parent, "Paused" if not _enabled else "Live", 3000)

    def on_clear():
        global _total_frames, _fc_frames
        _sessions.clear()
        del _pdus[:]
        del _events[:]
        _total_frames = 0
        _fc_frames = 0
        sess_tree.clear()
        pdu_tree.clear()
        log_view.clear()
        refresh()
        plugin_shell.set_status(parent, "Cleared", 3000)

    def on_export():
        rows = []
        for ts, fid, hx in _pdus:
            rows.append(["%.6f" % ts, "0x%X" % fid, hx, "", ""])
        for ts, kind, text in _events:
            rows.append([
                time.strftime("%H:%M:%S", time.localtime(ts)), kind, text, "", "",
            ])
        path = plugin_shell.export_csv(
            parent,
            ["Timestamp / time", "CAN ID / kind", "PDU / text", "", ""],
            rows,
            "isotp_monitor.csv",
        )
        if path:
            plugin_shell.set_status(parent, "Exported: %s" % path, 5000)
            QMessageBox.information(parent, "Export", "Saved:\n%s" % path)

    def on_timeout_changed(v):
        global _timeout_ms
        _timeout_ms = float(v)
        _persist()


    pause_btn.clicked.connect(on_pause)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)
    timeout_spin.valueChanged.connect(on_timeout_changed)

    return root
