# -*- coding: utf-8 -*-
"""obd2-scanner 插件 — OBD-II 车辆诊断扫描（ISO 15031-5 / SAE J1979）
功能：
- 模式 01 动态数据：支持 PID 自动发现（00/20/40/60/80/A0/C0 位图链），
  内置常用 PID 解码库（转速/车速/温度/电压/空燃比等 30+ 项），周期轮询
- 模式 02 冻结帧 / 03 读故障码 / 04 清故障码（确认对话框）/ 07 待定故障码
- 模式 09 车辆信息：VIN（多帧收集）、支持的 INFO
- DTC 文本库（常见 P0xxx）；请求 ID（0x7DF 功能/0x7E0 物理）可配
- 日志 + CSV 导出；仅在用户点击时发送请求（默认不发送）
依赖: pip install PyQt6
"""

import time

import sin

# ---- 模式 01 PID 解码库: pid → (名称, 解码函数(bytes data→文本), 单位) ----
def _d_a(data):
    return data[0]

def _d_pct(data):
    return "%.1f" % (data[0] * 100.0 / 255.0)

def _d_temp(data):
    return data[0] - 40

def _d_rpm(data):
    return (data[0] * 256 + data[1]) // 4

def _d_speed(data):
    return data[0]

def _d_16_100(data):
    return "%.3f" % ((data[0] * 256 + data[1]) / 100.0)

def _d_16_1000(data):
    return "%.1f" % ((data[0] * 256 + data[1]) / 1000.0)

def _d_16_4(data):
    return (data[0] * 256 + data[1]) / 4.0

def _d_16_20(data):
    return "%.2f" % ((data[0] * 256 + data[1]) / 20.0)

def _d_afr(data):
    return "%.2f" % ((data[0] * 256 + data[1]) / 32768.0 * 14.7)

def _d_secs(data):
    return data[0] * 256 + data[1]

PIDS = {
    0x03: ("燃油系统状态", lambda d: "0x%02X" % d[0], ""),
    0x04: ("计算负载值", _d_pct, "%"),
    0x05: ("冷却液温度", _d_temp, "°C"),
    0x06: ("短期燃油修正 B1", lambda d: "%.1f" % ((d[0] - 128) * 100.0 / 128.0), "%"),
    0x07: ("长期燃油修正 B1", lambda d: "%.1f" % ((d[0] - 128) * 100.0 / 128.0), "%"),
    0x0A: ("燃油压力", lambda d: d[0] * 3, "kPa"),
    0x0B: ("进气歧管绝压", _d_a, "kPa"),
    0x0C: ("发动机转速", _d_rpm, "rpm"),
    0x0D: ("车速", _d_speed, "km/h"),
    0x0E: ("点火提前角", lambda d: "%.1f" % (d[0] / 2.0 - 64.0), "°"),
    0x0F: ("进气温度", _d_temp, "°C"),
    0x10: ("空气流量 MAF", _d_16_100, "g/s"),
    0x11: ("节气门位置", _d_pct, "%"),
    0x1F: ("启动后运行时间", _d_secs, "s"),
    0x21: ("故障码亮灯时里程", _d_16_4, "km"),
    0x2F: ("燃油液位", _d_pct, "%"),
    0x31: ("行驶距离", _d_16_4, "km"),
    0x33: ("大气压力", _d_a, "kPa"),
    0x42: ("控制模块电压", _d_16_1000, "V"),
    0x43: ("负载绝对值", lambda d: "%.1f" % (d[0] * 100.0 / 255.0 * 2.5), "%"),
    0x44: ("空燃比 (λ→14.7)", _d_afr, ""),
    0x45: ("相对节气门位置", _d_pct, "%"),
    0x46: ("环境温度", _d_temp, "°C"),
    0x47: ("绝对节气门位置 B", _d_pct, "%"),
    0x49: ("相对油门踏板位置", _d_pct, "%"),
    0x4C: ("指令增压器", lambda d: "%.3f" % (d[0] / 255.0), "V"),
    0x5A: ("相对油门踏板传感器 D", _d_pct, "%"),
    0x5C: ("机油温度", _d_temp, "°C"),
    0x5E: ("发动机燃油消耗率", _d_16_20, "L/h"),
    0x61: ("驾驶员需求扭矩", lambda d: "%.1f" % (d[0] - 125.0), "%"),
    0x62: ("实际发动机扭矩", lambda d: "%.1f" % (d[0] - 125.0), "%"),
    0x67: ("冷却液温度传感器 B", _d_temp, "°C"),
}

