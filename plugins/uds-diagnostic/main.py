"""uds-diagnostic 插件 — UDS 诊断客户端（G10 强化版，对标 ZCANPro / CANoe / TSMaster）

功能（ISO 14229 + ISO 15765-2）：
- 传输层 uds_isotp：SF/FF/CF/FC 完整状态机（BS/STmin/WAIT/OVFLW），接收自动回流控
- 客户端 uds_client：P2/P2* 超时、NRC 0x78 自动续等、功能寻址、服务编解码
- Tab1 诊断服务：分类服务树 + 每服务专属参数表单 + 响应解码（+原始请求）
- Tab2 DID 面板：内置+自定义 DID 字典（dids.json 持久化）、批量读、周期轮询、按类型写入
- Tab3 DTC 管理：状态掩码复选、19 02 解析为 DTC 表（编号文本+状态位解码）、一键清 14
- Tab4 安全访问：种子-密钥交互，算法三选（演示 XOR / Python 表达式 / 算法文件）
- Tab5 刷写助手：预检（10 03→27→85→28）→ 34/36/37 完整传输 → 31 01 FF01 校验，进度条
- 诊断日志：结构化表格（时间/方向/CAN ID/PDU/解析），可暂停、导出 CSV、显示 ISO-TP 帧
- 工具栏：物理/功能/响应三 ID 可配、功能寻址开关、会话按钮联动、3E 心跳

依赖: pip install PyQt6
"""

import json
import os
import sys
import time

import sin

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from uds_isotp import IsotpLayer
from uds_client import (UdsClient, SESSIONS, DTC_STATUS_BITS,
                        dtc_to_text, dtc_status_text,
                        calc_key_demo, calc_key_expr, calc_key_file,
                        encode_10, encode_11, encode_14, encode_19, encode_22,
                        encode_2e, encode_27, encode_28, encode_2f, encode_31,
                        encode_34, encode_36, encode_37, encode_3e, encode_85,
                        encode_raw)

DIDS_FILE = os.path.join(_HERE, "dids.json")

BUILTIN_DIDS = {
    "F190": {"name": "VIN 车辆识别码", "type": "ascii"},
    "F18C": {"name": "ECU 装配日期 (BCD)", "type": "hex"},
    "F191": {"name": "Boot 软件版本", "type": "ascii"},
    "F192": {"name": "FVN 制造商车辆识别", "type": "ascii"},
    "F193": {"name": "当前诊断会话", "type": "u8"},
    "F194": {"name": "车辆制造商", "type": "ascii"},
    "F195": {"name": "系统名称", "type": "ascii"},
    "F196": {"name": "ECU 软件版本号", "type": "ascii"},
    "F197": {"name": "ECU 零件号", "type": "ascii"},
    "F198": {"name": "ECU 序列号", "type": "ascii"},
    "0100": {"name": "车型信息", "type": "hex"},
}

DID_TYPES = ["hex", "ascii", "u8", "u16", "u32"]

SERVICE_TREE = [
    ("会话与控制", [
        (0x10, "10 DiagnosticSessionControl 会话控制"),
        (0x11, "11 ECUReset ECU 复位"),
        (0x3E, "3E TesterPresent 在线诊断"),
        (0x28, "28 CommunicationControl 通信控制"),
        (0x85, "85 ControlDTCSetting DTC 设置"),
    ]),
    ("数据读写", [
        (0x22, "22 ReadDataByIdentifier 读 DID"),
        (0x2E, "2E WriteDataByIdentifier 写 DID"),
        (0x2F, "2F InputOutputControlByIdentifier IO 控制"),
    ]),
    ("DTC 诊断", [
        (0x14, "14 ClearDiagnosticInformation 清除 DTC"),
        (0x19, "19 ReadDTCInformation 读 DTC"),
    ]),
    ("安全访问", [
        (0x27, "27 SecurityAccess 安全访问"),
    ]),
    ("例程与刷写", [
        (0x31, "31 RoutineControl 例程控制"),
        (0x34, "34 RequestDownload 请求下载"),
        (0x36, "36 TransferData 数据传输"),
        (0x37, "37 RequestTransferExit 传输退出"),
    ]),
    ("高级", [
        (0x00, "原始请求（hex 直发）"),
    ]),
]

# 全局（deactivate 需要）
_isotp = None
_client = None


def _decode_did(dtype, data):
    """按类型解码 DID 数据 → (原始hex, 可读值)"""
    hex_str = " ".join(f"{b:02X}" for b in data)
    if dtype == "ascii":
        return hex_str, data.decode("ascii", "replace").rstrip("\x00 ")
    if dtype == "u8":
        return hex_str, str(data[0]) if data else "-"
    if dtype == "u16" and len(data) >= 2:
        return hex_str, str(int.from_bytes(data[:2], "big"))
    if dtype == "u32" and len(data) >= 4:
        return hex_str, str(int.from_bytes(data[:4], "big"))
    return hex_str, hex_str


def _encode_did_value(dtype, text):
    """写值文本 → bytes（按类型）"""
    if dtype == "ascii":
        return text.encode("ascii")
    if dtype == "u8":
        return int(text, 0).to_bytes(1, "big")
    if dtype == "u16":
        return int(text, 0).to_bytes(2, "big")
    if dtype == "u32":
        return int(text, 0).to_bytes(4, "big")
    return bytes.fromhex(text.replace(" ", ""))


def _fit_len(key, n):
    """密钥长度对齐 seed（长取低位字节，短左补零）"""
    if len(key) >= n:
        return key[-n:]
    return b"\x00" * (n - len(key)) + key


