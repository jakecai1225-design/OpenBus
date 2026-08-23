# -*- coding: utf-8 -*-
"""gbt27930-monitor 插件 — GB/T 27930 国标充电协议监视
功能：
- BMS(0xF4)/充电机(0x56) 双向报文识别与全命名（CHM/BHM/CRM/BRM/BCP/BRO/CRO/BCL/BCS/CCS/BSM/BST/CST/CML/CSD/CST）
- 关键报文字段解码：CHM/BHM/CRM/BCL/CCS/BSM（电压/电流/温度/SOC 等）
- J1939 传输协议（TP.BAM/RTS-CTS）被动重组长报文（BRM/BCS/CSD）
- 充电阶段状态机可视化（握手→参数配置→充电→结束）
- 报文统计 + 事件日志 + CSV 导出；纯监视不发送
依赖: pip install PyQt6
"""

import time

import sin

# 完整 29 位 ID → 报文名（GB/T 27930-2015）
MSG_TABLE = {
    0x1827F456: "CHM 充电机握手",
    0x182756F4: "BHM BMS 握手",
    0x1801F456: "CRM 充电机辨识",
    0x1CEB56F4: "BRM BMS 辨识(长)",
    0x180156F4: "BCP 电池充电参数",
    0x1802F456: "CML 充电机最大输出能力",
    0x180456F4: "BRO 电池充电准备就绪",
    0x1804F456: "CRO 充电机准备就绪",
    0x180556F4: "BCL 电池充电需求",
    0x1CEC56F4: "BCS 电池充电状态(长)",
    0x1806F456: "CCS 充电机充电状态",
    0x180756F4: "BSM 动力蓄电池状态",
    0x1808F456: "CST 充电机中止充电",
    0x180856F4: "BST BMS 中止充电",
    0x180956F4: "BSD BMS 统计数据",
    0x1809F456: "CSD 充电机统计数据(长)",
    0x180B56F4: "BEM BMS 错误报文",
    0x180BF456: "CEM 充电机错误报文",
}

STAGES = [
    ("握手", ["CHM", "BHM"]),
    ("辨识", ["CRM", "BRM"]),
    ("参数配置", ["BCP", "CTS", "CML", "BRO", "CRO", "CML"]),
    ("充电", ["BCL", "BCS", "CCS", "BSM"]),
    ("结束", ["BST", "CST", "BSD", "CSD", "BEM", "CEM"]),
]

_sessions = {}       # TP 重组: (da,sa) → {...}
_stats = {}          # 完整 ID → {name, count, last_ts, decoded}
_decoded = []        # [(ts, name, text)]
_events = []
_stage = "未知"
_total = 0
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1200:
        del _events[:600]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _le(data, start, length, scale=1.0, offset=0.0):
    idx = start - 1
    if idx + length > len(data):
        return None
    v = 0
    for i in range(length):
        v |= data[idx + i] << (8 * i)
    return v * scale + offset


