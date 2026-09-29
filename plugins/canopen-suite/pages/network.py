# -*- coding: utf-8 -*-
"""Network — Scan | NMT via suite chrome / sidebar (no inner tab strip)."""

from __future__ import annotations

import time

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QCheckBox,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QProgressBar,
    QStackedWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

import sin
from _shared import plugin_shell
from pages import _ui, interop

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


def _ghost(text, tip, icon=""):
    return _ui.ghost_btn(text, tip, icon)


def build(parent, session, log_fn) -> QWidget:
    stack = QStackedWidget()
    parent._network_tabs = None

    # ---- Scan ----
    scan_page = QWidget()
    slay = QVBoxLayout(scan_page)
    slay.setContentsMargins(0, 0, 0, 0)
    slay.setSpacing(0)

    deep_chk = QCheckBox("Deep")
    deep_chk.setChecked(True)
    deep_chk.setToolTip("Also read product / revision / serial (0x1018:2–4)")
    scan_btn = _ui.primary_btn(
        "Scan", "Probe nodes 1–127 via expedited SDO 0x1018", "search")
    stop_btn = _ghost("Stop", "Stop the scan", "stop")
    apply_btn = _ghost("Apply", "Use the selected row as the shared Node-ID", "apply")
    export_btn = _ghost("Export", "Export the node list as CSV", "export")
    clear_btn = _ghost("Clear", "Clear discovered nodes", "clear")
    stop_btn.setEnabled(False)
    slay.addWidget(_ui.tool_strip(
        deep_chk, scan_btn, stop_btn, apply_btn, export_btn, clear_btn,
        stretch_at=1))

    progress = QProgressBar()
    progress.setRange(0, 127)
    progress.setFixedHeight(2)
    progress.setTextVisible(False)
    slay.addWidget(progress)

    tree = interop.NodeScanTree()
    tree.setHeaderLabels([
        "Node", "Vendor ID", "Vendor", "Product", "Revision", "Serial",
        "Heartbeat", "Discovery",
    ])
    _ui.style_tree(tree, header_hidden=False)
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    _ui.configure_columns(tree, stretch=2)
    slay.addWidget(tree, 1)
    stack.addWidget(scan_page)

    # ---- NMT ----
    nmt_page = QWidget()
    nlay = QVBoxLayout(nmt_page)
    nlay.setContentsMargins(0, 0, 0, 0)
    nlay.setSpacing(0)

    node_spin = _ui.suite_spin(
        session.node_id, minimum=1, maximum=127,
        tip="Target Node-ID for NMT commands", width=100)
    node_lab = _ui.field_label("Node")
    nmt_btns = []
    for text, cmd, icon in (
        ("Start", NMT_START, "play"),
        ("Stop", NMT_STOP, "stop"),
        ("Pre-op", NMT_PREOP, "debug-continue"),
        ("Reset", NMT_RESET_NODE, "sync"),
    ):
        b = _ui.ghost_btn(
            text, "Send NMT %s to the Node-ID above" % text, icon)
        b.clicked.connect(
            lambda _=False, c=cmd: session.send_nmt(c, node_spin.value()))
        nmt_btns.append(b)
    nlay.addWidget(_ui.tool_strip(
        node_lab, node_spin, *nmt_btns, stretch_at=2))

    run_scan_nmt = _ui.ghost_btn(
        "Run Scan", "Discover nodes, then return here for NMT", "search")
    nmt_list = QListWidget()
    nmt_list.setObjectName("SuiteMatrix")
    _ui.style_list(nmt_list)
    nmt_list.setToolTip(
        "Nodes from the last Scan — click to set Node-ID. "
        "Or type a Node-ID above, then Start / Stop / Pre-op.")
    nlay.addWidget(nmt_list, 1)
    nmt_scan_strip = _ui.tool_strip(run_scan_nmt)
    nlay.addWidget(nmt_scan_strip, 0)
    stack.addWidget(nmt_page)

    state = {"running": False, "node": 1, "nodes": {}}

    def _refresh_nmt_list():
        nmt_list.clear()
        nodes = state["nodes"]
        if not nodes:
            item = QListWidgetItem("No scan results yet")
            item.setFlags(Qt.ItemFlag.NoItemFlags)
            nmt_list.addItem(item)
            nmt_scan_strip.setVisible(True)
            return
        nmt_scan_strip.setVisible(False)
        for node in sorted(nodes):
            st = nodes[node]
            vendor = st.get("vendor")
            name = VENDOR_NAMES.get(vendor, "") if vendor is not None else ""
            hb = st.get("hb_state", "")
            label = "Node %d" % node
            if name:
                label += " · %s" % name
            elif vendor is not None:
                label += " · 0x%08X" % vendor
            if hb:
                label += " · %s" % hb
            item = QListWidgetItem(label)
            item.setData(Qt.ItemDataRole.UserRole, node)
            nmt_list.addItem(item)

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
        _ui.fit_columns(tree, stretch=2)
        _refresh_nmt_list()

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
        node_spin.setValue(int(node))
        if hasattr(parent, "_persist"):
            parent._persist()
        plugin_shell.set_status(parent, "Applied Node-ID %d" % int(node), 3000)
        if hasattr(parent, "run_action"):
            parent.run_action("view.od")
        elif hasattr(parent, "goto_page"):
            parent.goto_page("od")

    def on_double_click(item, _col):
        if item is not None:
            tree.setCurrentItem(item)
            on_apply()

    def on_nmt_pick():
        item = nmt_list.currentItem()
        if item is None:
            return
        node = item.data(Qt.ItemDataRole.UserRole)
        if node is None:
            return
        node_spin.setValue(int(node))
        session.set_node_id(int(node))
        plugin_shell.set_status(parent, "NMT target Node-ID %d" % int(node), 2000)

    def on_node_spin():
        session.set_node_id(node_spin.value())

    def start_scan():
        on_scan()

    def select_view(key: str):
        if key in ("network_nmt", "nmt"):
            stack.setCurrentIndex(1)
            node_spin.setValue(session.node_id)
            _refresh_nmt_list()
        else:
            stack.setCurrentIndex(0)

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
    run_scan_nmt.clicked.connect(lambda: (select_view("network_scan"), on_scan()))
    tree.itemDoubleClicked.connect(on_double_click)
    nmt_list.itemClicked.connect(lambda _i: on_nmt_pick())
    node_spin.editingFinished.connect(on_node_spin)
    if hasattr(session, "on_node_changed"):
        session.on_node_changed(
            lambda: node_spin.setValue(session.node_id))

    def _net_menu(pos):
        from PyQt6.QtWidgets import QMenu
        menu = QMenu(tree)
        item = tree.itemAt(pos)
        if item is None:
            menu.addAction("Scan", on_scan)
            interop.add_bridge_actions(menu, parent, include_blank=True)
        else:
            node = item.data(0, Qt.ItemDataRole.UserRole)
            if node is None:
                try:
                    node = int(item.text(0))
                except ValueError:
                    node = None
            if node is not None:
                menu.addAction(
                    "Use as Node-ID + open OD",
                    lambda n=int(node): interop.shell_action(
                        parent, "network.use_node", node_id=n))
                menu.addAction(
                    "NMT Start",
                    lambda n=int(node): session.send_nmt(NMT_START, n))
                menu.addAction(
                    "NMT Pre-op",
                    lambda n=int(node): session.send_nmt(NMT_PREOP, n))
                interop.add_bridge_actions(menu, parent, node_id=int(node))
            menu.addSeparator()
            menu.addAction("Scan again", on_scan)
        menu.exec(tree.viewport().mapToGlobal(pos))

    tree.customContextMenuRequested.connect(_net_menu)
    stack.select_view = select_view  # type: ignore[attr-defined]
    stack.start_scan = start_scan  # type: ignore[attr-defined]
    return stack
