# -*- coding: utf-8 -*-
"""nmea2000-decoder 插件 — NMEA 2000（船舶）PGN 解码
功能：
- 29 位 ID 拆解（优先级/PF/PS/SA），PDU1/PDU2 自动识别
- Fast Packet 多帧重组（PGN ≥ 0xF000 且首字节为序号计数）
- 内置常用 PGN 解码：航向 127250 / 舵角 127245 / 航速 128259 /
  位置 129025 / COG&SOG 129026 / 水深 128267 / 风速风向 130306 /
  环境温度 130311 / 发动机转速 127488 等
- PGN 统计表 + 解码值流 + CSV 导出；纯监视不发送
依赖: pip install PyQt6
"""

import time

import sin

PGN_NAMES = {
    59392: "ISO 请求/确认",
    59904: "ISO 请求 (RQST)",
    60928: "ISO 地址声明",
    126992: "系统时间",
    126996: "产品信息 (长)",
    126998: "配置信息 (长)",
    127245: "舵角 Rudder",
    127250: "航向 Heading",
    127251: "回转速率 Rate of Turn",
    127257: "横倾/纵倾 Attitude",
    127488: "发动机转速 Rapid",
    127493: "变速箱/节流阀",
    127505: "液位 Fluid Level",
    128259: "航速 Speed (水参考)",
    128267: "水深 Water Depth",
    129025: "位置快速更新 Position Rapid",
    129026: "COG & SOG 快速更新",
    129029: "GNSS 位置 (长)",
    129283: "航路点 Cross Track Error",
    129284: "导航数据 Navigation Data",
    129539: "AIS DGNSS 电台 (长)",
    129794: "AIS Class A 静态/航行数据 (长)",
    129793: "AIS Class A 位置报告",
    129809: "AIS Class B 静态数据 (长)",
    129810: "AIS Class B 位置报告",
    130306: "风速风向 Wind Data",
    130310: "环境参数(旧)",
    130311: "环境参数 Environmental",
    130312: "温度 Temperature",
    130314: "实际压力 Pressure",
    130316: "温度扩展范围 (长)",
    130577: "风向数据 Direction Data",
}

# 解码表：pgn → [(偏移字节(0基), 长度, 名称, 因子, 单位, 备注)]
PGN_DECODES = {
    127245: [(1, 2, "舵角", 0.0001, "rad", "×57.3 转角度")],
    127250: [(1, 2, "航向", 0.0001, "rad", "×57.3 转角度"),
             (3, 2, "磁偏角", 0.0001, "rad", ""),
             (5, 2, "磁差", 0.0001, "rad", "")],
    127251: [(1, 4, "回转速率", 3.125e-06, "rad/s", "")],
    127488: [(1, 2, "发动机转速", 0.25, "rpm", ""),
             (3, 2, "增压压力", 0.1, "hPa", "offset=0")],
    127505: [(1, 4, "液位", 0.0000039, "比例", "0-100%")],
    128259: [(1, 2, "航速(水参考)", 0.01, "kn", ""),
             (3, 2, "航速(纵向)", 0.01, "m/s", "")],
    128267: [(1, 4, "水深", 0.01, "m", "")],
    129025: [(0, 4, "纬度", 1e-07, "°", ""),
             (4, 4, "经度", 1e-07, "°", "")],
    129026: [(1, 2, "航向 COG", 0.00573, "°", ""),
             (3, 2, "航速 SOG", 0.01, "kn", "")],
    130306: [(1, 2, "风速", 0.01, "m/s", ""),
             (3, 2, "风向角", 0.0001, "rad", "×57.3 转角度")],
    130311: [(1, 2, "温度", 0.01, "K", "-273.15 转摄氏"),
             (3, 2, "湿度", 0.004, "%", "")],
    130312: [(1, 2, "温度", 0.01, "K", "-273.15 转摄氏")],
}

RAD_TO_DEG = 57.29577951308232
K_TO_C = -273.15

# 已知 Fast Packet 长报文 PGN（其余 PDU2 单帧报文直接解码）
FAST_PACKET_PGNS = {
    126996,   # 产品信息
    126998,   # 配置信息
    129029,   # GNSS 位置（长）
    129284,   # 导航数据（长）
    129539,   # AIS DGNSS（长）
    129794,   # AIS Class A 静态（长）
    129809,   # AIS Class B 静态（长）
    130316,   # 扩展温度（长）
}

_fp_sessions = {}     # (pgn, sa) → {"expected", "buf": {}, "ts"}
_pgn_stats = {}
_decoded = []
_events = []
_total = 0
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1000:
        del _events[:500]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _le(data, off, length):
    if off + length > len(data):
        return None
    v = 0
    for i in range(length):
        v |= data[off + i] << (8 * i)
    return v


