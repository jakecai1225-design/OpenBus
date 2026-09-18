# -*- coding: utf-8 -*-
"""canopen-scanner — CANopen node scan (CiA 301).

SDO expedited Identity (0x1018) probe for nodes 1–127, optional deep read
(product/revision/serial), heartbeat discovery on 0x701–0x77F, CSV export.
Frames are sent only when the user clicks Scan.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QTextEdit, QHeaderView, QCheckBox,
    QProgressBar, QMessageBox,
)

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "canopen-scanner"

NMT_STATES = {
    0x04: "Stopped", 0x05: "Operational", 0x7F: "Pre-operational",
    0x00: "Boot-up",
}

VENDOR_NAMES = {
    0x000000BE: "Beckhoff", 0x00000127: "Keyence", 0x0000009A: "Wago",
    0x000000AD: "Codesys", 0x000000C8: "Elmo", 0x000000AB: "Maxon",
    0x0000002A: "Siemens", 0x00000055: "Lenze", 0x00000019: "Schneider",
    0x00000022: "Delta", 0x000000EA: "Inovance",
}

IDC_READ = [0x40, 0x18, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00]
IDC_READ_PC = [0x40, 0x18, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00]
IDC_READ_REV = [0x40, 0x18, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00]
IDC_READ_SN = [0x40, 0x18, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00]

_scan_state = {"running": False, "node": 1, "pending": None, "found": 0}
_nodes = {}
_events = []


def _ev(text):
    _events.append((time.time(), text))
    if len(_events) > 600:
        del _events[:300]


def _sdo_value(data):
    if len(data) >= 8:
        return data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
    return None


def _on_frame(frame):
    if not _scan_state.get("alive", True):
        return
    fid = frame.id
    data = frame.data
    if not data:
        return
    if 0x701 <= fid <= 0x77F:
        node = fid - 0x700
        st = _nodes.setdefault(node, {})
        state = NMT_STATES.get(data[0], "0x%02X" % data[0])
        if st.get("hb_state") != state:
            _ev("Node %d heartbeat → %s" % (node, state))
        st["hb_state"] = state
        st["hb_ts"] = time.time()
        return
    if 0x581 <= fid <= 0x5FF and _scan_state["running"]:
        node = fid - 0x580
        cmd = data[0]
        if cmd == 0x42 or (cmd & 0xE0) == 0x40:
            idx = data[1] | (data[2] << 8)
            sub = data[3]
            val = _sdo_value(data)
            st = _nodes.setdefault(node, {})
            st["sdo_ok"] = True
            if idx == 0x1018:
                if sub == 1:
                    st["vendor"] = val
                    _ev("Node %d online (SDO, vendor 0x%08X)" % (node, val or 0))
                elif sub == 2:
                    st["product"] = val
                elif sub == 3:
                    st["rev"] = val
                elif sub == 4:
                    st["sn"] = val
        elif cmd == 0x80:
            node_st = _nodes.setdefault(node, {})
            node_st["sdo_abort"] = True
            code = _sdo_value(data) or 0
            _ev("Node %d SDO abort 0x%08X" % (node, code))


def activate(context):
    global _scan_state
    _scan_state = {
        "running": False, "node": 1, "pending": None, "found": 0, "alive": True,
    }
    _nodes.clear()
    del _events[:]

    saved = state_store.load_state(PLUGIN_ID, "settings.json", default={}) or {}
    deep_default = bool(saved.get("deep_probe", True))

    win = sin.ui.create_window("CANopen Node Scanner (CiA 301)")
    win.resize(940, 600)
    plugin_shell.attach_status_bar(win, "Idle — scan only on button click")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    deep_chk = QCheckBox("Deep probe (product / revision / serial)")
    deep_chk.setChecked(deep_default)
    top.addWidget(deep_chk)
    top.addStretch(1)
    scan_btn = QPushButton("Start scan (1–127)")
    stop_btn = QPushButton("Stop")
    export_btn = QPushButton("Export CSV")
    clear_btn = QPushButton("Clear")
    top.addWidget(scan_btn)
    top.addWidget(stop_btn)
    top.addWidget(export_btn)
    top.addWidget(clear_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Sends SDO expedited read of 0x1018:01 (vendor ID) to nodes 1–127. "
        "Response = online. Also listens passively for heartbeats 0x701–0x77F. "
        "No frames until you click Start scan."))

    progress = QProgressBar()
    progress.setRange(0, 127)
    layout.addWidget(progress)

    empty = plugin_shell.empty_state_label(
        "No nodes yet — start a scan or wait for heartbeats.")
    tree = QTreeWidget()
    tree.setHeaderLabels([
        "Node", "Vendor ID", "Vendor", "Product", "Revision", "Serial",
        "Heartbeat", "Discovery",
    ])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(empty)
    layout.addWidget(tree, 1)
    tree.hide()

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    layout.addWidget(log_view, 1)

    context.on_frame(_on_frame)

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "deep_probe": deep_chk.isChecked(),
        }, "settings.json")

    def refresh():
        if _nodes:
            empty.hide()
            tree.show()
        else:
            tree.hide()
            empty.show()

        tree.clear()
        for node in sorted(_nodes):
            st = _nodes[node]
            vendor = st.get("vendor")
            vendor_s = ("0x%08X" % vendor) if vendor is not None else "-"
            vendor_name = VENDOR_NAMES.get(vendor, "") if vendor is not None else ""
            product = st.get("product")
            product_s = ("0x%08X" % product) if product is not None else "-"
            rev = st.get("rev")
            rev_s = ("0x%08X" % rev) if rev is not None else "-"
            sn = st.get("sn")
            sn_s = ("0x%08X" % sn) if sn is not None else "-"
            way = "SDO probe" if st.get("sdo_ok") or st.get("sdo_abort") else "Heartbeat"
            item = QTreeWidgetItem([
                str(node), vendor_s, vendor_name, product_s,
                rev_s, sn_s, st.get("hb_state", "-"), way,
            ])
            if st.get("sdo_ok"):
                item.setBackground(0, QColor("#2e7d32"))
                item.setForeground(0, QColor("white"))
            tree.addTopLevelItem(item)
        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), t)
                for ts, t in _events[-150:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

        n_sdo = sum(1 for st in _nodes.values() if st.get("sdo_ok") or st.get("sdo_abort"))
        n_hb = sum(1 for st in _nodes.values() if st.get("hb_state"))
        plugin_shell.set_status(
            win, "Nodes %d · SDO %d · heartbeat %d%s"
            % (len(_nodes), n_sdo, n_hb,
               " · scanning…" if _scan_state["running"] else ""))

    def scan_step():
        if not _scan_state["running"]:
            return
        node = _scan_state["node"]
        if node > 127:
            finish_scan()
            return
        progress.setValue(node)
        _scan_state["pending"] = node
        _scan_state["pending_ts"] = time.time()
        sin.frames.send(0x600 + node, bytes(IDC_READ))
        if deep_chk.isChecked():
            sin.frames.send(0x600 + node, bytes(IDC_READ_PC))
            sin.frames.send(0x600 + node, bytes(IDC_READ_REV))
            sin.frames.send(0x600 + node, bytes(IDC_READ_SN))
        _scan_state["node"] = node + 1
        QTimer.singleShot(60, scan_step)

    def finish_scan():
        _scan_state["running"] = False
        found = sum(
            1 for st in _nodes.values()
            if st.get("sdo_ok") or st.get("sdo_abort"))
        hb = sum(1 for st in _nodes.values() if st.get("hb_state"))
        _ev("Scan done: SDO %d nodes, heartbeat %d nodes" % (found, hb))
        scan_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        plugin_shell.set_status(
            win, "Scan complete · SDO %d · HB %d" % (found, hb), 6000)
        refresh()

    def on_scan():
        _persist()
        _scan_state["running"] = True
        _scan_state["node"] = 1
        scan_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        progress.setValue(0)
        _ev("Scanning nodes 1–127")
        plugin_shell.set_status(win, "Scanning…")
        scan_step()

    def on_stop():
        _scan_state["running"] = False
        scan_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        _ev("Scan stopped by user")
        plugin_shell.set_status(win, "Scan stopped", 4000)

    def on_export():
        rows = []
        for node in sorted(_nodes):
            st = _nodes[node]
            vendor = st.get("vendor")
            rows.append([
                node,
                ("0x%08X" % vendor) if vendor is not None else "-",
                VENDOR_NAMES.get(vendor, "") if vendor is not None else "",
                ("0x%08X" % st["product"]) if st.get("product") is not None else "-",
                ("0x%08X" % st["rev"]) if st.get("rev") is not None else "-",
                ("0x%08X" % st["sn"]) if st.get("sn") is not None else "-",
                st.get("hb_state", "-"),
                "SDO" if st.get("sdo_ok") or st.get("sdo_abort") else "Heartbeat",
            ])
        path = plugin_shell.export_csv(
            win,
            ["Node", "Vendor ID", "Vendor", "Product", "Revision", "Serial",
             "Heartbeat", "Discovery"],
            rows,
            "canopen_nodes.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported: %s" % path, 5000)
            QMessageBox.information(win, "Export", "Saved:\n%s" % path)

    def on_clear():
        _nodes.clear()
        del _events[:]
        tree.clear()
        progress.setValue(0)
        refresh()
        plugin_shell.set_status(win, "Cleared", 3000)

    raise_fn = plugin_shell.bind_raise(win)
    context.register_command(
        "canopenScanner.open", raise_fn, "Protocol: CANopen Scan")

    timer = QTimer(win)
    timer.timeout.connect(refresh)
    timer.start(500)

    deep_chk.toggled.connect(lambda _=None: _persist())
    scan_btn.clicked.connect(on_scan)
    stop_btn.clicked.connect(on_stop)
    export_btn.clicked.connect(on_export)
    clear_btn.clicked.connect(on_clear)
    stop_btn.setEnabled(False)

    win.show()
    sin.output.append(
        "CANopen scanner loaded (SDO 0x1018 + heartbeat; idle until scan)")


def deactivate():
    _scan_state["running"] = False
    _scan_state["alive"] = False
    sin.output.append("CANopen scanner deactivated")
