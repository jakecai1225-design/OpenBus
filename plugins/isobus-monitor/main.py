# -*- coding: utf-8 -*-
"""isobus-monitor 插件 — ISOBUS (ISO 11783) 农机总线监视
功能：
- 29 位 ID 拆解（优先级/PF/PS/SA/DA），PDU1/PDU2 识别
- 地址声明（PGN 0xEE00）解析：NAME 字段（功能/设备类/厂商码/身份号）→ 在线节点表
- 传输协议（TP.CM 0xEC00 / TP.DT 0xEB00）被动重组，识别 RTS/CTS/BAM/EOFLA/Abort
- 常用 PGN 命名（VT 0xE600/0xE700、TC 0xCB00/0xCC00、时间/诊断等）与消息统计
- 事件日志 + CSV 导出；纯监视不发送
依赖: pip install PyQt6
"""

import time

import sin

PGN_NAMES = {
    0xEE00: "地址声明 Address Claimed",
    0xFE08: "地址声明无法响应",
    0xEC00: "TP.CM 传输协议命令",
    0xEB00: "TP.DT 传输协议数据",
    0xE700: "ECU→VT 报文",
    0xE600: "VT→ECU 报文",
    0xCB00: "TC 服务器→客户端",
    0xCC00: "TC 客户端→服务器",
    0xFE0C: "ECU→TC 报文",
    0xFDDF: "诊断协议标识符",
    0xFDE6: "ECU→诊断报文",
    0xFDE5: "诊断→ECU 报文",
    0xFEE6: "时间/日期",
    0xFE0D: "工作集主机",
    0xC700: "VT 状态",
    0xDA00: "ISO 保留",
    0xE500: "语言/单位",
    0xFEEB: "软件标识(长)",
}

# ISO 11783-5 NAME 字段（与 J1939 不同）：字节 5=功能，字节 6 低 6 位=设备类
DEVICE_CLASSES = {
    0: "非特定", 1: "非特定农具", 2: "拖拉机", 3: "收割机", 4: "挂车",
    5: "农具挂车? ", 6: "自走式喷药机", 7: "拖拉机挂农具", 8: "非车辆单元",
    9: "传感器", 10: "导航", 11: "非特定移动单元", 12: "发动机",
    13: "动力输出单元", 14: "农具前端", 15: "非特定车辆", 16: "通用控制器",
    25: "虚拟终端(VT)", 126: "任务控制器(TC)", 128: "TC-BAS", 129: "TC-GEO",
    130: "TC-SC", 131: "TC-PRO", 132: "TC-TC",
}

FUNC_NAMES = {
    0: "非特定", 25: "虚拟终端", 126: "任务控制器", 128: "TC 基本服务器",
    129: "TC 地理服务器", 130: "TC 育种服务器", 132: "TC 任务数据服务器",
}

_sessions = {}       # (da, sa) → TP 会话
_nodes = {}          # SA → 地址声明信息
_pgn_stats = {}
_events = []
_decoded = []
_total = 0
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1200:
        del _events[:600]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _decode_name(data):
    """ISOBUS NAME 8 字节（ISO 11783-5 布局）"""
    if len(data) < 8:
        return None
    ident = data[0] | (data[1] << 8) | ((data[2] & 0x1F) << 16)
    manufacturer = ((data[2] >> 5) | (data[3] << 3)) & 0x7FF
    ecu_instance = (data[4] & 0x07)
    function_instance = (data[4] >> 3) & 0x0F
    function = data[5]
    device_class = data[6] & 0x3F
    device_class_instance = (data[6] >> 6) & 0x03
    industry_group = (data[7] & 0x07)
    aac = bool(data[7] & 0x40)
    sca = bool(data[7] & 0x80)
    return {
        "name_hex": _hex(data),
        "ident": ident,
        "manufacturer": manufacturer,
        "ecu_instance": ecu_instance,
        "function": function,
        "function_name": FUNC_NAMES.get(function, "功能 %d" % function),
        "function_instance": function_instance,
        "device_class": device_class,
        "device_class_name": DEVICE_CLASSES.get(device_class, "设备类 %d" % device_class),
        "device_class_instance": device_class_instance,
        "industry_group": industry_group,
        "self_configurable": sca,
    }