def _decode(pgn, data):
    out = []
    for (off, length, name, factor, unit, note) in PGN_DECODES.get(pgn, []):
        raw = _le(data, off, length)
        if raw is None:
            continue
        value = raw * factor
        if unit == "rad":
            out.append((name, "%.1f°" % (value * RAD_TO_DEG)))
        elif unit == "K":
            out.append((name, "%.1f °C" % (value + K_TO_C)))
        else:
            out.append((name, "%g %s" % (value, unit)))
    return out


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
    pgn = (cid >> 8) & 0x3FFFF          # 18 位 PGN
    if pf < 0xF0:
        pgn &= 0x3FF00                   # PDU1：PS 为目标地址，不属 PGN
    ts = frame.timestamp

    st = _pgn_stats.setdefault(pgn, {"count": 0, "name": PGN_NAMES.get(pgn, ""),
                                     "last_ts": ts, "sa": sa})
    st["count"] += 1
    st["last_ts"] = ts

    # ---- Fast Packet 重组（仅已知长报文 PGN）----
    payload = data
    if pgn in FAST_PACKET_PGNS and len(data) == 8:
        idx = (data[0] >> 5) & 0x07
        counter = data[0] & 0x1F
        key = (pgn, sa)
        if idx == 0:
            expected = data[1]
            _fp_sessions[key] = {"expected": expected, "buf": {0: bytes(data[2:8])},
                                 "counter": counter, "ts": time.time()}
            return                      # 头包不单独解码
        sess = _fp_sessions.get(key)
        if sess is not None and sess.get("counter") == counter:
            sess["buf"][idx] = bytes(data[1:8])
            sess["ts"] = time.time()
            have = sum(len(v) for v in sess["buf"].values())
            if have >= sess["expected"]:
                payload = b""
                for i in sorted(sess["buf"].keys()):
                    payload += sess["buf"][i]
                payload = payload[:sess["expected"]]
                del _fp_sessions[key]
                _ev("FP", "PGN %d Fast Packet 重组完成 %d 字节" % (pgn, len(payload)))
            else:
                return                  # 组包中
        # 无会话的续包：退回按单帧原始展示

    if pgn == 60928:           # ISO 地址声明
        _ev("AC", "节点 %d 地址声明 NAME=%s" % (sa, _hex(data[:8])))

    for field, value in _decode(pgn, payload):
        _decoded.append((ts, pgn, "%s: %s" % (field, value)))
        if len(_decoded) > 2000:
            del _decoded[:800]


def activate(context):
    global _running
    _fp_sessions.clear()
    _pgn_stats.clear()
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
        sin.output.append("NMEA 2000 解码插件需要 PyQt6: pip install PyQt6")
        return

    _running = True
    win = sin.ui.create_window("NMEA 2000 解码")
    win.resize(940, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    summary = QLabel("等待 NMEA 2000 数据（29 位扩展帧）...")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("清零")
    export_btn = QPushButton("导出 CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    dec_tab = QWidget()
    dv = QVBoxLayout(dec_tab)
    dec_tree = QTreeWidget()
    dec_tree.setHeaderLabels(["时间戳(s)", "PGN", "名称", "解码值"])
    dec_tree.setRootIsDecorated(False)
    dec_tree.setAlternatingRowColors(True)
    dh = dec_tree.header()
    dh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    dv.addWidget(dec_tree, 1)

    stat_tab = QWidget()
    sv = QVBoxLayout(stat_tab)
    stat_tree = QTreeWidget()
    stat_tree.setHeaderLabels(["PGN", "名称", "帧数", "最后 SA", "最后时间"])
    stat_tree.setRootIsDecorated(False)
    stat_tree.setAlternatingRowColors(True)
    stat_tree.setSortingEnabled(True)
    sh = stat_tree.header()
    sh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(stat_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(dec_tab, "解码流")
    tabs.addTab(stat_tab, "PGN 统计")
    tabs.addTab(log_tab, "事件日志")

    context.on_frame(_on_frame)

    def refresh():
        summary.setText("扩展帧 %d    PGN %d    解码记录 %d    Fast Packet 会话 %d"
                        % (_total, len(_pgn_stats), len(_decoded), len(_fp_sessions)))

        dec_tree.setSortingEnabled(False)
        dec_tree.clear()
        for ts, pgn, text in _decoded[-400:]:
            dec_tree.addTopLevelItem(QTreeWidgetItem([
                "%.3f" % ts, str(pgn), PGN_NAMES.get(pgn, ""), text]))
        dec_tree.scrollToBottom()
        dec_tree.setSortingEnabled(True)

        stat_tree.setSortingEnabled(False)
        stat_tree.clear()
        for pgn, st in _pgn_stats.items():
            stat_tree.addTopLevelItem(QTreeWidgetItem([
                str(pgn), st["name"], str(st["count"]), str(st["sa"]),
                "%.3f" % st["last_ts"]]))
        stat_tree.setSortingEnabled(True)

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-120:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

    timer = QTimer()
    timer.timeout.connect(refresh)
    timer.start(600)

    def on_clear():
        _fp_sessions.clear()
        _pgn_stats.clear()
        del _decoded[:]
        del _events[:]

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 NMEA 2000 记录 CSV",
                                              "nmea2000_decode.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("时间戳s,PGN,名称,解码值\n")
                for ts, pgn, text in _decoded:
                    f.write("%.3f,%d,%s,%s\n" % (ts, pgn, PGN_NAMES.get(pgn, ""), text))
                f.write("\nPGN统计\nPGN,名称,帧数\n")
                for pgn, st in _pgn_stats.items():
                    f.write("%d,%s,%d\n" % (pgn, st["name"], st["count"]))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("nmea2000Decoder.open", on_open_cmd, "协议: NMEA 2000")

    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append("NMEA 2000 解码插件已加载（订阅实时帧，PGN 解码 + Fast Packet 重组）")


def deactivate():
    global _running
    _running = False
    sin.output.append("NMEA 2000 解码插件已停用")
