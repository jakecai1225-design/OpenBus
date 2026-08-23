# -*- coding: utf-8 -*-
"""canopen-scanner 插件 — CANopen 节点扫描（CiA 301）
功能：
- 节点 1-127 扫描：SDO expedited 读 0x1018:01（厂商码）探测在线节点
- 可选深度探测：0x1018:02 产品码、0x1018:03 修订号、0x1018:04 序列号
- 心跳发现：被动监听 0x701-0x77F 心跳报文（状态字节 NMT 状态解码）
- 节点清单表（节点号/厂商码/产品码/心跳状态）、CSV 导出
- 仅用户点击「开始扫描」才发帧（安全基线）
依赖: pip install PyQt6
"""

import time

from PyQt6.QtCore import QTimer

import sin

NMT_STATES = {
    0x04: "Stopped", 0x05: "Operational", 0x7F: "Pre-operational",
    0x00: "Boot-up（初始化）",
}

VENDOR_NAMES = {
    0x000000BE: "Beckhoff", 0x00000127: "Keyence", 0x0000009A: "Wago",
    0x000000AD: "Codesys", 0x000000C8: "Elmo", 0x000000AB: "Maxon",
    0x0000002A: "Siemens", 0x00000055: "Lenze", 0x00000019: "施耐德",
    0x00000022: "台达", 0x000000EA: "汇川",
}

IDC_READ = [0x40, 0x18, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00]   # 读 0x1018:01 厂商码
IDC_READ_PC = [0x40, 0x18, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00]  # 读 0x1018:02 产品码
IDC_READ_REV = [0x40, 0x18, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00]
IDC_READ_SN = [0x40, 0x18, 0x10, 0x04, 0x00, 0x00, 0x00, 0x00]

_scan_state = {"running": False, "node": 1, "pending": None, "found": 0}
_nodes = {}       # node -> dict(vendor, product, rev, sn, hb_state, hb_ts, sdo_ok)
_events = []


def _ev(text):
    _events.append((time.time(), text))
    if len(_events) > 600:
        del _events[:300]


def _sdo_value(data):
    """SDO 响应 payload → int（小端）"""
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
    # 心跳: 0x700 + node
    if 0x701 <= fid <= 0x77F:
        node = fid - 0x700
        st = _nodes.setdefault(node, {})
        state = NMT_STATES.get(data[0], "0x%02X" % data[0])
        if st.get("hb_state") != state:
            _ev("节点 %d 心跳状态 → %s" % (node, state))
        st["hb_state"] = state
        st["hb_ts"] = time.time()
        return
    # SDO 响应: 0x580 + node
    if 0x581 <= fid <= 0x5FF and _scan_state["running"]:
        node = fid - 0x580
        cmd = data[0]
        if cmd == 0x42 or (cmd & 0xE0) == 0x40:   # expedited 读响应
            idx = data[1] | (data[2] << 8)
            sub = data[3]
            val = _sdo_value(data)
            st = _nodes.setdefault(node, {})
            st["sdo_ok"] = True
            if idx == 0x1018:
                if sub == 1:
                    st["vendor"] = val
                    _ev("节点 %d 在线（SDO 响应，厂商码 0x%08X）" % (node, val or 0))
                elif sub == 2:
                    st["product"] = val
                elif sub == 3:
                    st["rev"] = val
                elif sub == 4:
                    st["sn"] = val
        elif cmd == 0x80:   # SDO abort
            node_st = _nodes.setdefault(node, {})
            node_st["sdo_abort"] = True
            code = _sdo_value(data) or 0
            _ev("节点 %d SDO abort 0x%08X" % (node, code))


