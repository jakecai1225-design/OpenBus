# -*- coding: utf-8 -*-
"""j1939-analyzer 插件 — SAE J1939 协议分析
功能：
- 29 位 ID 拆解：优先级 / PF / PS(DA或PGN扩展) / SA
- 内置高频 PGN 解码表（EEC1/CCVS/ET1/LFE1/电子引擎小时/DM1/DM2/地址声明等）
- DM1/DM2 故障码解码（SPN/FMI/OC，Lamp 状态位）
- 传输协议重组：TP.BAM(0xEC00)/TP.DT(0xEB00) 被动多帧重组（含 RTS/CTS/EOFLA/Abort 识别）
- 地址声明（0xEE00）解析：SA → NAME（厂商码/功能等）
- PGN 统计表 + 解码值实时表 + CSV 导出；纯监视不发送
依赖: pip install PyQt6
"""

import time

import sin

# PGN 名称表（常用）
PGN_NAMES = {
    0xEE00: "地址声明 Address Claimed",
    0xFECA: "DM1 主动故障码",
    0xFECB: "DM2 历史故障码",
    0xF004: "EEC1 发动机电控 1",
    0xFEF1: "CCVS 车速车速",
    0xFEEE: "ET1 发动机温度",
    0xFEE9: "LFE1 燃油经济性",
    0xFEEA: "电子引擎小时",
    0xF003: "EEC2 发动机电控 2",
    0xFEF2: "CCVS2",
    0xFDC1: "EBC1 电控制动 1",
    0xFEF5: "ET1 扩展温度",
    0xFEF7: "电子发动机空气温度",
    0xFED0: "VG1 车速组分",
    0xFDA9: "CCVS3",
    0xEB00: "TP.DT 传输协议数据",
    0xEC00: "TP.CM 传输协议命令",
    0xEAFF: "RQST 请求",
    0xEA00: "RQST 请求(旧)",
    0xFE6C: "ET1 发动机温度扩展",
    0xFDC2: "EBC2 电控制动 2",
    0xFDC5: "EBC1 电控制动配置",
    0xFEF0: "CCVS1 车速(旧)",
    0xFDB7: "CCVS1 车速(旧)",
}

LAMP_BITS = [
    (0, "保护灯(红)"), (1, "琥珀色灯"), (2, "故障灯(红)"), (3, "故障灯(黄)"),
    (4, "Malfunction 灯(MIL)"), (5, "停止灯(红)"), (6, "警示灯(黄)"), (7, "保护灯"),
]

# NAME 高频字段（J1939-81）：byte4=功能, byte2&0xE0 + byte3=厂商码, byte0~2低位=身份号
FUNC_NAMES = {
    0: "非特定", 3: "变速箱", 4: "仪表组", 5: "仪表#2", 8: "驱动桥缓速器",
    9: "变速箱缓速器", 11: "发动机缓速器", 16: "电控喷油泵", 27: "发动机#2",
    30: "发动机#3", 32: "主离合器/传动", 33: "发动机(主)", 35: "制动/ABS",
    36: "仪表盘#2", 37: "车身控制器", 38: "驾驶室控制器", 39: "挂车制动",
    40: "车辆中心控制器", 45: "照明控制器", 85: "混合动力系统", 98: "电池管理系统",
    128: "国四后处理#1", 129: "国四后处理#2", 130: "国四后处理#3",
    131: "国四后处理#4", 132: "国四后处理#5", 133: "发电机", 134: "电机#1",
    135: "电机#2", 136: "电机#3", 249: "发动机#4? (特殊)",
}

# SPN 解码表：PGN → [(起始字节(1基), 字节数, SPN, 名称, 因子, 偏移, 单位)]
SPN_DECODES = {
    0xF004: [  # EEC1
        (2, 1, 512, "驾驶员需求扭矩%", 1, -125, "%"),
        (3, 1, 513, "实际发动机扭矩%", 1, -125, "%"),
        (4, 2, 190, "发动机转速", 0.125, 0, "rpm"),
    ],
    0xFEF1: [  # CCVS
        (1, 2, 84, "基于车轮的车速", 0.00390625, 0, "km/h"),
    ],
    0xFEEE: [  # ET1
        (1, 1, 110, "发动机冷却液温度", 1, -40, "°C"),
        (2, 1, 174, "燃油温度", 1, -40, "°C"),
    ],
    0xFEE9: [  # LFE1
        (1, 2, 183, "发动机燃油消耗率", 0.05, 0, "L/h"),
    ],
    0xFEEA: [  # 电子引擎小时
        (1, 4, 247, "发动机总运行时间", 0.05, 0, "h"),
    ],
    0xFDC1: [  # EBC1
        (8, 2, 520, "相对车速", 0.00390625, 0, "km/h"),
    ],
    0xFEF7: [
        (1, 1, 171, "环境温度", 1, -40, "°C"),
        (3, 1, 172, "进气温度", 1, -40, "°C"),
    ],
}

