# -*- coding: utf-8 -*-
"""Network workspace — heartbeat + SDO 0x1018 scan, NMT, Apply node."""

from __future__ import annotations

import time

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QCheckBox,
    QHBoxLayout,
    QHeaderView,
    QProgressBar,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

import sin
from _shared import plugin_shell

from session import NMT_PREOP, NMT_RESET_NODE, NMT_START, NMT_STOP

NMT_STATES = {
    0x04: "Stopped",
    0x05: "Operational",
    0x7F: "Pre-operational",
    0x00: "Boot-up",
}

VENDOR_NAMES = {
    0x000000BE: "Beckhoff",
    0x00000127: "Keyence",
    0x0000009A: "Wago",
    0x000000AD: "Codesys",
    0x000000C8: "Elmo",
    0x000000AB: "Maxon",
    0x0000002A: "Siemens",
    0x00000055: "Lenze",
    0x00000019: "Schneider",
    0x00000022: "Delta",
    0x000000EA: "Inovance",
}

IDC_READ = bytes([0x40, 0x18, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00])
IDC_READ_PC = bytes([0x40, 0x18, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00])
IDC_READ_REV = bytes([0x40, 0x18, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00])
IDC_READ_SN = bytes([0x40, 0x18, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00])


def _sdo_value(data):
    if len(data) >= 8:
        return data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
    return None


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    layout.addWidget(plugin_shell.help_label(
        "Scans nodes 1–127 with expedited SDO read of Identity 0x1018, and "
        "listens for heartbeats 0x701–0x77F. Select a row and Apply node to "
        "update the shared session strip. NMT buttons use the selected Node-ID."))

    top = QHBoxLayout()
    deep_chk = QCheckBox("Deep probe (product / revision / serial)")
    deep_chk.setChecked(True)
    top.addWidget(deep_chk)
    top.addStretch(1)
    scan_btn = QPushButton("Start scan (1–127)")
    stop_btn = QPushButton("Stop")
    apply_btn = QPushButton("Apply node")
    export_btn = QPushButton("Export CSV")
    clear_btn = QPushButton("Clear")
    for w in (scan_btn, stop_btn, apply_btn, export_btn, clear_btn):
        top.addWidget(w)
    layout.addLayout(top)
    stop_btn.setEnabled(False)

    nmt_row = QHBoxLayout()
    for text, cmd in (
        ("Start", NMT_START),
        ("Stop", NMT_STOP),
        ("Pre-op", NMT_PREOP),
        ("Reset", NMT_RESET_NODE),
    ):
        b = QPushButton("NMT %s" % text)
        b.clicked.connect(lambda _=False, c=cmd: session.send_nmt(c))
        nmt_row.addWidget(b)
    nmt_row.addStretch()
    layout.addLayout(nmt_row)

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

    state = {
        "running": False,
        "node": 1,
        "nodes": {},
    }

    def refresh():
        nodes = state["nodes"]
        if nodes:
            empty.hide()
            tree.show()
        else:
            tree.hide()
            empty.show()
        tree.clear()
        for node in sorted(nodes):
            st = nodes[node]
            vendor = st.get("vendor")
            vendor_s = ("0x%08X" % vendor) if vendor is not None else "-"
            vendor_name = VENDOR_NAMES.get(vendor, "") if vendor is not None else ""
            product = st.get("product")
            rev = st.get("rev")
            sn = st.get("sn")
            way = "SDO probe" if st.get("sdo_ok") or st.get("sdo_abort") else "Heartbeat"
            item = QTreeWidgetItem([
                str(node),
                vendor_s,
                vendor_name,
                ("0x%08X" % product) if product is not None else "-",
                ("0x%08X" % rev) if rev is not None else "-",
                ("0x%08X" % sn) if sn is not None else "-",
                st.get("hb_state", "-"),
                way,
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, node)
            if st.get("sdo_ok"):
                item.setBackground(0, QColor("#2e7d32"))
                item.setForeground(0, QColor("white"))
            tree.addTopLevelItem(item)
        n_sdo = sum(
            1 for st in nodes.values()
            if st.get("sdo_ok") or st.get("sdo_abort"))
        n_hb = sum(1 for st in nodes.values() if st.get("hb_state"))
        plugin_shell.set_status(
            parent,
            "Nodes %d · SDO %d · heartbeat %d%s" % (
                len(nodes), n_sdo, n_hb,
                " · scanning…" if state["running"] else ""),
        )

    def on_bus_frame(frame):
        fid = frame.id
        data = bytes(frame.data) if frame.data else b""
        if not data:
            return
        nodes = state["nodes"]
        if 0x701 <= fid <= 0x77F:
            node = fid - 0x700
            st = nodes.setdefault(node, {})
            hb = NMT_STATES.get(data[0], "0x%02X" % data[0])
            if st.get("hb_state") != hb:
                log_fn("RX", fid, data, "Node %d heartbeat → %s" % (node, hb))
            st["hb_state"] = hb
            st["hb_ts"] = time.time()
            return
        if not state["running"]:
            return
        if 0x581 <= fid <= 0x5FF:
            node = fid - 0x580
            cmd = data[0]
            if cmd == 0x42 or (cmd & 0xE0) == 0x40:
                idx = data[1] | (data[2] << 8)
                sub = data[3]
                val = _sdo_value(data)
                st = nodes.setdefault(node, {})
                st["sdo_ok"] = True
                if idx == 0x1018:
                    if sub == 1:
                        st["vendor"] = val
                        log_fn(
                            "RX", fid, data,
                            "Node %d online (vendor 0x%08X)" % (node, val or 0))
                    elif sub == 2:
                        st["product"] = val
                    elif sub == 3:
                        st["rev"] = val
                    elif sub == 4:
                        st["sn"] = val
            elif cmd == 0x80:
                st = nodes.setdefault(node, {})
                st["sdo_abort"] = True
                code = _sdo_value(data) or 0
                log_fn("ERR", fid, data, "Node %d SDO abort 0x%08X" % (node, code))

    session.on_bus_frame(on_bus_frame)

    def finish_scan():
        state["running"] = False
        found = sum(
            1 for st in state["nodes"].values()
            if st.get("sdo_ok") or st.get("sdo_abort"))
        hb = sum(1 for st in state["nodes"].values() if st.get("hb_state"))
        log_fn("RX", "-", b"", "Scan done: SDO %d, heartbeat %d" % (found, hb))
        scan_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        refresh()

    def scan_step():
        if not state["running"]:
            return
        node = state["node"]
        if node > 127:
            finish_scan()
            return
        progress.setValue(node)
        sin.frames.send(0x600 + node, IDC_READ)
        if deep_chk.isChecked():
            sin.frames.send(0x600 + node, IDC_READ_PC)
            sin.frames.send(0x600 + node, IDC_READ_REV)
            sin.frames.send(0x600 + node, IDC_READ_SN)
        state["node"] = node + 1
        QTimer.singleShot(60, scan_step)

    def on_scan():
        state["running"] = True
        state["node"] = 1
        scan_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        progress.setValue(0)
        log_fn("TX", "-", b"", "Scanning nodes 1–127 (SDO 0x1018)")
        scan_step()

    def on_stop():
        state["running"] = False
        scan_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        log_fn("RX", "-", b"", "Scan stopped")

    def on_apply():
        item = tree.currentItem()
        if not item:
            plugin_shell.set_status(parent, "Select a node row first", 3000)
            return
        node = item.data(0, Qt.ItemDataRole.UserRole)
        if node is None:
            try:
                node = int(item.text(0))
            except ValueError:
                return
        session.set_node_id(int(node))
        if hasattr(parent, "_sync_strip_from_session"):
            parent._sync_strip_from_session()
        if hasattr(parent, "_persist"):
            parent._persist()
        plugin_shell.set_status(parent, "Applied Node-ID %d" % int(node), 3000)

    def on_export():
        rows = []
        for node in sorted(state["nodes"]):
            st = state["nodes"][node]
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
            parent,
            ["Node", "Vendor ID", "Vendor", "Product", "Revision", "Serial",
             "Heartbeat", "Discovery"],
            rows,
            "canopen_nodes.csv",
        )
        if path:
            plugin_shell.set_status(parent, "Exported: %s" % path, 5000)

    def on_clear():
        state["nodes"].clear()
        progress.setValue(0)
        refresh()

    ui_timer = QTimer(root)
    ui_timer.timeout.connect(refresh)
    ui_timer.start(500)

    scan_btn.clicked.connect(on_scan)
    stop_btn.clicked.connect(on_stop)
    apply_btn.clicked.connect(on_apply)
    export_btn.clicked.connect(on_export)
    clear_btn.clicked.connect(on_clear)
    return root