def activate(context):
    global _scan_state
    _scan_state = {"running": False, "node": 1, "pending": None, "found": 0, "alive": True}
    _nodes.clear()
    del _events[:]

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QCheckBox, QProgressBar
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("CANopen 扫描插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CANopen 节点扫描 (CiA 301)")
    win.resize(940, 600)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    deep_chk = QCheckBox("深度探测（产品码/修订号/序列号）")
    deep_chk.setChecked(True)
    top.addWidget(deep_chk)
    top.addStretch(1)
    scan_btn = QPushButton("开始扫描 (1-127)")
    stop_btn = QPushButton("停止")
    export_btn = QPushButton("导出 CSV")
    clear_btn = QPushButton("清零")
    top.addWidget(scan_btn)
    top.addWidget(stop_btn)
    top.addWidget(export_btn)
    top.addWidget(clear_btn)
    layout.addLayout(top)

    progress = QProgressBar()
    progress.setRange(0, 127)
    layout.addWidget(progress)

    tree = QTreeWidget()
    tree.setHeaderLabels(["节点", "厂商码", "厂商", "产品码", "修订号", "序列号",
                          "心跳状态", "发现方式"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    header = tree.header()
    header.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    layout.addWidget(log_view, 1)

    hint = QLabel("扫描方式：对节点 1-127 发 SDO expedited 读 0x1018:01（厂商码），"
                  "有响应即在线；同时被动监听心跳 0x701-0x77F")
    hint.setStyleSheet("color:#888;font-size:11px;")
    layout.addWidget(hint)

    context.on_frame(_on_frame)

    def refresh():
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
            way = "SDO 探测" if st.get("sdo_ok") or st.get("sdo_abort") else "心跳发现"
            item = QTreeWidgetItem([str(node), vendor_s, vendor_name, product_s,
                                    rev_s, sn_s, st.get("hb_state", "-"), way])
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
        # 发 SDO 读厂商码
        sin.frames.send(0x600 + node, bytes(IDC_READ))
        # 深度探测：产品码/修订号/序列号（同轮顺发，靠响应回调归类）
        if deep_chk.isChecked():
            sin.frames.send(0x600 + node, bytes(IDC_READ_PC))
            sin.frames.send(0x600 + node, bytes(IDC_READ_REV))
            sin.frames.send(0x600 + node, bytes(IDC_READ_SN))
        _scan_state["node"] = node + 1
        # 节点间隔（给慢节点响应时间）
        QTimer.singleShot(60, scan_step)

    def finish_scan():
        _scan_state["running"] = False
        found = sum(1 for st in _nodes.values()
                    if st.get("sdo_ok") or st.get("sdo_abort"))
        hb = sum(1 for st in _nodes.values() if st.get("hb_state"))
        _ev("扫描完成: SDO 响应 %d 节点, 心跳发现 %d 节点" % (found, hb))
        scan_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        refresh()

    def on_scan():
        _scan_state["running"] = True
        _scan_state["node"] = 1
        scan_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        progress.setValue(0)
        _ev("开始扫描节点 1-127")
        scan_step()

    def on_stop():
        _scan_state["running"] = False
        scan_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        _ev("用户停止扫描")

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出节点清单 CSV",
                                              "canopen_nodes.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("节点,厂商码,厂商,产品码,修订号,序列号,心跳状态,发现方式\n")
                for node in sorted(_nodes):
                    st = _nodes[node]
                    vendor = st.get("vendor")
                    f.write("%d,%s,%s,%s,%s,%s,%s,%s\n" % (
                        node,
                        ("0x%08X" % vendor) if vendor is not None else "-",
                        VENDOR_NAMES.get(vendor, "") if vendor is not None else "",
                        ("0x%08X" % st["product"]) if st.get("product") is not None else "-",
                        ("0x%08X" % st["rev"]) if st.get("rev") is not None else "-",
                        ("0x%08X" % st["sn"]) if st.get("sn") is not None else "-",
                        st.get("hb_state", "-"),
                        "SDO" if st.get("sdo_ok") or st.get("sdo_abort") else "心跳"))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_clear():
        _nodes.clear()
        del _events[:]
        tree.clear()
        progress.setValue(0)

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("canopenScanner.open", on_open_cmd, "协议: CANopen 扫描")

    timer = QTimer()
    timer.timeout.connect(refresh)
    timer.start(500)

    scan_btn.clicked.connect(on_scan)
    stop_btn.clicked.connect(on_stop)
    export_btn.clicked.connect(on_export)
    clear_btn.clicked.connect(on_clear)
    stop_btn.setEnabled(False)

    win.show()
    sin.output.append("CANopen 扫描插件已加载（SDO 0x1018 探测 + 心跳监听，激活期间零发送）")


def deactivate():
    _scan_state["running"] = False
    _scan_state["alive"] = False
    sin.output.append("CANopen 扫描插件已停用")