# 常见 DTC 文本库（节选 SAE 通用定义）
DTC_TEXTS = {
    "P0100": "空气流量传感器电路故障",
    "P0101": "空气流量传感器信号范围异常",
    "P0110": "进气温度传感器电路故障",
    "P0113": "进气温度传感器信号过高",
    "P0115": "冷却液温度传感器电路故障",
    "P0116": "冷却液温度传感器信号范围异常",
    "P0120": "节气门位置传感器电路故障",
    "P0121": "节气门位置传感器信号范围异常",
    "P0122": "节气门位置传感器信号过低",
    "P0123": "节气门位置传感器信号过高",
    "P0130": "氧传感器电路故障（B1S1）",
    "P0131": "氧传感器电压过低（B1S1）",
    "P0132": "氧传感器电压过高（B1S1）",
    "P0135": "氧传感器加热器电路故障（B1S1）",
    "P0171": "燃油系统过稀（B1）",
    "P0172": "燃油系统过浓（B1）",
    "P0201": "喷油嘴电路故障（缸1）",
    "P0202": "喷油嘴电路故障（缸2）",
    "P0203": "喷油嘴电路故障（缸3）",
    "P0204": "喷油嘴电路故障（缸4）",
    "P0230": "燃油泵初级电路故障",
    "P0300": "检测到随机/多缸失火",
    "P0301": "检测到缸1失火",
    "P0302": "检测到缸2失火",
    "P0303": "检测到缸3失火",
    "P0304": "检测到缸4失火",
    "P0325": "爆震传感器电路故障（B1）",
    "P0335": "曲轴位置传感器电路故障",
    "P0340": "凸轮轴位置传感器电路故障",
    "P0401": "EGR 流量不足",
    "P0420": "催化器效率低于阈值（B1）",
    "P0440": "EVAP 系统故障",
    "P0442": "EVAP 系统小泄漏",
    "P0455": "EVAP 系统大泄漏",
    "P0500": "车速传感器故障",
    "P0505": "怠速控制系统故障",
    "P0601": "ECU 内部校验和错误",
    "P0602": "ECU 编程错误",
    "P0700": "变速箱控制系统（请求亮灯）",
    "P1000": "OBD 就绪检测未完成",
    "U0100": "与 ECM/PCM 通讯丢失",
    "U0101": "与 TCM 通讯丢失",
    "U0121": "与 ABS 控制模块通讯丢失",
    "U0155": "与组合仪表通讯丢失",
}

# ---- 状态 ----
_supported = {}        # pid → True
_values = {}           # pid → (文本, 单位, ts)
_dtcs = []             # [(code, text, mode)]
_vin = ""              # 收集中的 VIN
_vin_src = None
_pending = None        # {"kind", "pid", "ts", "data": [], "mode"}
_queue = []            # 待发请求
_log = []
_tx_id = 0x7DF
_polling = False       # 周期轮询模式
_poll_interval_ms = 500
_running = True


def _lg(text):
    _log.append((time.time(), text))
    if len(_log) > 800:
        del _log[:400]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _decode_dtc(a, b):
    prefix = "PCBU"[(a >> 6) & 0x03]
    return "%s%04X" % (prefix, ((a & 0x3F) << 8) | b)


def _dtc_text(code):
    return DTC_TEXTS.get(code, "")