def activate(context):
    global _isotp, _client

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QGroupBox, QLabel,
            QPushButton, QComboBox, QLineEdit, QCheckBox, QSpinBox, QTreeWidget,
            QTreeWidgetItem, QTabWidget, QTableWidget, QTableWidgetItem,
            QHeaderView, QSplitter, QProgressBar, QFileDialog, QMessageBox,
            QAbstractItemView
        )
        from PyQt6.QtCore import QTimer, Qt
        from PyQt6.QtGui import QColor, QFont
    except ImportError as e:
        msg = (
            "UDS diagnostic requires PyQt6 in the plugin-host Python "
            f"(MSYS2: pacman -S mingw-w64-ucrt-x86_64-python-pyqt6): {e}"
        )
        sin.output.append(msg)
        raise RuntimeError(msg) from e

    # ================================================================
    #  协议栈
    # ================================================================
    def on_send_frame(can_id, data):
        sin.frames.send(can_id, data)

    _isotp = IsotpLayer(on_send_frame)
    _client = UdsClient(_isotp)

    win = sin.ui.create_window("UDS 诊断 (ISO 14229) — G10")
    win.resize(1020, 700)

    central = QWidget()
    win.setCentralWidget(central)
    root = QVBoxLayout(central)
    root.setContentsMargins(6, 6, 6, 6)

    # ================================================================
    #  日志表格（底部，先建好供各处引用）
    # ================================================================
    log_group = QGroupBox("诊断日志")
    log_v = QVBoxLayout(log_group)

    log_table = QTableWidget(0, 5)
    log_table.setHorizontalHeaderLabels(["时间", "方向", "CAN ID", "PDU", "解析"])
    log_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    log_table.verticalHeader().setVisible(False)
    log_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    log_table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)
    log_table.horizontalHeader().setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)

    log_btn_row = QHBoxLayout()
    log_pause_check = QCheckBox("暂停")
    log_frame_check = QCheckBox("显示 ISO-TP 帧")
    log_export_btn = QPushButton("导出 CSV")
    log_clear_btn = QPushButton("清空")
    for w in (log_pause_check, log_frame_check):
        log_btn_row.addWidget(w)
    log_btn_row.addStretch()
    for w in (log_export_btn, log_clear_btn):
        log_btn_row.addWidget(w)

    log_v.addWidget(log_table, 1)
    log_v.addLayout(log_btn_row)

    _log_rows = []          # (时间, 方向, id, pdu, 解析) 缓存，暂停时暂存

    def _log_row(direction, can_id, pdu, note, color=None):
        if log_pause_check.isChecked():
            if len(_log_rows) < 5000:
                _log_rows.append((time.time(), direction, can_id, pdu, note))
            return
        ts = time.time()
        _append_log_row(ts, direction, can_id, pdu, note, color)
        while log_table.rowCount() > 2000:
            log_table.removeRow(0)

    def _append_log_row(ts, direction, can_id, pdu, note, color=None):
        tstr = time.strftime("%H:%M:%S", time.localtime(ts)) + f".{int(ts % 1 * 1000):03d}"
        hex_str = " ".join(f"{b:02X}" for b in pdu[:48]) if isinstance(pdu, (bytes, bytearray)) else str(pdu)
        more = " ..." if isinstance(pdu, (bytes, bytearray)) and len(pdu) > 48 else ""
        idstr = f"0x{can_id:X}" if isinstance(can_id, int) else str(can_id)
        colors = {"TX": QColor("#1565C0"), "RX": QColor("#2E7D32"), "ERR": QColor("#C62828"),
                  "FC": QColor("#6A1B9A")}
        c = colors.get(direction if not color else color, QColor("#333"))
        row = log_table.rowCount()
        log_table.insertRow(row)
        for col, text in enumerate((tstr, direction, idstr, hex_str + more, note or "")):
            item = QTableWidgetItem(text)
            item.setForeground(c)
            log_table.setItem(row, col, item)
        bar = log_table.verticalScrollBar()
        bar.setValue(bar.maximum())

    def _on_log_flush():
        """取消暂停后回放暂存行"""
        if not log_pause_check.isChecked() and _log_rows:
            for ts, direction, can_id, pdu, note in _log_rows[:2000]:
                _append_log_row(ts, direction, can_id, pdu, note)
            del _log_rows[:2000]

    log_pause_check.toggled.connect(lambda _c: _on_log_flush())

    def _on_export_csv():
        path, _ = QFileDialog.getSaveFileName(win, "导出诊断日志", "uds_log.csv", "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("时间,方向,CAN ID,PDU,解析\n")
                for r in range(log_table.rowCount()):
                    cells = [log_table.item(r, c).text() if log_table.item(r, c) else ""
                             for c in range(5)]
                    f.write(",".join('"' + c.replace('"', '""') + '"' for c in cells) + "\n")
            sin.output.append(f"UDS 日志已导出: {path}")
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    log_export_btn.clicked.connect(_on_export_csv)
    log_clear_btn.clicked.connect(lambda: (log_table.setRowCount(0), _log_rows.clear()))

    _isotp.on_log = lambda d, cid, data, note: (
        _log_row("FC" if "FC" in note else ("TX" if d == "TX" else "RX"), cid, bytes(data),
                 f"[ISO-TP {note}]")
        if log_frame_check.isChecked() else None)
    _isotp.on_error = lambda msg: _log_row("ERR", "-", b"", msg)
    _client.on_pdu = lambda resp, desc: _log_row("RX", _isotp.rx_id, resp, desc)
    _client.on_pending = lambda: _log_row("RX", _isotp.rx_id, b"\x7F\x00\x78", "NRC 0x78 响应挂起，P2* 续等")

    # ================================================================
    #  统一发送入口
    # ================================================================
    _flashing = [False]      # 刷写中标志（list 便于闭包内改写）

    def uds_send(pdu, on_done=None, expect_response=True, tag="请求"):
        functional = func_check.isChecked()
        _isotp.tx_id = tx_spin.value()
        _isotp.func_id = func_spin.value()
        _isotp.rx_id = rx_spin.value()
        cid = _isotp.func_id if functional else _isotp.tx_id
        _log_row("TX", cid, pdu, tag)
        _client.request(pdu, functional=functional,
                        expect_response=expect_response and not functional,
                        on_done=on_done)

    def _wrap_done(on_done):
        """包装回调：统一记录失败原因到日志"""
        def cb(ok, resp, note):
            if not ok:
                _log_row("ERR", "-", b"", note or "失败")
            if on_done:
                on_done(ok, resp, note)
        return cb

    # ================================================================
    #  工具栏：寻址 + 会话 + 心跳
    # ================================================================
    bar = QGroupBox()
    bar_row = QHBoxLayout(bar)

    def _hex_spin(lo, hi, val):
        s = QSpinBox()
        s.setRange(lo, hi)
        s.setDisplayIntegerBase(16)
        s.setPrefix("0x")
        s.setValue(val)
        return s

    tx_spin = _hex_spin(1, 0x7FF, 0x7E0)
    func_spin = _hex_spin(1, 0x7FF, 0x7DF)
    rx_spin = _hex_spin(1, 0x7FF, 0x7E8)
    func_check = QCheckBox("功能寻址发送")

    for lbl, w in (("物理 ID:", tx_spin), ("功能 ID:", func_spin),
                   ("响应 ID:", rx_spin), (None, func_check)):
        if lbl:
            bar_row.addWidget(QLabel(lbl))
        bar_row.addWidget(w)
    bar_row.addWidget(QLabel("    "))
    sep = QLabel("|")
    sep.setStyleSheet("color:#aaa;")
    bar_row.addWidget(sep)

    session_label = QLabel("会话: 未知")
    session_label.setStyleSheet("font-weight:bold;")

    def _set_session(name):
        session_label.setText(f"会话: {name}")

    def _go_session(session):
        def cb(ok, resp, note):
            if ok and resp and resp[:1] == b"\x50":
                _set_session(SESSIONS.get(resp[1], f"0x{resp[1]:02X}"))
        uds_send(encode_10(session), _wrap_done(cb), tag="会话切换 10")

    for code, label in ((0x01, "默认"), (0x03, "扩展"), (0x02, "编程")):
        b = QPushButton(label)
        b.setToolTip(f"DiagnosticSessionControl 0x{code:02X}")
        b.clicked.connect(lambda _c, s=code: _go_session(s))
        bar_row.addWidget(b)
    bar_row.addWidget(session_label)
    bar_row.addStretch()

    tp_check = QCheckBox("3E 心跳 2s")
    tp_check.setToolTip("TesterPresent 3E 80（抑制响应），周期 2s")
    bar_row.addWidget(tp_check)

    root.addWidget(bar)

    splitter = QSplitter(Qt.Orientation.Vertical)
    root.addWidget(splitter, 1)

    tabs = QTabWidget()
    splitter.addWidget(tabs)
    splitter.addWidget(log_group)
    splitter.setSizes([460, 240])

    # ================================================================
    #  Tab 1: 诊断服务（服务树 + 动态表单）
    # ================================================================
    svc_tab = QWidget()
    svc_h = QHBoxLayout(svc_tab)

    svc_tree = QTreeWidget()
    svc_tree.setHeaderLabel("服务")
    for cat, items in SERVICE_TREE:
        cat_item = QTreeWidgetItem([cat])
        cat_item.setFlags(Qt.ItemFlag.ItemIsEnabled)   # 分类不可选
        f = QFont()
        f.setBold(True)
        cat_item.setFont(0, f)
        svc_tree.addTopLevelItem(cat_item)
        for sid, name in items:
            it = QTreeWidgetItem([name])
            it.setData(0, Qt.ItemDataRole.UserRole, sid)
            cat_item.addChild(it)
    svc_tree.expandAll()

    svc_right = QWidget()
    svc_right_v = QVBoxLayout(svc_right)
    svc_title = QLabel("选择左侧服务")
    svc_title.setStyleSheet("font-weight:bold;")
    svc_param_box = QGroupBox("参数")
    svc_form = QFormLayout(svc_param_box)
    svc_send_btn = QPushButton("发送请求")
    svc_send_btn.setMinimumHeight(32)
    svc_resp_label = QLabel("最近响应: —")
    svc_resp_label.setWordWrap(True)
    svc_resp_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
    svc_right_v.addWidget(svc_title)
    svc_right_v.addWidget(svc_param_box)
    svc_right_v.addWidget(svc_send_btn)
    svc_right_v.addWidget(QLabel("响应:"))
    svc_right_v.addWidget(svc_resp_label)
    svc_right_v.addStretch()

    svc_h.addWidget(svc_tree, 1)
    svc_h.addWidget(svc_right, 2)

    _svc_maker = [None]      # 当前服务的请求构造器

    def _clear_form():
        while svc_form.count():
            item = svc_form.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()

    def _hex_edit(placeholder, default=""):
        e = QLineEdit(default)
        e.setPlaceholderText(placeholder)
        return e

    def _parse_hex(text):
        return bytes.fromhex(text.replace(" ", "").replace("0x", ""))

    def _parse_did(text):
        b = _parse_hex(text)
        if len(b) != 2:
            raise ValueError("DID 需为 2 字节 hex（如 F190）")
        return int.from_bytes(b, "big")

    def _parse_n(text, n, what):
        b = _parse_hex(text)
        if len(b) != n:
            raise ValueError(f"{what}需为 {n} 字节 hex")
        return b

    # ---- 每服务表单（返回请求构造器；参数非法时抛 ValueError）----
    def _f_10():
        c = QComboBox()
        for k, v in SESSIONS.items():
            c.addItem(v, k)
        svc_form.addRow("目标会话:", c)
        return lambda: encode_10(c.currentData())

    def _f_11():
        c = QComboBox()
        for k, v in ((0x01, "01 硬复位"), (0x02, "02 点火复位"), (0x03, "03 软复位")):
            c.addItem(v, k)
        svc_form.addRow("复位类型:", c)
        return lambda: encode_11(c.currentData())

    def _f_3e():
        c = QComboBox()
        c.addItem("80 抑制响应（推荐）", 0x80)
        c.addItem("00 需响应", 0x00)
        svc_form.addRow("子功能:", c)
        def sync_expect(*_a):
            _svc_expect[0] = c.currentData() != 0x80
        c.currentIndexChanged.connect(sync_expect)
        sync_expect()
        return lambda: encode_3e(c.currentData())

    def _f_28():
        cc = QComboBox()
        for k, v in ((0x00, "00 使能 Rx+Tx"), (0x01, "01 使能 Rx 禁 Tx"),
                     (0x02, "02 禁 Rx 使能 Tx"), (0x03, "03 禁 Rx+Tx")):
            cc.addItem(v, k)
        ct = QComboBox()
        for k, v in ((0x01, "01 普通报文"), (0x02, "02 网络管理"), (0x03, "03 普通+网管")):
            ct.addItem(v, k)
        svc_form.addRow("控制:", cc)
        svc_form.addRow("通信类型:", ct)
        return lambda: encode_28(cc.currentData(), ct.currentData())

    def _f_85():
        c = QComboBox()
        c.addItem("01 开启 DTC 记录", 0x01)
        c.addItem("02 关闭 DTC 记录", 0x02)
        svc_form.addRow("子功能:", c)
        return lambda: encode_85(c.currentData())

    def _f_22():
        e = _hex_edit("DID hex，如 F190 或 F1 90", "F190")
        svc_form.addRow("DID:", e)
        return lambda: encode_22(_parse_did(e.text()))

    def _f_2e():
        d = _hex_edit("DID hex", "F187")
        v = _hex_edit("数据 hex（如 53 49 4E）")
        svc_form.addRow("DID:", d)
        svc_form.addRow("数据:", v)
        return lambda: encode_2e(_parse_did(d.text()), _parse_hex(v.text()))

    def _f_2f():
        d = _hex_edit("DID hex", "0B7A")
        c = QComboBox()
        for k, v in ((0x01, "01 交回控制权"), (0x03, "03 恢复默认"), (0x04, "04 冻结当前状态"),
                     (0x00, "0X 短期调节（需数据）")):
            c.addItem(v, k)
        v = _hex_edit("控制数据 hex（短期调节用）")
        svc_form.addRow("DID:", d)
        svc_form.addRow("控制:", c)
        svc_form.addRow("数据:", v)
        def make():
            data = _parse_hex(v.text()) if v.text().strip() else b""
            return encode_2f(_parse_did(d.text()), c.currentData(), data)
        return make

    def _f_14():
        e = _hex_edit("DTC 组 3 字节（FFFFFF=全部）", "FFFFFF")
        svc_form.addRow("DTC 组:", e)
        def make():
            return bytes([0x14]) + _parse_n(e.text(), 3, "DTC 组")
        return make

    def _f_19():
        c = QComboBox()
        for k, v in ((0x01, "01 按掩码报 DTC 数量"), (0x02, "02 按掩码报 DTC 列表"),
                     (0x04, "04 报快照记录（按 DTC 号）"), (0x0A, "0A 报支持的 DTC")):
            c.addItem(v, k)
        m = QSpinBox()
        m.setRange(0, 0xFF)
        m.setDisplayIntegerBase(16)
        m.setPrefix("0x")
        m.setValue(0xFF)
        e = _hex_edit("DTC 号 3 字节（子功能 04 用）", "FFFFFF")
        svc_form.addRow("子功能:", c)
        svc_form.addRow("状态掩码:", m)
        svc_form.addRow("DTC 号:", e)
        def make():
            sub = c.currentData()
            if sub == 0x04:
                return bytes([0x19, 0x04]) + _parse_n(e.text(), 3, "DTC 号")
            return encode_19(sub, m.value())
        return make

    def _f_27():
        c = QComboBox()
        for lv in (0x01, 0x03, 0x05, 0x07, 0x09, 0x0B):
            c.addItem(f"{lv:02X} 请求种子 level{(lv + 1) // 2}", lv)
            c.addItem(f"{lv + 1:02X} 发送密钥 level{(lv + 1) // 2}", lv + 1)
        v = _hex_edit("密钥 hex（发送密钥子功能用）")
        svc_form.addRow("子功能:", c)
        svc_form.addRow("数据:", v)
        def make():
            req = encode_27(c.currentData())
            if v.text().strip():
                req += _parse_hex(v.text())
            return req
        return make

    def _f_31():
        c = QComboBox()
        for k, v in ((0x01, "01 启动例程"), (0x02, "02 停止例程"), (0x03, "03 请求结果")):
            c.addItem(v, k)
        r = _hex_edit("例程 ID hex", "FF01")
        svc_form.addRow("子功能:", c)
        svc_form.addRow("例程 ID:", r)
        return lambda: encode_31(c.currentData(), _parse_did(r.text()))

    def _f_34():
        f = QComboBox()
        f.addItem("0x44 地址 4B + 长度 4B", (4, 4))
        f.addItem("0x22 地址 2B + 长度 2B", (2, 2))
        a = _hex_edit("起始地址 hex（按格式字节数），如 08040000", "08040000")
        s = QSpinBox()
        s.setRange(1, 0x7FFFFFFF)
        s.setValue(0x10000)
        svc_form.addRow("格式:", f)
        svc_form.addRow("起始地址:", a)
        svc_form.addRow("字节数:", s)
        def make():
            al, sl = f.currentData()
            addr = int.from_bytes(_parse_n(a.text(), al, "地址"), "big")
            return encode_34(addr, s.value(), al, sl)
        return make

    def _f_36():
        c = QSpinBox()
        c.setRange(0, 0xFF)
        c.setValue(1)
        d = _hex_edit("块数据 hex")
        svc_form.addRow("块序号:", c)
        svc_form.addRow("数据:", d)
        return lambda: encode_36(c.value(), _parse_hex(d.text()))

    def _f_37():
        svc_form.addRow("参数:", QLabel("无参数 — RequestTransferExit"))
        return encode_37

    def _f_raw():
        e = _hex_edit("完整请求 hex，如 22 F1 90", "22 F1 90")
        svc_form.addRow("请求:", e)
        return lambda: encode_raw(0, _parse_hex(e.text()))

    FORMS = {0x10: _f_10, 0x11: _f_11, 0x3E: _f_3e, 0x28: _f_28, 0x85: _f_85,
             0x22: _f_22, 0x2E: _f_2e, 0x2F: _f_2f, 0x14: _f_14, 0x19: _f_19,
             0x27: _f_27, 0x31: _f_31, 0x34: _f_34, 0x36: _f_36, 0x37: _f_37,
             0x00: _f_raw}
    NAMES = {sid: name for _, items in SERVICE_TREE for sid, name in items}
    _svc_expect = [True]

    def _on_service_selected(item):
        sid = item.data(0, Qt.ItemDataRole.UserRole)
        if sid is None:
            return
        _clear_form()
        svc_title.setText(NAMES.get(sid, str(sid)))
        _svc_expect[0] = True
        _svc_maker[0] = FORMS[sid]()

    svc_tree.currentItemChanged.connect(lambda cur, _p: _on_service_selected(cur))

    def _on_svc_send():
        if not _svc_maker[0]:
            return
        try:
            pdu = _svc_maker[0]()
        except ValueError as e:
            _log_row("ERR", "-", b"", f"参数错误: {e}")
            return
        except Exception as e:
            _log_row("ERR", "-", b"", f"参数解析失败: {e}")
            return
        if not pdu:
            _log_row("ERR", "-", b"", "请求为空")
            return

        def cb(ok, resp, note):
            svc_resp_label.setText(note if note else ("已发送" if ok else "失败"))
            if ok and resp:
                svc_resp_label.setText((note or "") +
                                       ("\n" + " ".join(f"{b:02X}" for b in resp)))
        uds_send(pdu, _wrap_done(cb), expect_response=_svc_expect[0], tag=NAMES.get(pdu[0], ""))

    svc_send_btn.clicked.connect(_on_svc_send)
    tabs.addTab(svc_tab, "诊断服务")

    # ================================================================
    #  Tab 2: DID 面板
    # ================================================================
    did_tab = QWidget()
    did_v = QVBoxLayout(did_tab)

    did_table = QTableWidget(0, 6)
    did_table.setHorizontalHeaderLabels(["DID", "名称", "类型", "原始值", "解析值", "周期(ms)"])
    did_table.verticalHeader().setVisible(False)
    did_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    did_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    did_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    did_v.addWidget(did_table, 1)

    did_btn_row = QHBoxLayout()
    did_add = QPushButton("添加")
    did_del = QPushButton("删除")
    did_read_all = QPushButton("全部读")
    did_poll_check = QCheckBox("周期轮询")
    did_write_edit = QLineEdit()
    did_write_edit.setPlaceholderText("写值（选中行；按类型：文本/十进制/hex）")
    did_write_btn = QPushButton("写选中")
    for w in (did_add, did_del, did_read_all, did_poll_check):
        did_btn_row.addWidget(w)
    did_btn_row.addStretch()
    did_btn_row.addWidget(did_write_edit, 2)
    did_btn_row.addWidget(did_write_btn)
    did_v.addLayout(did_btn_row)

    _did_rows = []          # [{"did":int,"name":str,"type":str,"period":int,"value":bytes|None}]

    def _load_dids():
        merged = {}
        merged.update(BUILTIN_DIDS)
        try:
            with open(DIDS_FILE, encoding="utf-8-sig") as f:
                data = json.load(f)
            if isinstance(data, dict):
                for h in data.get("disabled", []):
                    merged.pop(h, None)
                merged.update(data.get("custom", {}))
        except (OSError, json.JSONDecodeError):
            pass
        return merged

    def _save_dids():
        custom = {}
        disabled = []
        kept = {f"{r['did']:04X}" for r in _did_rows}
        for did_hex in BUILTIN_DIDS:
            if did_hex not in kept:
                disabled.append(did_hex)
        for r in _did_rows:
            did_hex = f"{r['did']:04X}"
            if did_hex not in BUILTIN_DIDS:
                custom[did_hex] = {"name": r["name"], "type": r["type"]}
        try:
            with open(DIDS_FILE, "w", encoding="utf-8") as f:
                json.dump({"custom": custom, "disabled": disabled}, f,
                          ensure_ascii=False, indent=2)
        except OSError as e:
            _log_row("ERR", "-", b"", f"dids.json 保存失败: {e}")

    def _refresh_did_table():
        did_table.setRowCount(0)
        for r in _did_rows:
            raw, val = _decode_did(r["type"], r["value"]) if r["value"] else ("-", "-")
            row = did_table.rowCount()
            did_table.insertRow(row)
            for col, text in enumerate((f"0x{r['did']:04X}", r["name"], r["type"],
                                        raw, val, str(r["period"] or "-"))):
                did_table.setItem(row, col, QTableWidgetItem(text))

    def _init_did_rows():
        _did_rows.clear()
        for did_hex, info in _load_dids().items():
            try:
                _did_rows.append({"did": int(did_hex, 16), "name": info.get("name", did_hex),
                                  "type": info.get("type", "hex"), "period": 0, "value": None})
            except ValueError:
                continue
        _did_rows.sort(key=lambda r: r["did"])
        _refresh_did_table()

    _init_did_rows()

    def _did_add():
        from PyQt6.QtWidgets import QDialog, QFormLayout, QDialogButtonBox
        dlg = QDialog(win)
        dlg.setWindowTitle("添加 DID")
        form = QFormLayout(dlg)
        de = QLineEdit()
        de.setPlaceholderText("4 位 hex，如 F1A0")
        ne = QLineEdit()
        ne.setPlaceholderText("名称")
        tc = QComboBox()
        tc.addItems(DID_TYPES)
        form.addRow("DID:", de)
        form.addRow("名称:", ne)
        form.addRow("类型:", tc)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        form.addRow(bb)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        if dlg.exec() == QDialog.DialogCode.Accepted:
            try:
                did = int(de.text().strip(), 16)
                if not 0 <= did <= 0xFFFF:
                    raise ValueError
            except ValueError:
                _log_row("ERR", "-", b"", "DID 需为 4 位 hex")
                return
            if any(r["did"] == did for r in _did_rows):
                _log_row("ERR", "-", b"", "DID 已存在")
                return
            _did_rows.append({"did": did, "name": ne.text().strip() or f"0x{did:04X}",
                              "type": tc.currentText(), "period": 0, "value": None})
            _did_rows.sort(key=lambda r: r["did"])
            _save_dids()
            _refresh_did_table()

    def _did_del():
        sel = did_table.currentRow()
        if 0 <= sel < len(_did_rows):
            _did_rows.pop(sel)
            _save_dids()
            _refresh_did_table()

    def _did_read(did):
        def cb(ok, resp, note):
            if ok and resp and resp[:1] == b"\x62" and len(resp) >= 3:
                rid = int.from_bytes(resp[1:3], "big")
                for i, r in enumerate(_did_rows):
                    if r["did"] == rid:
                        r["value"] = bytes(resp[3:])
                        break
                _refresh_did_table()
        uds_send(encode_22(did), _wrap_done(cb), tag=f"读 DID 0x{did:04X}")

    def _did_read_all():
        """串行读全部（客户端单事务：忙则等 100ms 再试）"""
        queue = [r["did"] for r in _did_rows]

        def next_one():
            if not queue or not _isotp._alive:
                return
            if _client.busy or _flashing[0]:
                QTimer.singleShot(100, next_one)
                return
            _did_read(queue.pop(0))
            QTimer.singleShot(60, next_one)

        next_one()

    did_read_all.clicked.connect(_did_read_all)

    def _did_write():
        sel = did_table.currentRow()
        if not (0 <= sel < len(_did_rows)) or not did_write_edit.text().strip():
            return
        r = _did_rows[sel]
        try:
            data = _encode_did_value(r["type"], did_write_edit.text().strip())
        except (ValueError, UnicodeEncodeError) as e:
            _log_row("ERR", "-", b"", f"写值非法: {e}")
            return
        def cb(ok, resp, note):
            if ok:
                sin.output.append(f"DID 0x{r['did']:04X} 写入成功")
        uds_send(encode_2e(r["did"], data), _wrap_done(cb), tag=f"写 DID 0x{r['did']:04X}")

    did_add.clicked.connect(_did_add)
    did_del.clicked.connect(_did_del)
    did_write_btn.clicked.connect(_did_write)

    def _on_did_double(row, col):
        """双击周期列改轮询周期"""
        if col != 5 or not (0 <= row < len(_did_rows)):
            return
        r = _did_rows[row]
        from PyQt6.QtWidgets import QInputDialog
        text, okk = QInputDialog.getInt(win, "轮询周期", f"DID 0x{r['did']:04X} 周期 ms (0=关):",
                                        r["period"] or 1000, 0, 60000, 100)
        if okk:
            r["period"] = text
            _refresh_did_table()

    did_table.cellDoubleClicked.connect(_on_did_double)

    _did_last_read = {}

    def _did_poll_tick():
        if not did_poll_check.isChecked() or _flashing[0] or _client.busy:
            return
        now = time.time()
        for r in _did_rows:
            if r["period"] <= 0:
                continue
            last = _did_last_read.get(r["did"], 0)
            if now - last >= r["period"] / 1000.0 and not _client.busy:
                _did_last_read[r["did"]] = now
                _did_read(r["did"])
                if _client.busy:      # 客户端单事务：每 tick 最多一个
                    break

    did_poll_timer = QTimer(win)
    did_poll_timer.setInterval(200)
    did_poll_timer.timeout.connect(_did_poll_tick)
    did_poll_timer.start(200)

    tabs.addTab(did_tab, "DID 面板")

    # ================================================================
    #  Tab 3: DTC 管理
    # ================================================================
    dtc_tab = QWidget()
    dtc_v = QVBoxLayout(dtc_tab)

    mask_box = QGroupBox("状态掩码（reportDTCByStatusMask 19 02 的掩码位）")
    mask_row = QHBoxLayout(mask_box)
    mask_checks = {}
    for bit, name in DTC_STATUS_BITS.items():
        short = name.split()[0]
        cb = QCheckBox(short)
        cb.setToolTip(name)
        if bit in (0x01, 0x08):
            cb.setChecked(True)
        mask_row.addWidget(cb)
        mask_checks[bit] = cb
    mask_row.addStretch()

    dtc_btn_row = QHBoxLayout()
    dtc_read_btn = QPushButton("读 DTC 列表 (19 02)")
    dtc_cnt_btn = QPushButton("读 DTC 数量 (19 01)")
    dtc_clear_btn = QPushButton("清除 DTC (14 FF FF FF)")
    dtc_btn_row.addWidget(dtc_read_btn)
    dtc_btn_row.addWidget(dtc_cnt_btn)
    dtc_btn_row.addStretch()
    dtc_btn_row.addWidget(dtc_clear_btn)

    dtc_table = QTableWidget(0, 4)
    dtc_table.setHorizontalHeaderLabels(["DTC", "原始字节", "状态字节", "状态位解码"])
    dtc_table.verticalHeader().setVisible(False)
    dtc_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    dtc_table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)

    dtc_v.addWidget(mask_box)
    dtc_v.addLayout(dtc_btn_row)
    dtc_v.addWidget(dtc_table, 1)

    def _mask_value():
        v = 0
        for bit, cb in mask_checks.items():
            if cb.isChecked():
                v |= bit
        return v

    def _on_dtc_read():
        mask = _mask_value()

        def cb(ok, resp, note):
            if not (ok and resp and resp[:2] == b"\x59\x02"):
                return
            dtc_table.setRowCount(0)
            body = resp[3:]          # 跳过 59 02 statusAvailabilityMask
            count = 0
            for i in range(0, len(body) - 2, 3):
                d, st = body[i:i + 2], body[i + 2]
                row = dtc_table.rowCount()
                dtc_table.insertRow(row)
                raw = " ".join(f"{b:02X}" for b in body[i:i + 3])
                for col, text in enumerate((dtc_to_text(body[i:i + 2]), raw,
                                            f"0x{st:02X}", dtc_status_text(st))):
                    item = QTableWidgetItem(text)
                    if col == 0 and (st & 0x01):
                        item.setForeground(QColor("#C62828"))
                    dtc_table.setItem(row, col, item)
                count += 1
            _log_row("RX", _isotp.rx_id, resp, f"19 02 解析出 {count} 条 DTC")

        uds_send(encode_19(0x02, mask), _wrap_done(cb), tag=f"读 DTC 掩码 0x{mask:02X}")

    def _on_dtc_count():
        def cb(ok, resp, note):
            if ok and resp and resp[:2] == b"\x59\x01" and len(resp) >= 5:
                n = int.from_bytes(resp[3:5], "big")
                QMessageBox.information(win, "DTC 数量",
                                        f"状态可用性 0x{resp[2]:02X}\n符合条件的 DTC 数: {n}")
        uds_send(encode_19(0x01, _mask_value()), _wrap_done(cb), tag="读 DTC 数量")

    def _on_dtc_clear():
        def cb(ok, resp, note):
            if ok:
                dtc_table.setRowCount(0)
                _log_row("RX", _isotp.rx_id, b"", "清除 DTC 成功（54 响应）")
        uds_send(encode_14(0xFFFFFF), _wrap_done(cb), tag="清除 DTC")

    dtc_read_btn.clicked.connect(_on_dtc_read)
    dtc_cnt_btn.clicked.connect(_on_dtc_count)
    dtc_clear_btn.clicked.connect(_on_dtc_clear)
    tabs.addTab(dtc_tab, "DTC 管理")

    # ================================================================
    #  Tab 4: 安全访问
    # ================================================================
    sec_tab = QWidget()
    sec_form = QFormLayout(sec_tab)

    sec_level = QComboBox()
    for lv in (0x01, 0x03, 0x05, 0x07, 0x09, 0x0B):
        sec_level.addItem(f"level {(lv + 1) // 2}（种子 {lv:02X} / 密钥 {lv + 1:02X}）", lv)

    sec_algo = QComboBox()
    sec_algo.addItem("演示算法（seed XOR 0xA5...）", "demo")
    sec_algo.addItem("Python 表达式", "expr")
    sec_algo.addItem("算法文件（calculate_key）", "file")

    sec_expr = QLineEdit("seed ^ 0x11223344")
    sec_expr.setToolTip("可用变量: seed（大端整数）/ seed_bytes；返回 int 或 bytes")
    sec_file_btn = QPushButton("选择算法文件...")
    sec_file_label = QLabel("未选择")

    sec_seed_label = QLabel("—")
    sec_key_label = QLabel("—")
    sec_result_label = QLabel("—")
    for l in (sec_seed_label, sec_key_label, sec_result_label):
        l.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        l.setStyleSheet("font-family: Consolas, monospace;")

    sec_seed_btn = QPushButton("请求种子 (27 odd)")
    sec_key_btn = QPushButton("发送密钥 (27 even)")
    sec_key_btn.setEnabled(False)

    sec_form.addRow("安全级别:", sec_level)
    sec_form.addRow("密钥算法:", sec_algo)
    sec_form.addRow("表达式:", sec_expr)
    sec_form.addRow("算法文件:", sec_file_btn)
    sec_form.addRow("当前文件:", sec_file_label)
    sec_form.addRow("种子 Seed:", sec_seed_label)
    sec_form.addRow("计算密钥:", sec_key_label)
    sec_form.addRow("执行结果:", sec_result_label)
    sec_form.addRow(sec_seed_btn)
    sec_form.addRow(sec_key_btn)

    _sec_state = {"seed": None, "file": None}

    def _on_pick_algo_file():
        path, _ = QFileDialog.getOpenFileName(win, "选择密钥算法 Python 文件", "",
                                              "Python (*.py)")
        if path:
            _sec_state["file"] = path
            sec_file_label.setText(path)

    sec_file_btn.clicked.connect(_on_pick_algo_file)

    def _calc_key(seed):
        mode = sec_algo.currentData()
        if mode == "demo":
            return calc_key_demo(seed, len(seed))
        if mode == "expr":
            key = calc_key_expr(sec_expr.text().strip(), seed)
        else:
            if not _sec_state["file"]:
                raise ValueError("未选择算法文件")
            key = calc_key_file(_sec_state["file"], seed)
        return _fit_len(key, len(seed))

    _key_calc_ref = [None]
    _key_calc_ref[0] = _calc_key       # 刷写预检共用本 Tab 的算法配置

    def _on_request_seed():
        lv = sec_level.currentData()
        _sec_state["seed"] = None
        sec_key_btn.setEnabled(False)
        sec_seed_label.setText("请求中...")
        sec_result_label.setText("—")

        def cb(ok, resp, note):
            if ok and resp and resp[0] == 0x67 and len(resp) > 2:
                seed = bytes(resp[2:])
                _sec_state["seed"] = seed
                sec_seed_label.setText(seed.hex().upper())
                try:
                    key = _calc_key(seed)
                    sec_key_label.setText(key.hex().upper())
                    sec_key_btn.setEnabled(True)
                    sec_result_label.setText("密钥已计算，可发送")
                except Exception as e:
                    sec_key_label.setText("—")
                    sec_result_label.setText(f"算法失败: {e}")
            else:
                sec_seed_label.setText("失败: " + (note or "无响应"))
                sec_result_label.setText("失败")

        uds_send(encode_27(lv), _wrap_done(cb), tag=f"请求种子 27 {lv:02X}")

    def _on_send_key():
        seed = _sec_state["seed"]
        if not seed:
            return
        try:
            key = _calc_key(seed)
        except Exception as e:
            sec_result_label.setText(f"算法失败: {e}")
            return
        lv = sec_level.currentData() + 1

        def cb(ok, resp, note):
            if ok and resp and resp[0] == 0x67:
                sec_result_label.setText("密钥校验通过（安全解锁）")
                sin.output.append("UDS 安全访问解锁成功")
            else:
                sec_result_label.setText("失败: " + (note or "无响应"))

        uds_send(encode_27(lv) + key, _wrap_done(cb), tag=f"发送密钥 27 {lv:02X}")

    sec_seed_btn.clicked.connect(_on_request_seed)
    sec_key_btn.clicked.connect(_on_send_key)
    tabs.addTab(sec_tab, "安全访问")

    # ================================================================
    #  Tab 5: 刷写助手
    # ================================================================
    fl_tab = QWidget()
    fl_v = QVBoxLayout(fl_tab)

    fl_form = QFormLayout()
    fl_file_edit = QLineEdit()
    fl_file_edit.setPlaceholderText("固件二进制文件（.bin/.hex 原样按字节传输）")
    fl_file_btn = QPushButton("浏览...")
    fl_file_row = QHBoxLayout()
    fl_file_row.addWidget(fl_file_edit, 1)
    fl_file_row.addWidget(fl_file_btn)

    fl_addr = _hex_edit("刷写起始地址 hex（4 字节），如 08040000", "08040000")
    fl_pre_check = QCheckBox("预检序列（10 02 → 27 解锁 → 85 02 → 28 03 03）")
    fl_pre_check.setChecked(True)
    fl_sec_lv = QComboBox()
    for lv in (0x01, 0x03, 0x05):
        fl_sec_lv.addItem(f"level {(lv + 1) // 2}", lv)
    fl_block_edit = QLineEdit()
    fl_block_edit.setPlaceholderText("留空=用 34 响应的单块最大长度")

    fl_form.addRow("固件文件:", fl_file_row)
    fl_form.addRow("起始地址:", fl_addr)
    fl_form.addRow("预检:", fl_pre_check)
    fl_form.addRow("安全级别:", fl_sec_lv)
    fl_form.addRow("单块大小:", fl_block_edit)

    fl_progress = QProgressBar()
    fl_progress.setRange(0, 1000)
    fl_progress.setValue(0)
    fl_status = QLabel("空闲 — 选择文件后点击开始")
    fl_status.setStyleSheet("color:#555;")

    fl_btn_row = QHBoxLayout()
    fl_start_btn = QPushButton("开始刷写")
    fl_start_btn.setMinimumHeight(34)
    fl_stop_btn = QPushButton("中止")
    fl_stop_btn.setEnabled(False)
    fl_btn_row.addWidget(fl_start_btn)
    fl_btn_row.addWidget(fl_stop_btn)
    fl_btn_row.addStretch()

    fl_v.addLayout(fl_form)
    fl_v.addLayout(fl_btn_row)
    fl_v.addWidget(fl_progress)
    fl_v.addWidget(fl_status)
    fl_v.addStretch()

    def _on_pick_file():
        path, _ = QFileDialog.getOpenFileName(win, "选择固件文件", "", "所有文件 (*.*)")
        if path:
            fl_file_edit.setText(path)
            try:
                size = os.path.getsize(path)
                fl_status.setText(f"已选文件 {size} 字节（0x{size:X}）")
            except OSError:
                pass

    fl_file_btn.clicked.connect(_on_pick_file)

    _flash = {"state": "idle", "data": b"", "addr": 0, "maxblk": 0, "pos": 0,
              "block": 0, "size": 0, "t0": 0}

    def _fl_set(status, pct=None):
        fl_status.setText(status)
        if pct is not None:
            fl_progress.setValue(int(pct * 1000))

    def _fl_fail(msg):
        _flash["state"] = "idle"
        _flashing[0] = False
        fl_start_btn.setEnabled(True)
        fl_stop_btn.setEnabled(False)
        _fl_set(f"失败: {msg}", None)
        _log_row("ERR", "-", b"", f"刷写失败: {msg}")

    def _fl_done():
        _flash["state"] = "idle"
        _flashing[0] = False
        fl_start_btn.setEnabled(True)
        fl_stop_btn.setEnabled(False)
        cost = time.time() - _flash["t0"]
        _fl_set(f"刷写完成（{cost:.1f}s，{_flash['size']} 字节）", 1.0)
        _set_session(SESSIONS[0x02])
        sin.output.append(f"UDS 刷写完成: {fl_file_edit.text()}")

    def _fl_send(req, state, tag):
        def cb(ok, resp, note):
            if _flash["state"] == "idle":
                return
            if not ok:
                _fl_fail(f"{tag}: {note}")
                return
            _fl_step(state, resp)
        uds_send(req, _wrap_done(cb), tag=f"[刷写] {tag}")

    def _fl_step(state, resp):
        st = _flash["state"] = state
        if st == "session":
            _fl_set("预检: 切换编程会话...")
            _fl_send(encode_10(0x02), "sec", "10 02 编程会话")
        elif st == "sec":
            if _flash.get("precheck"):
                lv = fl_sec_lv.currentData()
                _fl_set("预检: 请求安全种子...")
                def seed_cb(ok, r, note):
                    if not ok:
                        _fl_fail(f"27 种子: {note}")
                        return
                    if r and r[0] == 0x67 and len(r) > 2:
                        seed = bytes(r[2:])
                        try:
                            key = _key_calc_ref[0](seed)
                        except Exception as e:
                            _fl_fail(f"密钥算法: {e}")
                            return
                        _fl_send(encode_27(lv + 1) + key, "dtc_off", f"27 {lv + 1:02X} 密钥")
                    else:
                        _fl_fail("27 未返回种子")
                uds_send(encode_27(lv), _wrap_done(seed_cb), tag="[刷写] 27 种子")
            else:
                _fl_step("dtc_off", None)
        elif st == "dtc_off":
            if _flash.get("precheck"):
                _fl_set("预检: 关闭 DTC 记录...")
                _fl_send(encode_85(0x02), "comm_off", "85 02")
            else:
                _fl_step("comm_off", None)
        elif st == "comm_off":
            if _flash.get("precheck"):
                _fl_set("预检: 关闭通信...")
                _fl_send(encode_28(0x03, 0x03), "reqdl", "28 03 03")
            else:
                _fl_step("reqdl", None)
        elif st == "reqdl":
            _fl_set("请求下载 34...")
            _fl_send(encode_34(_flash["addr"], len(_flash["data"])),
                     "transfer", "34 请求下载")
        elif st == "transfer":
            if not (resp and resp[0] == 0x74 and len(resp) >= 3):
                _fl_fail("34 响应格式错误")
                return
            n = resp[1] >> 4                          # 高半字节 = maxNumberOfBlockLength 字节数
            maxblk = int.from_bytes(resp[2:2 + n], "big") if n else 0
            if fl_block_edit.text().strip():
                try:
                    maxblk = int(fl_block_edit.text(), 0)
                except ValueError:
                    _fl_fail("单块大小非法")
                    return
            if maxblk <= 2:
                _fl_fail(f"单块最大长度过小: {maxblk}")
                return
            _flash["maxblk"] = maxblk
            _flash["pos"] = 0
            _flash["block"] = 1
            _flash["state"] = "data"
            _fl_set(f"传输中（单块 {maxblk}B）...", 0.0)
            _fl_next_block()
        elif st == "check":
            _fl_set("执行完整性校验 31 01 FF01...")
            _fl_send(encode_31(0x01, 0xFF01), "exit_ok", "31 01 FF01")
        elif st == "exit_ok":
            _fl_send(encode_37(), "done", "37 传输退出")
        elif st == "done":
            _fl_done()

    def _fl_next_block():
        if _flash["state"] != "data":
            return
        pos, data = _flash["pos"], _flash["data"]
        chunk = data[pos:pos + _flash["maxblk"] - 2]
        if not chunk:
            _flash["state"] = "check"
            _fl_step("check", None)
            return
        counter = _flash["block"]
        _flash["block"] = (counter + 1) & 0xFF

        def cb(ok, resp, note):
            if _flash["state"] != "data":
                return
            if not ok:
                _fl_fail(f"36 块 {counter}: {note}")
                return
            _flash["pos"] = pos + len(chunk)     # 推进偏移，否则永远重发第一块
            total = len(data)
            _fl_set(f"传输中 {pos + len(chunk)}/{total} 字节，块 {counter}", (pos + len(chunk)) / total)
            _fl_next_block()

        uds_send(encode_36(counter, chunk), _wrap_done(cb),
                 tag=f"[刷写] 36 块 {counter} ({len(chunk)}B)")

    def _on_flash_start():
        if _flash["state"] != "idle":
            return
        path = fl_file_edit.text().strip()
        if not path or not os.path.isfile(path):
            QMessageBox.warning(win, "刷写", "请先选择有效的固件文件")
            return
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            QMessageBox.warning(win, "刷写", f"读取失败: {e}")
            return
        if not data:
            QMessageBox.warning(win, "刷写", "固件文件为空")
            return
        try:
            addr = int.from_bytes(_parse_n(fl_addr.text(), 4, "起始地址"), "big")
        except ValueError as e:
            QMessageBox.warning(win, "刷写", str(e))
            return
        _flash.update({"data": data, "addr": addr, "size": len(data),
                       "pos": 0, "block": 1, "maxblk": 0,
                       "precheck": fl_pre_check.isChecked(), "t0": time.time()})
        _flashing[0] = True
        fl_start_btn.setEnabled(False)
        fl_stop_btn.setEnabled(True)
        fl_progress.setValue(0)
        _log_row("TX", _isotp.tx_id, b"", f"开始刷写: {path}（{len(data)}B @ 0x{addr:X}）")
        _fl_step("session", None)

    def _on_flash_stop():
        if _flash["state"] != "idle":
            _client.cancel()
            _flash["state"] = "idle"
            _flashing[0] = False
            fl_start_btn.setEnabled(True)
            fl_stop_btn.setEnabled(False)
            _fl_set("已中止")

    fl_start_btn.clicked.connect(_on_flash_start)
    fl_stop_btn.clicked.connect(_on_flash_stop)
    tabs.addTab(fl_tab, "刷写助手")

    # ================================================================
    #  帧接入 + TesterPresent
    # ================================================================
    def on_frame(frame):
        if frame.data:
            _isotp.on_frame(frame.id, bytes(frame.data))

    context.on_frame(on_frame)

    tp_timer = QTimer(win)
    tp_timer.setInterval(2000)

    def on_tp_tick():
        if tp_check.isChecked() and _isotp._alive and not _flashing[0]:
            # 抑制响应的 3E80：直接走传输层，不占用客户端事务
            _isotp.tx_id = tx_spin.value()
            _isotp.send(encode_3e(0x80))
            _log_row("TX", tx_spin.value(), encode_3e(0x80), "TesterPresent 3E 80（心跳）")

    tp_timer.timeout.connect(on_tp_tick)
    tp_timer.start(2000)

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("udsDiagnostic.open", on_open_cmd, "协议: UDS 诊断")

    _log_row("RX", "-", b"", "UDS 诊断（G10）就绪 — 物理 0x7E0 / 功能 0x7DF / 响应 0x7E8，"
             "ISO-TP 完整流控 + P2 超时管理")
    win.show()
    sin.output.append("UDS 诊断插件已加载（G10 强化版）")


def deactivate():
    if _isotp:
        _isotp.shutdown()
    if _client:
        _client.shutdown()
    sin.output.append("UDS 诊断插件已停用")
