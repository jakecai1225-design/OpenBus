# -*- coding: utf-8 -*-
"""Network — Scan | NMT. Tabs live in the suite chrome row."""

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
    QStackedWidget,
    QTabBar,
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


def _ghost(text, tip):
    btn = QPushButton(text)
    btn.setObjectName("GhostButton")
    btn.setFixedHeight(28)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    return btn


def build(parent, session, log_fn) -> QWidget:
    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    bar.addTab("Scan")
    bar.addTab("NMT")
    parent._network_tabs = bar

    stack = QStackedWidget()
    bar.currentChanged.connect(stack.setCurrentIndex)

    # ---- Scan ----
    scan_page = QWidget()
    slay = QVBoxLayout(scan_page)
    slay.setContentsMargins(8, 6, 8, 6)
    slay.setSpacing(6)

    tools = QHBoxLayout()
    deep_chk = QCheckBox("Deep")
    deep_chk.setChecked(True)
    deep_chk.setToolTip("Also read product / revision / serial (0x1018:2–4)")
    scan_btn = QPushButton("Scan")
    scan_btn.setObjectName("PrimaryButton")
    scan_btn.setFixedHeight(28)
    scan_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    scan_btn.setToolTip("Probe nodes 1–127 via expedited SDO 0x1018")
    stop_btn = _ghost("Stop", "Stop the scan")
    apply_btn = _ghost("Apply", "Use the selected row as the shared Node-ID")
    export_btn = _ghost("Export", "Export the node list as CSV")
    clear_btn = _ghost("Clear", "Clear discovered nodes")
    stop_btn.setEnabled(False)
    tools.addWidget(deep_chk)
    tools.addStretch(1)
    tools.addWidget(scan_btn)
    tools.addWidget(stop_btn)
    tools.addWidget(apply_btn)
    tools.addWidget(export_btn)
    tools.addWidget(clear_btn)
    slay.addLayout(tools)

    progress = QProgressBar()
    progress.setRange(0, 127)
    progress.setFixedHeight(4)
    progress.setTextVisible(False)
    slay.addWidget(progress)

    tree = QTreeWidget()
    tree.setHeaderLabels([
        "Node", "Vendor ID", "Vendor", "Product", "Revision", "Serial",
        "Heartbeat", "Discovery",
    ])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tree.header().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
    slay.addWidget(tree, 1)
    stack.addWidget(scan_page)

    # ---- NMT ----
    nmt_page = QWidget()
    nlay = QVBoxLayout(nmt_page)
    nlay.setContentsMargins(8, 6, 8, 6)
    nlay.setSpacing(8)
    nrow = QHBoxLayout()
    for text, cmd in (
        ("Start", NMT_START),
        ("Stop", NMT_STOP),
        ("Pre-op", NMT_PREOP),
        ("Reset", NMT_RESET_NODE),
    ):
        b = QPushButton("NMT %s" % text)
        b.setObjectName("GhostButton")
        b.setFixedHeight(28)
        b.setCursor(Qt.CursorShape.PointingHandCursor)
        b.setToolTip("Send NMT %s to the shared Node-ID" % text)
        b.clicked.connect(lambda _=False, c=cmd: session.send_nmt(c))
        nrow.addWidget(b)
    nrow.addStretch(1)
    nlay.addLayout(nrow)
    nlay.addStretch(1)
    stack.addWidget(nmt_page)

    state = {"running": False, "node": 1, "nodes": {}}

    def refresh():
        nodes = state["nodes"]
        tree.clear()
        for node in sorted(nodes):
            st = nodes[node]
            vendor = st.get("vendor")
            item = QTreeWidgetItem([
                str(node),
                ("0x%08X" % vendor) if vendor is not None else "-",
                VENDOR_NAMES.get(vendor, "") if vendor is not None else "",
                ("0x%08X" % st["product"]) if st.get("product") is not None else "-",
                ("0x%08X" % st["rev"]) if st.get("rev") is not None else "-",
                ("0x%08X" % st["sn"]) if st.get("sn") is not None else "-",
                st.get("hb_state", "-"),
                "SDO probe" if st.get("sdo_ok") or st.get("sdo_abort") else "Heartbeat",
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, node)
            if st.get("sdo_ok"):
                item.setBackground(0, QColor("#2e7d32"))
                item.setForeground(0, QColor("white"))
            tree.addTopLevelItem(item)

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

    ui_timer = QTimer(stack)
    ui_timer.timeout.connect(refresh)
    ui_timer.start(500)

    scan_btn.clicked.connect(on_scan)
    stop_btn.clicked.connect(on_stop)
    apply_btn.clicked.connect(on_apply)
    export_btn.clicked.connect(on_export)
    clear_btn.clicked.connect(on_clear)
    return stack