def _on_frame(frame):
    global _pending, _vin
    if not _running:
        return
    fid = frame.id
    if not (0x7E8 <= fid <= 0x7EF):
        return
    data = frame.data
    if not data:
        return
    mode = data[0] - 0x40
    ts = frame.timestamp

    if mode in (1, 2) and len(data) >= 3:
        pid = data[1]
        payload = data[2:]
        if mode == 1:
            if pid == 0x00 or pid in (0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0):
                base = pid + 1
                for i in range(32):
                    if i < len(payload) * 8:
                        byte = payload[i // 8]
                        if byte & (0x80 >> (i % 8)):
                            _supported[base + i] = True
            elif pid == 0x01 and len(payload) >= 1:
                mil = "点亮" if payload[0] & 0x80 else "熄灭"
                cnt = payload[0] & 0x7F
                _values[0x01] = ("MIL:%s DTC数:%d" % (mil, cnt), "", ts)
            elif pid in PIDS:
                name, fn, unit = PIDS[pid]
                try:
                    _values[pid] = (str(fn(payload)), unit, ts)
                except Exception:
                    _values[pid] = ("解码失败: %s" % _hex(payload), "", ts)
        _lg("RX 0x%X 模式%02X PID %02X: %s" % (fid, mode, pid, _hex(data[:8])))
        _pending = None if _pending and _pending.get("pid") == pid else _pending
    elif mode in (3, 7) and len(data) >= 2:
        cnt = data[1]
        for i in range((len(data) - 2) // 2):
            code = _decode_dtc(data[2 + 2 * i], data[3 + 2 * i])
            _dtcs.append((code, _dtc_text(code), "03" if mode == 3 else "07"))
        _lg("RX 0x%X 模式%02X: DTC 数 %d" % (fid, mode, cnt))
    elif mode == 4:
        _lg("RX 0x%X 模式04 清码响应" % fid)
    elif mode == 9 and len(data) >= 2:
        info = data[1]
        if info == 0x02 and len(data) >= 3:
            # VIN 多帧：data[2] = 帧序号 01..N，后续字节为 ASCII
            seq = data[2]
            chars = data[3:8]
            if seq == 1:
                _vin = ""
            _vin += "".join(chr(b) for b in chars if 0x20 <= b < 0x7F)
        _lg("RX 0x%X 模式09 INFO %02X: %s" % (fid, info, _hex(data[:8])))
    if _pending:
        _pending = None     # 任意响应即完成当前请求


def _request(kind, mode, pid=0):
    """把请求加入队列（由 tick 顺序发出）"""
    _queue.append({"kind": kind, "mode": mode, "pid": pid,
                   "ts": 0, "tries": 0})


def activate(context):
    global _running, _polling, _tx_id
    _supported.clear()
    _values.clear()
    del _dtcs[:]
    del _queue[:]
    _log.clear()
    _pending = None
    _polling = False

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QTabWidget, QComboBox, QSpinBox
        )
        from PyQt6.QtCore import QTimer
    except ImportError:
        sin.output.append("OBD-II 扫描插件需要 PyQt6: pip install PyQt6")
        return

    _running = True
    win = sin.ui.create_window("OBD-II 诊断扫描 (ISO 15031-5)")
    win.resize(960, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    top.addWidget(QLabel("请求 ID:"))
    req_combo = QComboBox()
    req_combo.addItem("0x7DF 功能寻址", 0x7DF)
    req_combo.addItem("0x7E0 物理寻址", 0x7E0)
    top.addWidget(req_combo)
    poll_spin = QSpinBox()
    poll_spin.setRange(100, 5000)
    poll_spin.setSingleStep(100)
    poll_spin.setValue(_poll_interval_ms)
    poll_spin.setSuffix(" ms")
    top.addWidget(QLabel("轮询间隔"))
    top.addWidget(poll_spin)
    top.addStretch(1)
    support_btn = QPushButton("发现支持 PID")
    clear_dtc_btn = QPushButton("读取故障码")
    pending_btn = QPushButton("读取待定码(07)")
    clear_btn = QPushButton("清除故障码(04)")
    vin_btn = QPushButton("读取 VIN")
    poll_btn = QPushButton("开始轮询")
    top.addWidget(support_btn)
    top.addWidget(clear_dtc_btn)
    top.addWidget(pending_btn)
    top.addWidget(clear_btn)
    top.addWidget(vin_btn)
    top.addWidget(poll_btn)
    layout.addLayout(top)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    # Tab1: 动态数据
    data_tab = QWidget()
    dv = QVBoxLayout(data_tab)
    dv.addWidget(QLabel("模式 01 动态数据（先「发现支持 PID」，再「开始轮询」）"))
    val_tree = QTreeWidget()
    val_tree.setHeaderLabels(["PID", "名称", "值", "单位", "时间戳"])
    val_tree.setRootIsDecorated(False)
    val_tree.setAlternatingRowColors(True)
    vh = val_tree.header()
    vh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    dv.addWidget(val_tree, 1)
    export_btn = QPushButton("导出 CSV")
    dv.addWidget(export_btn)

    # Tab2: 故障码
    dtc_tab = QWidget()
    tv = QVBoxLayout(dtc_tab)
    dtc_tree = QTreeWidget()
    dtc_tree.setHeaderLabels(["故障码", "描述", "来源模式"])
    dtc_tree.setRootIsDecorated(False)
    dtc_tree.setAlternatingRowColors(True)
    tv.addWidget(dtc_tree, 1)

    # Tab3: 日志
    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(data_tab, "动态数据")
    tabs.addTab(dtc_tab, "故障码")
    tabs.addTab(log_tab, "日志")

    vin_label = QLabel("VIN: -")
    vin_label.setStyleSheet("font-weight: bold;")
    layout.addWidget(vin_label)

    context.on_frame(_on_frame)

    def _send_now(mode, pid):
        req = bytes([mode, pid]) if mode in (1, 2, 9) else bytes([mode])
        sin.frames.send(_tx_id, req.ljust(8, b"\x55")[:8])
        _lg("TX 0x%X: %s" % (_tx_id, _hex(req.ljust(8, b"\x55")[:8])))

    def tick():
        global _pending
        now = time.time()
        if _pending is not None:
            if now - _pending["ts"] > 0.5:
                _lg("超时: 模式%02X PID %02X 无响应" % (_pending["mode"], _pending["pid"]))
                _pending = None
            else:
                return
        if _queue:
            req = _queue.pop(0)
            _pending = {"kind": req["kind"], "mode": req["mode"], "pid": req["pid"],
                        "ts": now}
            _send_now(req["mode"], req["pid"])
            return
        if _polling:
            # 轮询：所有已发现且在解码库中的 PID
            for pid in sorted(_supported):
                if pid in PIDS:
                    _request("poll", 1, pid)
            for pid in sorted(_values):
                if pid in PIDS and pid not in _supported:
                    _request("poll", 1, pid)

    timer = QTimer()
    timer.timeout.connect(tick)
    timer.start(50)

    def refresh():
        val_tree.setSortingEnabled(False)
        val_tree.clear()
        for pid, (text, unit, ts) in sorted(_values.items()):
            if pid == 0x01:
                name = "MIL 状态 / DTC 数"
            else:
                name = PIDS[pid][0] if pid in PIDS else "PID %02X" % pid
            val_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%02X" % pid, name, text, unit, "%.1f" % ts]))
        val_tree.setSortingEnabled(True)

        dtc_tree.clear()
        for code, text, mode in _dtcs:
            dtc_tree.addTopLevelItem(QTreeWidgetItem([code, text, mode]))

        vin_label.setText("VIN: %s" % (_vin.strip() or "-"))
        if _log:
            log_view.setPlainText("\n".join(
                "[%s] %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), t)
                for ts, t in _log[-160:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

    refresher = QTimer()
    refresher.timeout.connect(refresh)
    refresher.start(500)

    def on_support():
        _supported.clear()
        for base in (0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0):
            _request("support", 1, base)
        _lg("开始发现支持 PID（7 组位图）")

    def on_read_dtc():
        _request("dtc", 3, 0)

    def on_read_pending():
        _request("dtc", 7, 0)

    def on_clear_dtc():
        if QMessageBox.question(win, "确认", "确认清除故障码与冻结帧？\n"
                                "该操作会影响车辆排放就绪状态。") != \
                QMessageBox.StandardButton.Yes:
            return
        _request("clear", 4, 0)
        _lg("发送清码请求（模式 04）")

    def on_vin():
        global _vin
        _vin = ""
        _request("vin", 9, 0x02)

    def on_poll():
        global _polling
        _polling = not _polling
        poll_btn.setText("停止轮询" if _polling else "开始轮询")
        if _polling and not _supported:
            _lg("提示：未发现支持 PID，将按解码库轮询")

    def on_tx_changed(index):
        global _tx_id
        _tx_id = req_combo.itemData(index) or 0x7DF

    def on_interval(v):
        global _poll_interval_ms
        _poll_interval_ms = int(v)

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 OBD 数据 CSV",
                                              "obd2_data.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("PID,名称,值,单位,时间戳\n")
                for pid, (text, unit, ts) in sorted(_values.items()):
                    name = PIDS[pid][0] if pid in PIDS else "PID %02X" % pid
                    f.write("0x%02X,%s,%s,%s,%.1f\n" % (pid, name, text, unit, ts))
                f.write("\n故障码\n故障码,描述,来源\n")
                for code, text, mode in _dtcs:
                    f.write("%s,%s,%s\n" % (code, text, mode))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("obd2Scanner.open", on_open_cmd, "诊断: OBD-II 扫描")

    req_combo.currentIndexChanged.connect(on_tx_changed)
    poll_spin.valueChanged.connect(on_interval)
    support_btn.clicked.connect(on_support)
    clear_dtc_btn.clicked.connect(on_read_dtc)
    pending_btn.clicked.connect(on_read_pending)
    clear_btn.clicked.connect(on_clear_dtc)
    vin_btn.clicked.connect(on_vin)
    poll_btn.clicked.connect(on_poll)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append("OBD-II 扫描插件已加载（订阅响应帧 0x7E8-0x7EF，按需发送请求）")


def deactivate():
    global _running, _polling
    _running = False
    _polling = False
    sin.output.append("OBD-II 扫描插件已停用")