def _feed_tp(sa, da, data):
    if not data:
        return None
    key = (da, sa)
    cmd = data[0]
    if cmd == 0x20 and len(data) >= 8:          # BAM
        total = data[1] | (data[2] << 8)
        npkts = data[3]
        pgn = data[5] | (data[6] << 8) | (data[7] << 16)
        _sessions[key] = {"total": total, "npkts": npkts, "pgn": pgn, "buf": {}}
        _ev("TP", "BAM: SA=%02X PGN=0x%04X %d字节" % (sa, pgn, total))
        return None
    if cmd == 0x10 and len(data) >= 8:          # RTS
        total = data[1] | (data[2] << 8)
        npkts = data[3]
        pgn = data[5] | (data[6] << 8) | (data[7] << 16)
        _sessions[key] = {"total": total, "npkts": npkts, "pgn": pgn, "buf": {}}
        _ev("TP", "RTS: SA=%02X DA=%02X PGN=0x%04X %d字节" % (sa, da, pgn, total))
        return None
    if cmd == 0x17:
        _ev("TP", "CTS: SA=%02X DA=%02X" % (sa, da))
        return None
    if cmd == 0x13:
        _ev("TP", "EOFLA: SA=%02X DA=%02X" % (sa, da))
        return None
    if cmd == 0xFF:
        _ev("TP", "Abort: SA=%02X 原因=%d" % (sa, data[1] if len(data) > 1 else -1))
        return None
    return None


def _feed_tp_dt(sa, da, data):
    key = (da, sa)
    st = _sessions.get(key)
    if not st or not data:
        return None
    st["buf"][data[0]] = bytes(data[1:])
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
    global _total
    if not _running or not frame.extended:
        return
    data = frame.data
    if not data:
        return
    _total += 1
    cid = frame.id & 0x1FFFFFFF
    pf = (cid >> 16) & 0xFF
    ps = (cid >> 8) & 0xFF
    sa = cid & 0xFF
    pgn = (pf << 8 | ps) if pf >= 0xF0 else (pf << 8)
    da = None if pf >= 0xF0 else ps
    ts = frame.timestamp

    st = _pgn_stats.setdefault(pgn, {"count": 0, "name": PGN_NAMES.get(pgn, ""),
                                     "last_ts": ts, "sa": sa, "da": da})
    st["count"] += 1
    st["last_ts"] = ts

    if pgn == 0xEC00:
        _feed_tp(sa, ps, data)
        return
    if pgn == 0xEB00:
        result = _feed_tp_dt(sa, ps, data)
        if result:
            pgn2, payload = result
            _ev("TP", "重组完成 SA=%02X PGN=0x%04X %d字节: %s"
                % (sa, pgn2, len(payload), _hex(payload[:24])))
        return

    if pgn == 0xEE00:
        info = _decode_name(data)
        if info:
            info["ts"] = time.time()
            _nodes[sa] = info
            _ev("AC", "节点 %02X 上线: %s / %s（厂商 %d）"
                % (sa, info["device_class_name"], info["function_name"],
                   info["manufacturer"]))
            _decoded.append((ts, "地址声明", "SA=%02X %s/%s 厂商%d"
                             % (sa, info["device_class_name"], info["function_name"],
                                info["manufacturer"])))
    elif pgn in PGN_NAMES:
        _decoded.append((ts, PGN_NAMES[pgn], "SA=%02X %s" % (sa, _hex(data[:16]))))
    if len(_decoded) > 1500:
        del _decoded[:600]


