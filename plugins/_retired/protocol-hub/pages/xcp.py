# -*- coding: utf-8 -*-
"""xcp-monitor — XCP on CAN calibration/measurement monitor.

CTO command/response PID classify (CMD/RES/ERR/EV/SERV), common command
names, DAQ/STIM DTO ODT counting, session stats + event stream + CSV.
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
PAGE_KEY = "xcp"

PID_NAMES = {
    0xFF: "RES Response", 0xFE: "ERR Error", 0xFD: "EV Event",
    0xFC: "SERV Service request",
    0xFB: "DAQRDY? (0xFB)", 0xFA: "DAQRDY? (0xFA)",
}

CMD_NAMES = {
    0x00: "CONNECT",
    0xFD: "GET_STATUS",
    0xFC: "GET_COMM_MODE_INFO",
    0xFB: "GET_ID",
    0xFA: "SET_REQUEST",
    0xF9: "GET_SEED",
    0xF7: "UNLOCK",
    0xF6: "SET_MTA",
    0xF5: "UPLOAD",
    0xF4: "SHORT_UPLOAD",
    0xF3: "BUILD_CHECKSUM",
    0xF2: "TRANSPORT_LAYER_CMD",
    0xF1: "DOWNLOAD_NEXT / SET_CAL_PAGE group",
    0xF0: "DOWNLOAD",
    0xEE: "DOWNLOAD_MAX",
    0xE7: "DIAG_SERVICE",
    0xE5: "FREE_DAQ",
    0xE4: "ALLOC_DAQ",
    0xE3: "SET_DAQ_PTR",
    0xE2: "WRITE_DAQ",
    0xE0: "START_STOP",
    0xDF: "DIAG_SERVICE? (0xDF)",
    0xDE: "SELECT_CAL_PAGE group",
    0xDD: "PGM group",
}

DAQ_RANGE = (0xD6, 0xE5)

ERR_CODES = {
    0x00: "Command understood but not executed",
    0x10: "Command temporarily not supported",
    0x11: "Command not implemented",
    0x12: "Illegal command / invalid parameter",
    0x13: "Out of range (length)",
    0x14: "Page mode protected / access denied",
    0x20: "Resource locked by function",
    0x21: "Data block too large / out of bounds",
    0x22: "Checksum error",
    0x23: "Out of range / resource unavailable",
    0x30: "Generic error",
    0x31: "Processor halted (internal)",
    0x32: "Generic error (undefined)",
}

_cto_count = 0
_dto_count = 0
_err_count = 0
_cmd_stats = {}
_events = []
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 2000:
        del _events[:1000]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _on_frame(frame):
    global _cto_count, _dto_count, _err_count
    if not _running or not frame.data:
        return
    data = frame.data
    first = data[0]

    if first >= 0xFC:
        _cto_count += 1
        name = PID_NAMES.get(first, "PID %02X" % first)
        if first == 0xFF:
            cmd = data[1] if len(data) > 1 else 0
            cmd_name = CMD_NAMES.get(cmd, "CMD %02X" % cmd)
            _ev("RES", "%s ← %s: %s" % (name, cmd_name, _hex(data[:8])))
            _cmd_stats[("RES", cmd_name)] = _cmd_stats.get(("RES", cmd_name), 0) + 1
        elif first == 0xFE:
            _err_count += 1
            code = data[1] if len(data) > 1 else 0
            desc = ERR_CODES.get(code, "unknown error code")
            _ev("ERR", "Error code=%02X (%s): %s" % (code, desc, _hex(data[:8])))
            key = ("ERR", "code=%02X" % code)
            _cmd_stats[key] = _cmd_stats.get(key, 0) + 1
        elif first == 0xFD:
            code = data[1] if len(data) > 1 else 0
            _ev("EV", "Event code=%02X: %s" % (code, _hex(data[:8])))
        elif first == 0xFC:
            _ev("SERV", "Service request: %s" % _hex(data[:8]))
        else:
            _ev("CTO", "%s: %s" % (name, _hex(data[:8])))
        return

    if first in CMD_NAMES:
        _cto_count += 1
        cmd_name = CMD_NAMES[first]
        _ev("CMD", "%s: %s" % (cmd_name, _hex(data[:8])))
        _cmd_stats[("CMD", cmd_name)] = _cmd_stats.get(("CMD", cmd_name), 0) + 1
        return

    if DAQ_RANGE[0] <= first <= DAQ_RANGE[1]:
        _cto_count += 1
        cmd_name = CMD_NAMES.get(first, "DAQ group %02X" % first)
        _ev("CMD", "%s: %s" % (cmd_name, _hex(data[:8])))
        _cmd_stats[("CMD", cmd_name)] = _cmd_stats.get(("CMD", cmd_name), 0) + 1
        return

    _dto_count += 1
    odt = first
    if odt == 0:
        _ev("DTO", "ODT0: %s" % _hex(data[:8]))


def build(parent, session, log_fn):
    global _running, _cto_count, _dto_count, _err_count
    _cmd_stats.clear()
    del _events[:]
    _cto_count = _dto_count = _err_count = 0

    saved = state_store.load_state(SUITE_ID, "settings.json", default={}) or {}
    last_tab = int(saved.get("last_tab", 0) or 0)

    _running = True
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    top = QHBoxLayout()
    summary = QLabel("Waiting for XCP (CTO/DTO structure)…")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("Clear")
    export_btn = QPushButton("Export CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Passive XCP: byte0 PID FF/FE/FD/FC = CTO RES/ERR/EV/SERV; "
        "known command codes = CMD; other low PIDs counted as DAQ/STIM DTO. "
        "No TX."))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    stat_tab = QWidget()
    sv = QVBoxLayout(stat_tab)
    empty = plugin_shell.empty_state_label(
        "No CTO commands yet — wait for CONNECT / DAQ traffic.")
    stat_tree = QTreeWidget()
    stat_tree.setHeaderLabels(["Direction", "Command / type", "Count"])
    stat_tree.setRootIsDecorated(False)
    stat_tree.setAlternatingRowColors(True)
    stat_tree.setSortingEnabled(True)
    stat_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(empty)
    sv.addWidget(stat_tree, 1)
    stat_tree.hide()

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(stat_tab, "Command stats")
    tabs.addTab(log_tab, "Event stream")
    if 0 <= last_tab < tabs.count():
        tabs.setCurrentIndex(last_tab)

    session.on_bus_frame(_on_frame)

    def _persist():
        state_store.save_state(SUITE_ID, {
            "last_tab": tabs.currentIndex(),
        }, "settings.json")

    def refresh():
        summary.setText(
            "CTO %d    DTO %d    ERR %d    Command types %d"
            % (_cto_count, _dto_count, _err_count, len(_cmd_stats)))

        if _cmd_stats:
            empty.hide()
            stat_tree.show()
        else:
            stat_tree.hide()
            empty.show()

        stat_tree.setSortingEnabled(False)
        stat_tree.clear()
        for (kind, name), count in _cmd_stats.items():
            stat_tree.addTopLevelItem(QTreeWidgetItem([kind, name, str(count)]))
        stat_tree.setSortingEnabled(True)

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (
                    time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-200:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

        plugin_shell.set_status(
            parent, "Live · CTO %d · DTO %d · ERR %d"
            % (_cto_count, _dto_count, _err_count))

    timer = QTimer(root)
    timer.timeout.connect(refresh)
    timer.start(600)

    def on_clear():
        global _cto_count, _dto_count, _err_count
        _cmd_stats.clear()
        del _events[:]
        _cto_count = _dto_count = _err_count = 0
        refresh()
        plugin_shell.set_status(parent, "Cleared", 3000)

    def on_export():
        rows = []
        for (kind, name), count in _cmd_stats.items():
            rows.append([kind, name, count])
        for ts, k, t in _events:
            rows.append([
                time.strftime("%H:%M:%S", time.localtime(ts)), k, t,
            ])
        path = plugin_shell.export_csv(
            parent,
            ["Direction / time", "Command / kind", "Count / text"],
            rows,
            "xcp_monitor.csv",
        )
        if path:
            _persist()
            plugin_shell.set_status(parent, "Exported: %s" % path, 5000)
            QMessageBox.information(parent, "Export", "Saved:\n%s" % path)


    tabs.currentChanged.connect(lambda _=None: _persist())
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    return root