_sessions = {}       # TP 会话: (da, sa) → {"total","npkts","pgn","buf":{},"ts"}
_pgn_stats = {}      # pgn → {count, name, last_ts, sa}
_addr_claims = {}    # sa → {"name_hex", "manufacturer", "function", "function_name", "ts"}
_decoded = []        # 解码值记录 [(ts, pgn, text)]
_events = []
_total = 0
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1500:
        del _events[:800]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _le(data, start, length):
    """1 基起始字节小端取值"""
    idx = start - 1
    if idx + length > len(data):
        return None
    v = 0
    for i in range(length):
        v |= data[idx + i] << (8 * i)
    return v


def decode_pgns():
    return PGN_NAMES


def _decode_spns(pgn, data):
    out = []
    for (start, length, spn, name, factor, offset, unit) in SPN_DECODES.get(pgn, []):
        raw = _le(data, start, length)
        if raw is None:
            continue
        out.append((spn, name, raw * factor + offset, unit))
    return out


def _decode_dm1(data):
    """DM1/DM2 载荷：byte0 灯状态 + 每 4 字节一个 DTC"""
    out = []
    if len(data) >= 1:
        lamp = data[0]
        lamps = [name for bit, name in LAMP_BITS if lamp & (1 << bit) and bit < 4]
        out.append((None, "激活灯状态", "、".join(lamps) if lamps else "全部灭", ""))
    n = (len(data) - 1) // 4 if len(data) > 1 else 0
    for i in range(n):
        chunk = data[1 + 4 * i: 5 + 4 * i]
        spn = (chunk[0] | (chunk[1] << 8) | ((chunk[2] & 0x07) << 16))
        fmi = (chunk[2] >> 3) & 0x1F
        oc = chunk[3] if chunk[3] < 127 else "N/A"
        out.append((spn, "故障码", "SPN%d / FMI%d / OC%s" % (spn, fmi, oc), ""))
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
        "function_name": FUNC_NAMES.get(function, "功能 %d" % function),
    }


def _feed_tp(sa, da, data):
    """处理 TP.CM / TP.DT 被动重组"""
    if len(data) < 1:
        return None
    cmd = data[0]
    key = (da, sa)
    if cmd == 0x20:          # BAM 公告
        if len(data) >= 8:
            total = data[1] | (data[2] << 8)
            npkts = data[3]
            pgn = data[5] | (data[6] << 8) | (data[7] << 16)
            _sessions[key] = {"total": total, "npkts": npkts, "pgn": pgn,
                              "buf": {}, "ts": time.time()}
            _ev("TP", "BAM: SA=%02X DA=%02X PGN=0x%04X %d字节 %d包" % (sa, da, pgn, total, npkts))
    elif cmd == 0x10:        # RTS
        if len(data) >= 8:
            total = data[1] | (data[2] << 8)
            npkts = data[3]
            pgn = data[5] | (data[6] << 8) | (data[7] << 16)
            _sessions[key] = {"total": total, "npkts": npkts, "pgn": pgn,
                              "buf": {}, "ts": time.time()}
            _ev("TP", "RTS: SA=%02X DA=%02X PGN=0x%04X %d字节" % (sa, da, pgn, total))
    elif cmd == 0x17:        # CTS
        _ev("TP", "CTS: SA=%02X DA=%02X" % (sa, da))
    elif cmd == 0x13:        # EOFLA/ACK
        _ev("TP", "EOFLA/ACK: SA=%02X DA=%02X" % (sa, da))
    elif cmd == 0xFF:        # Abort
        _ev("TP", "Abort: SA=%02X DA=%02X 原因=%d" % (sa, da, data[1] if len(data) > 1 else -1))
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
        del _sessions[key]
        _ev("TP", "重组完成: SA=%02X PGN=0x%04X %d字节" % (sa, st["pgn"], st["total"]))
        return st["pgn"], payload
    return None


def _on_frame(frame):
    global _total
    if not _running:
        return
    data = frame.data
    if not data or not frame.extended:
        return              # J1939 仅 29 位扩展帧
    _total += 1
    cid = frame.id
    prio = (cid >> 26) & 0x07
    pf = (cid >> 16) & 0xFF
    ps = (cid >> 8) & 0xFF
    sa = cid & 0xFF
    pgn = (pf << 8 | ps) if pf >= 0xF0 else (pf << 8)
    ts = frame.timestamp

    # ---- 传输协议 ----
    if pgn == 0xEC00:
        _feed_tp(sa, ps, data)
        st = _pgn_stats.setdefault(pgn, {"count": 0, "name": PGN_NAMES.get(pgn, ""),
                                         "last_ts": ts, "sa": sa})
        st["count"] += 1
        st["last_ts"] = ts
        return
    if pgn == 0xEB00:
        result = _feed_tp_dt(sa, ps, data)
        st = _pgn_stats.setdefault(pgn, {"count": 0, "name": PGN_NAMES.get(pgn, ""),
                                         "last_ts": ts, "sa": sa})
        st["count"] += 1
        st["last_ts"] = ts
        if result:
            pgn2, payload = result
            _record_decoded(ts, pgn2, "TP 重组 %d 字节: %s" % (len(payload), _hex(payload[:24])))
            for spn, name, value, unit in _decode_spns(pgn2, payload):
                _record_decoded(ts, pgn2, "SPN%d %s = %g %s" % (spn, name, value, unit))
        return

    # ---- 地址声明 ----
    if pgn == 0xEE00:
        claim = _decode_addr_claim(data)
        if claim:
            _addr_claims[sa] = claim
            claim["ts"] = time.time()
            _ev("AC", "节点 %02X 声明: 厂商码 %d 功能 %s" % (sa, claim["manufacturer"],
                                                          claim["function_name"]))

    # ---- 统计 ----
    st = _pgn_stats.setdefault(pgn, {"count": 0, "name": PGN_NAMES.get(pgn, ""),
                                     "last_ts": ts, "sa": sa})
    st["count"] += 1
    st["last_ts"] = ts

    # ---- 解码 ----
    if pgn in (0xFECA, 0xFECB):
        for spn, name, value, unit in _decode_dm1(data):
            _record_decoded(ts, pgn, "%s: %s" % (name, value))
    for spn, name, value, unit in _decode_spns(pgn, data):
        _record_decoded(ts, pgn, "SPN%d %s = %g %s" % (spn, name, value, unit))