def _decode_fields(cid, data):
    """返回 [(字段名, 值文本)]；仅解码有把握的字段，其余交由原始 Hex 展示"""
    out = []
    if cid == 0x1827F456:      # CHM
        v = _le(data, 2, 2, 0.1)
        out.append(("充电机通信协议版本", "GB/T 27930-%d" % data[0] if data else "?"))
        out.append(("最高允许充电电压", "%.1f V" % v if v is not None else "?"))
    elif cid == 0x182756F4:    # BHM
        v = _le(data, 1, 2, 0.1)
        c = _le(data, 3, 2, 0.1)
        out.append(("最高允许充电总电压", "%.1f V" % v if v is not None else "?"))
        out.append(("电池额定容量", "%.1f Ah" % c if c is not None else "?"))
    elif cid == 0x1801F456:    # CRM
        out.append(("辨识结果", "成功(0xAA)" if data and data[0] == 0xAA else "辨识中(0x%02X)" % (data[0] if data else 0)))
        out.append(("充电机编号", "%d" % data[1] if len(data) > 1 else "?"))
        out.append(("辨识报文编号", "%d" % data[2] if len(data) > 2 else "?"))
    elif cid == 0x180456F4:    # BRO
        out.append(("电池充电准备就绪", "就绪(0xAA)" if data and data[0] == 0xAA else "未就绪(0x%02X)" % (data[0] if data else 0)))
    elif cid == 0x1804F456:    # CRO
        out.append(("充电机准备就绪", "就绪(0xAA)" if data and data[0] == 0xAA else "未就绪(0x%02X)" % (data[0] if data else 0)))
    elif cid == 0x180556F4:    # BCL
        v = _le(data, 1, 2, 0.1)
        i = _le(data, 3, 2, 0.1, -400)
        out.append(("需求电压", "%.1f V" % v if v is not None else "?"))
        out.append(("需求电流", "%.1f A" % i if i is not None else "?"))
        if len(data) > 5:
            out.append(("充电模式", {1: "恒流", 2: "恒压"}.get(data[5], "%d" % data[5])))
    elif cid == 0x1806F456:    # CCS
        v = _le(data, 1, 2, 0.1)
        i = _le(data, 3, 2, 0.1, -400)
        out.append(("充电机输出电压", "%.1f V" % v if v is not None else "?"))
        out.append(("充电机输出电流", "%.1f A" % i if i is not None else "?"))
        if len(data) > 5:
            out.append(("累计充电时间", "%d min" % (data[5] | (data[6] << 8) if len(data) > 6 else data[5])))
    elif cid == 0x180756F4:    # BSM
        v = _le(data, 1, 2, 0.01)
        t = data[3] if len(data) > 3 else None
        out.append(("最高单体动力蓄电池电压", "%.2f V" % v if v is not None else "?"))
        out.append(("最高动力蓄电池温度", "%d °C" % t if t is not None else "?"))
        if len(data) > 4:
            out.append(("单体电压最低探针序号", "%d" % data[4]))
        if len(data) > 5:
            out.append(("SOC 估算值", "%d %%" % (data[5] // 2 if data[5] <= 200 else data[5])))
    elif cid == 0x180856F4:    # BST（BMS 中止）
        if data:
            reason = []
            bits = ["中止充电原因标志", "中止充电故障原因", "中止充电错误原因"]
            out.append(("中止原因字节", _hex(data[:min(4, len(data))])))
    elif cid == 0x1808F456:    # CST（充电机中止）
        if data:
            out.append(("中止原因字节", _hex(data[:min(4, len(data))])))
    return out


def _feed_tp(sa, da, data):
    """TP.CM/TP.DT 重组（BRM/BCS/CSD 等长报文）"""
    if not data:
        return None
    key = (da, sa)
    cmd = data[0]
    if cmd in (0x20, 0x10) and len(data) >= 8:   # BAM / RTS
        total = data[1] | (data[2] << 8)
        npkts = data[3]
        pgn = data[5] | (data[6] << 8) | (data[7] << 16)
        _sessions[key] = {"total": total, "npkts": npkts, "pgn": pgn, "buf": {}}
        _ev("TP", "%s: SA=%02X PGN=0x%04X %d字节" % ("BAM" if cmd == 0x20 else "RTS", sa, pgn, total))
        return None
    if cmd == 0xFF:
        _ev("TP", "Abort: SA=%02X" % sa)
        return None
    st = _sessions.get(key)
    if not st:
        return None
    seq = data[0]
    st["buf"][seq] = bytes(data[1:])
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
    global _total, _stage
    if not _running or not frame.extended:
        return
    data = frame.data
    if not data:
        return
    cid = frame.id & 0x1FFFFFFF
    ts = frame.timestamp
    pf = (cid >> 16) & 0xFF
    ps = (cid >> 8) & 0xFF
    sa = cid & 0xFF
    pgn = (pf << 8 | ps) if pf >= 0xF0 else (pf << 8)

    # TP 帧单独处理
    if pgn in (0xEC00, 0xEB00):
        _total += 1
        if pgn == 0xEC00:
            _feed_tp(sa, ps, data)
        else:
            result = _feed_tp(sa, ps, data)
            if result:
                pgn2, payload = result
                name = "TP 重组 PGN 0x%04X" % pgn2
                _decoded.append((ts, name, "%d 字节: %s" % (len(payload), _hex(payload[:24]))))
                if len(_decoded) > 2000:
                    del _decoded[:800]
        return

    name = MSG_TABLE.get(cid)
    if name is None:
        # 未知名表：按 SA 判断方向，仍记录 PGN
        if sa in (0xF4, 0x56):
            name = "PGN 0x%04X SA=%02X" % (pgn, sa)
        else:
            return
    _total += 1
    st = _stats.setdefault(cid, {"name": name, "count": 0, "last_ts": ts})
    st["count"] += 1
    st["last_ts"] = ts

    for field, text in _decode_fields(cid, data):
        _decoded.append((ts, name, "%s: %s" % (field, text)))
    if len(_decoded) > 3000:
        del _decoded[:1000]

    # 阶段推断
    for stage, keys in STAGES:
        if any(name.startswith(k) for k in keys):
            if _stage != stage:
                _stage = stage
                _ev("STAGE", "进入充电阶段: %s（%s）" % (stage, name))
            break


def activate(context):
    global _running, _stage
    _sessions.clear()
    _stats.clear()
    del _decoded[:]
    del _events[:]
    _stage = "未知"

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QTabWidget, QFrame
        )
        from PyQt6.QtCore import QTimer, Qt
    except ImportError:
        sin.output.append("国标充电监视插件需要 PyQt6: pip install PyQt6")
        return

    _running = True
    win = sin.ui.create_window("GB/T 27930 国标充电监视")
    win.resize(980, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    # ---- 顶部：阶段指示 ----
    stage_row = QHBoxLayout()
    stage_labels = {}
    for i, (stage, _) in enumerate(STAGES):
        box = QFrame()
        box.setFixedHeight(38)
        box.setStyleSheet("QFrame{border:1px solid #bbb;border-radius:6px;background:#f2f2f2;}"
                          "QFrame:active{background:#e6f4ff;}")
        lbl = QLabel(stage)
        lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay = QHBoxLayout(box)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(lbl)
        stage_labels[stage] = (box, lbl)
        stage_row.addWidget(box, 1)
        if i < len(STAGES) - 1:
            arrow = QLabel("→")
            arrow.setFixedWidth(18)
            stage_row.addWidget(arrow)
    layout.addLayout(stage_row)

    top = QHBoxLayout()
    summary = QLabel("等待充电报文（29 位扩展帧，BMS=0xF4 / 充电机=0x56）...")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("清零")
    export_btn = QPushButton("导出 CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    # Tab1: 解码流
    dec_tab = QWidget()
    dv = QVBoxLayout(dec_tab)
    dec_tree = QTreeWidget()
    dec_tree.setHeaderLabels(["时间戳(s)", "报文", "解码"])
    dec_tree.setRootIsDecorated(False)
    dec_tree.setAlternatingRowColors(True)
    dh = dec_tree.header()
    dh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    dv.addWidget(dec_tree, 1)

    # Tab2: 报文统计
    stat_tab = QWidget()
    sv = QVBoxLayout(stat_tab)
    stat_tree = QTreeWidget()
    stat_tree.setHeaderLabels(["CAN ID", "报文", "帧数", "最后时间"])
    stat_tree.setRootIsDecorated(False)
    stat_tree.setAlternatingRowColors(True)
    stat_tree.setSortingEnabled(True)
    sh = stat_tree.header()
    sh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(stat_tree, 1)

    # Tab3: 事件日志
    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(dec_tab, "解码流")
    tabs.addTab(stat_tab, "报文统计")
    tabs.addTab(log_tab, "事件日志")

    context.on_frame(_on_frame)

    ACTIVE_CSS = "QFrame{border:2px solid #16a34a;border-radius:6px;background:#dcfce7;font-weight:bold;}"
    IDLE_CSS = "QFrame{border:1px solid #bbb;border-radius:6px;background:#f2f2f2;}"

    def refresh():
        summary.setText("扩展帧 %d    报文类型 %d    解码记录 %d    当前阶段: %s"
                        % (_total, len(_stats), len(_decoded), _stage))
        for stage, (box, lbl) in stage_labels.items():
            box.setStyleSheet(ACTIVE_CSS if stage == _stage else IDLE_CSS)

        dec_tree.setSortingEnabled(False)
        dec_tree.clear()
        for ts, name, text in _decoded[-400:]:
            dec_tree.addTopLevelItem(QTreeWidgetItem(["%.3f" % ts, name, text]))
        dec_tree.scrollToBottom()
        dec_tree.setSortingEnabled(True)

        stat_tree.setSortingEnabled(False)
        stat_tree.clear()
        for cid, st in _stats.items():
            stat_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%08X" % cid, st["name"], str(st["count"]), "%.3f" % st["last_ts"]]))
        stat_tree.setSortingEnabled(True)

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
        _stats.clear()
        del _decoded[:]
        del _events[:]
        global _stage
        _stage = "未知"

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出国标充电记录 CSV",
                                              "gbt27930_monitor.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("时间戳s,报文,解码\n")
                for ts, name, text in _decoded:
                    f.write("%.3f,%s,%s\n" % (ts, name, text))
                f.write("\n报文统计\nCAN ID,报文,帧数\n")
                for cid, st in _stats.items():
                    f.write("0x%08X,%s,%d\n" % (cid, st["name"], st["count"]))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("gbt27930Monitor.open", on_open_cmd, "协议: 国标充电监视")

    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append("国标充电监视插件已加载（订阅实时帧，GB/T 27930 被动解码）")


def deactivate():
    global _running
    _running = False
    sin.output.append("国标充电监视插件已停用")