def activate(context):
    global _running
    _sessions.clear()
    _nodes.clear()
    _pgn_stats.clear()
    del _events[:]
    del _decoded[:]

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QTabWidget
        )
        from PyQt6.QtCore import QTimer
    except ImportError:
        sin.output.append("ISOBUS 监视插件需要 PyQt6: pip install PyQt6")
        return

    _running = True
    win = sin.ui.create_window("ISOBUS (ISO 11783) 监视")
    win.resize(960, 630)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    summary = QLabel("等待 ISOBUS 数据（29 位扩展帧）...")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("清零")
    export_btn = QPushButton("导出 CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    node_tab = QWidget()
    nv = QVBoxLayout(node_tab)
    node_tree = QTreeWidget()
    node_tree.setHeaderLabels(["源地址", "NAME Hex", "设备类", "功能", "厂商码",
                               "身份号", "声明时间"])
    node_tree.setRootIsDecorated(False)
    node_tree.setAlternatingRowColors(True)
    nh = node_tree.header()
    nh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    nv.addWidget(node_tree, 1)

    pgn_tab = QWidget()
    pv = QVBoxLayout(pgn_tab)
    pgn_tree = QTreeWidget()
    pgn_tree.setHeaderLabels(["PGN", "名称", "帧数", "最后 SA", "最后时间"])
    pgn_tree.setRootIsDecorated(False)
    pgn_tree.setAlternatingRowColors(True)
    pgn_tree.setSortingEnabled(True)
    ph = pgn_tree.header()
    ph.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    pv.addWidget(pgn_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(node_tab, "在线节点")
    tabs.addTab(pgn_tab, "PGN 统计")
    tabs.addTab(log_tab, "事件日志")

    context.on_frame(_on_frame)

    def refresh():
        summary.setText("扩展帧 %d    在线节点 %d    PGN %d    TP 会话 %d"
                        % (_total, len(_nodes), len(_pgn_stats), len(_sessions)))

        node_tree.clear()
        for sa, n in sorted(_nodes.items()):
            node_tree.addTopLevelItem(QTreeWidgetItem([
                "%02X" % sa, n["name_hex"], n["device_class_name"], n["function_name"],
                str(n["manufacturer"]), str(n["ident"]),
                time.strftime("%H:%M:%S", time.localtime(n["ts"]))]))

        pgn_tree.setSortingEnabled(False)
        pgn_tree.clear()
        for pgn, st in _pgn_stats.items():
            pgn_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%04X" % pgn, st["name"], str(st["count"]),
                "%02X" % st["sa"], "%.3f" % st["last_ts"]]))
        pgn_tree.setSortingEnabled(True)

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-150:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

    timer = QTimer()
    timer.timeout.connect(refresh)
    timer.start(600)

    def on_clear():
        _sessions.clear()
        _nodes.clear()
        _pgn_stats.clear()
        del _events[:]
        del _decoded[:]

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 ISOBUS 记录 CSV",
                                              "isobus_monitor.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("源地址,NAME,设备类,功能,厂商码,身份号\n")
                for sa, n in sorted(_nodes.items()):
                    f.write("%02X,%s,%s,%s,%d,%d\n"
                            % (sa, n["name_hex"], n["device_class_name"],
                               n["function_name"], n["manufacturer"], n["ident"]))
                f.write("\nPGN统计\nPGN,名称,帧数\n")
                for pgn, st in _pgn_stats.items():
                    f.write("0x%04X,%s,%d\n" % (pgn, st["name"], st["count"]))
                f.write("\n事件日志\n时间,类型,内容\n")
                for ts, k, t in _events:
                    f.write("%s,%s,%s\n" % (time.strftime("%H:%M:%S", time.localtime(ts)), k, t))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("isobusMonitor.open", on_open_cmd, "协议: ISOBUS 监视")

    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append("ISOBUS 监视插件已加载（订阅实时帧，地址声明 + TP 重组被动监视）")


def deactivate():
    global _running
    _running = False
    sin.output.append("ISOBUS 监视插件已停用")