def _record_decoded(ts, pgn, text):
    _decoded.append((ts, pgn, text))
    if len(_decoded) > 3000:
        del _decoded[:1000]


def activate(context):
    global _running
    _sessions.clear()
    _pgn_stats.clear()
    _addr_claims.clear()
    del _decoded[:]
    del _events[:]

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QTabWidget
        )
        from PyQt6.QtCore import QTimer
    except ImportError:
        sin.output.append("J1939 分析插件需要 PyQt6: pip install PyQt6")
        return

    _running = True
    win = sin.ui.create_window("J1939 协议分析")
    win.resize(980, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    summary = QLabel("等待 J1939 数据（29 位扩展帧）...")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("清零")
    export_btn = QPushButton("导出 CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    # Tab1: PGN 统计
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

    # Tab2: 解码值
    dec_tab = QWidget()
    dv = QVBoxLayout(dec_tab)
    dec_tree = QTreeWidget()
    dec_tree.setHeaderLabels(["时间戳(s)", "PGN", "解码"])
    dec_tree.setRootIsDecorated(False)
    dec_tree.setAlternatingRowColors(True)
    dh = dec_tree.header()
    dh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    dv.addWidget(dec_tree, 1)

    # Tab3: 地址声明
    addr_tab = QWidget()
    av = QVBoxLayout(addr_tab)
    addr_tree = QTreeWidget()
    addr_tree.setHeaderLabels(["源地址", "NAME (Hex)", "厂商码", "功能码", "功能", "声明时间"])
    addr_tree.setRootIsDecorated(False)
    addr_tree.setAlternatingRowColors(True)
    ah = addr_tree.header()
    ah.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    av.addWidget(addr_tree, 1)

    # Tab4: 事件日志
    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(pgn_tab, "PGN 统计")
    tabs.addTab(dec_tab, "解码值")
    tabs.addTab(addr_tab, "地址声明")
    tabs.addTab(log_tab, "事件日志")

    context.on_frame(_on_frame)

    def refresh():
        summary.setText("扩展帧 %d    PGN %d    解码记录 %d    在线节点 %d    TP 会话 %d"
                        % (_total, len(_pgn_stats), len(_decoded), len(_addr_claims),
                           len(_sessions)))

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

        addr_tree.clear()
        for sa, c in sorted(_addr_claims.items()):
            addr_tree.addTopLevelItem(QTreeWidgetItem([
                "%02X" % sa, c["name_hex"], str(c["manufacturer"]),
                str(c["function"]), c["function_name"],
                time.strftime("%H:%M:%S", time.localtime(c["ts"]))]))

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
        _pgn_stats.clear()
        _addr_claims.clear()
        del _decoded[:]
        del _events[:]
        summary.setText("等待 J1939 数据（29 位扩展帧）...")

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 J1939 记录 CSV",
                                              "j1939_analysis.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("PGN,名称,帧数\n")
                for pgn, st in _pgn_stats.items():
                    f.write("0x%04X,%s,%d\n" % (pgn, st["name"], st["count"]))
                f.write("\n地址声明\n源地址,NAME,厂商码,功能码,功能\n")
                for sa, c in sorted(_addr_claims.items()):
                    f.write("%02X,%s,%d,%d,%s\n" % (sa, c["name_hex"], c["manufacturer"],
                                                    c["function"], c["function_name"]))
                f.write("\n解码值\n时间戳,PGN,内容\n")
                for ts, pgn, text in _decoded:
                    f.write("%.3f,0x%04X,%s\n" % (ts, pgn, text))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("j1939Analyzer.open", on_open_cmd, "协议: J1939 分析")

    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append("J1939 分析插件已加载（订阅实时帧，被动监视 + PGN/SPN 解码）")


def deactivate():
    global _running
    _running = False
    sin.output.append("J1939 分析插件已停用")
