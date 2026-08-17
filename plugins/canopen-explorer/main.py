"""canopen-explorer 插件 — CANopen 工具（G9，替代原 C++ CanOpenView）

功能（CiA 301）：
- NMT 主站命令：Start / Stop / Pre-Operational / Reset / Reset Communication
- SDO 客户端：expedited 读/写（快速传输，1-4 字节），Index/Subindex/节点可配
- Heartbeat 监控：节点状态表（Boot-up/Stopped/Operational/Pre-Op），超时提示
- Emergency (0x80+ID) 解析：错误代码 + 错误寄存器
- 事务日志

依赖: pip install PyQt6
"""

import time

import sin


NMT_COMMANDS = [
    (0x01, "Start Remote Node", "进入运行态"),
    (0x02, "Stop Remote Node", "停止"),
    (0x80, "Enter Pre-Operational", "进入预运行"),
    (0x81, "Reset Node", "节点复位"),
    (0x82, "Reset Communication", "通信复位"),
]

HEARTBEAT_STATES = {
    0x00: "Boot-up",
    0x04: "Stopped",
    0x05: "Operational",
    0x7F: "Pre-Operational",
}

EMCY_HINTS = {
    0x0000: "错误复位 / 无错误",
    0x1000: "通用",
    0x2000: "电流",
    0x3000: "电压",
    0x4000: "温度",
    0x5000: "设备硬件",
    0x6000: "软件",
    0x8000: "附加模块",
    0x9000: "监控",
    0xF000: "厂商自定义",
}

COMMON_OD = [
    (0x1000, "设备类型 (u32)"),
    (0x1001, "错误寄存器 (u8)"),
    (0x1005, "COB-ID SYNC (u32)"),
    (0x1017, "心跳生产时间 ms (u16)"),
    (0x1018, "Identity 对象"),
    (0x1200, "SDO 服务器参数"),
    (0x1400, "RPDO 通信参数"),
    (0x1600, "RPDO 映射"),
    (0x1800, "TPDO 通信参数"),
    (0x1A00, "TPDO 映射"),
]

# SDO 接收状态（分段传输 v1 不实现，expedited 即可覆盖常用对象）
_hb_nodes = {}       # node_id → {"state": str, "ts": float, "label": widget}


def _log(log_view, tag, msg, color="#333"):
    ts = time.strftime("%H:%M:%S")
    log_view.append(f"[{ts}] <b>{tag}</b> <span style='color:{color}'>{msg}</span>")


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QGroupBox,
            QLabel, QPushButton, QComboBox, QLineEdit, QTextEdit,
            QSpinBox, QTabWidget, QTreeWidget, QTreeWidgetItem,
            QHeaderView
        )
        from PyQt6.QtCore import Qt
    except ImportError:
        sin.output.append("CANopen 插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CANopen (CiA 301)")
    win.resize(860, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    layout.addWidget(log_view, 1)

    # ============================================================
    #  Tab 1: NMT 节点控制
    # ============================================================
    nmt_tab = QWidget()
    nmt_form = QFormLayout(nmt_tab)

    nmt_node_spin = QSpinBox()
    nmt_node_spin.setRange(0, 127)
    nmt_node_spin.setValue(1)
    nmt_form.addRow("节点 ID (0=广播):", nmt_node_spin)

    nmt_btn_row = QHBoxLayout()
    nmt_buttons = []
    for cmd, name, desc in NMT_COMMANDS:
        btn = QPushButton(name)
        btn.setToolTip(desc)
        btn.setMinimumHeight(30)
        nmt_btn_row.addWidget(btn)
        nmt_buttons.append((btn, cmd))
    nmt_form.addRow(nmt_btn_row)

    def send_nmt(cmd):
        node = nmt_node_spin.value()
        cob = 0x000
        data = bytes([cmd, node])
        sin.frames.send(cob, data)
        _log(log_view, "NMT TX", f"0x{cob:X} [{data[0]:02X} {data[1]:02X}] → "
             f"{'广播' if node == 0 else f'节点 {node}'}: "
             f"{dict((c, n) for c, n, _ in NMT_COMMANDS).get(cmd, '')}")

    for btn, cmd in nmt_buttons:
        btn.clicked.connect(lambda _c=cmd: send_nmt(_c))

    tabs.addTab(nmt_tab, "NMT 节点控制")

    # ============================================================
    #  Tab 2: SDO 读写（expedited）
    # ============================================================
    sdo_tab = QWidget()
    sdo_form = QFormLayout(sdo_tab)

    sdo_node_spin = QSpinBox()
    sdo_node_spin.setRange(1, 127)
    sdo_node_spin.setValue(1)
    sdo_form.addRow("节点 ID:", sdo_node_spin)

    od_row = QHBoxLayout()
    od_combo = QComboBox()
    for idx, name in COMMON_OD:
        od_combo.addItem(f"0x{idx:04X} {name}", idx)
    od_row.addWidget(od_combo, 1)
    sdo_form.addRow("常用对象:", od_row)

    idx_spin = QSpinBox()
    idx_spin.setRange(0, 0xFFFF)
    idx_spin.setDisplayIntegerBase(16)
    idx_spin.setPrefix("0x")
    idx_spin.setValue(0x1000)
    sdo_form.addRow("Index:", idx_spin)

    sub_spin = QSpinBox()
    sub_spin.setRange(0, 0xFF)
    sub_spin.setValue(0)
    sdo_form.addRow("Subindex:", sub_spin)

    size_combo = QComboBox()
    for label, n in (("u8 (1 字节)", 1), ("u16 (2 字节)", 2),
                     ("u32 (4 字节)", 4)):
        size_combo.addItem(label, n)
    sdo_form.addRow("写数据大小:", size_combo)

    value_edit = QLineEdit()
    value_edit.setPlaceholderText("写操作的值（十进制或 0x 开头十六进制）")
    sdo_form.addRow("写值:", value_edit)

    sdo_btn_row = QHBoxLayout()
    sdo_read_btn = QPushButton("SDO 读")
    sdo_write_btn = QPushButton("SDO 写")
    sdo_read_btn.setMinimumHeight(30)
    sdo_write_btn.setMinimumHeight(30)
    sdo_btn_row.addWidget(sdo_read_btn)
    sdo_btn_row.addWidget(sdo_write_btn)
    sdo_btn_row.addStretch()
    sdo_form.addRow(sdo_btn_row)

    def on_od_picked(index):
        idx_spin.setValue(od_combo.itemData(index))

    def sdo_cob(direction, node):
        # SDO 请求 0x600+node，响应 0x580+node
        return (0x600 + node) if direction == "req" else (0x580 + node)

    def on_sdo_read():
        node = sdo_node_spin.value()
        idx, sub = idx_spin.value(), sub_spin.value()
        req = bytes([0x40, (idx >> 8) & 0xFF, idx & 0xFF, sub, 0, 0, 0, 0])
        sin.frames.send(sdo_cob("req", node), req)
        _log(log_view, "SDO TX", f"0x{sdo_cob('req', node):X} 读 0x{idx:04X}:{sub:02X}")

    def on_sdo_write():
        node = sdo_node_spin.value()
        idx, sub = idx_spin.value(), sub_spin.value()
        n = size_combo.currentData()
        try:
            text = value_edit.text().strip()
            val = int(text, 0) if text else 0
        except ValueError:
            _log(log_view, "SDO", f"无效写值: {text}", "#c0392b")
            return
        # expedited 写：命令字节按字节数定 (1/2/4 字节 → 0x2F/0x2B/0x23)
        cmd_byte = {1: 0x2F, 2: 0x2B, 4: 0x23}.get(n, 0x22)
        data = val.to_bytes(n, "little")
        req = bytes([cmd_byte, (idx >> 8) & 0xFF, idx & 0xFF, sub]) + data
        req = req + b"\x00" * (8 - len(req))
        sin.frames.send(sdo_cob("req", node), req)
        _log(log_view, "SDO TX", f"0x{sdo_cob('req', node):X} 写 0x{idx:04X}:{sub:02X} = {val} "
             f"(0x{val:0{2 * n}X})")

    tabs.addTab(sdo_tab, "SDO 读写")

    # ============================================================
    #  Tab 3: Heartbeat 监控
    # ============================================================
    hb_tab = QWidget()
    hb_layout = QVBoxLayout(hb_tab)
    hb_hint = QLabel("监听 0x700+节点ID 心跳；状态: Boot-up / Stopped / Operational / Pre-Operational")
    hb_hint.setStyleSheet("color: #888;")
    hb_layout.addWidget(hb_hint)

    hb_tree = QTreeWidget()
    hb_tree.setHeaderLabels(["节点", "状态", "最后心跳", "距今 (s)"])
    hb_tree.setRootIsDecorated(False)
    hb_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    hb_layout.addWidget(hb_tree)

    def refresh_hb_tree():
        now = time.time()
        hb_tree.clear()
        for node_id in sorted(_hb_nodes):
            info = _hb_nodes[node_id]
            ago = now - info["ts"]
            item = QTreeWidgetItem([str(node_id), info["state"],
                                    time.strftime("%H:%M:%S", time.localtime(info["ts"])),
                                    f"{ago:.1f}"])
        # 状态着色
            color = {"Operational": "#27ae60", "Stopped": "#c0392b",
                     "Pre-Operational": "#f39c12", "Boot-up": "#7f8c8d"}.get(info["state"], "#333")
            item.setForeground(1, _qcolor(color))
            hb_tree.addTopLevelItem(item)

    def _qcolor(name):
        from PyQt6.QtGui import QColor
        return QColor(name)

    tabs.addTab(hb_tab, "Heartbeat")

    # ============================================================
    #  Tab 4: Emergency
    # ============================================================
    emcy_tab = QWidget()
    emcy_layout = QVBoxLayout(emcy_tab)
    emcy_hint = QLabel("监听 0x80+节点ID 紧急报文（EMCY）")
    emcy_hint.setStyleSheet("color: #888;")
    emcy_layout.addWidget(emcy_hint)
    emcy_view = QTextEdit()
    emcy_view.setReadOnly(True)
    emcy_layout.addWidget(emcy_view)
    tabs.addTab(emcy_tab, "Emergency")

    # ============================================================
    #  帧接收分发
    # ============================================================
    def on_frame(frame):
        fid = frame.id
        data = bytes(frame.data) if frame.data else b""
        if not data:
            return

        # Heartbeat / Boot-up: 0x700+node，1 字节状态
        if 0x701 <= fid <= 0x77F:
            node = fid - 0x700
            state = HEARTBEAT_STATES.get(data[0], f"未知 0x{data[0]:02X}")
            _hb_nodes[node] = {"state": state, "ts": time.time()}
            return

        # EMCY: 0x80+node
        if 0x081 <= fid <= 0x0FF:
            node = fid - 0x80
            if len(data) >= 3:
                ecode = data[0] | (data[1] << 8)
                reg = data[2]
                hint = next((h for k, h in EMCY_HINTS.items() if (ecode >> 12) == (k >> 12)),
                            "未知类别")
                ts = time.strftime("%H:%M:%S")
                emcy_view.append(f"[{ts}] <b>节点 {node}</b> EMCY 0x{ecode:04X} "
                                 f"错误寄存器 0x{reg:02X} — {hint} "
                                 f"<span style='color:#c0392b'>({', '.join(f'{b:02X}' for b in data)})</span>")
            return

        # SDO 响应: 0x580+node
        if 0x581 <= fid <= 0x5FF:
            node = fid - 0x580
            if len(data) < 4:
                return
            cmd = data[0]
            idx = (data[2] << 8) | data[1]
            sub = data[3]
            if cmd == 0x4B or cmd == 0x43 or cmd == 0x4F:
                # expedited 正响应
                n = {0x4B: 2, 0x43: 4, 0x4F: 1}[cmd]
                val = int.from_bytes(data[4:4 + n], "little")
                _log(log_view, "SDO RX",
                     f"节点 {node} 读 0x{idx:04X}:{sub:02X} = {val} (0x{val:0{2 * n}X})",
                     "#27ae60")
            elif cmd == 0x60:
                _log(log_view, "SDO RX", f"节点 {node} 写 0x{idx:04X}:{sub:02X} 成功", "#27ae60")
            elif cmd == 0x80:
                abort_code = int.from_bytes(data[4:8], "little")
                _log(log_view, "SDO RX",
                     f"节点 {node} 0x{idx:04X}:{sub:02X} 中止 0x{abort_code:08X}", "#c0392b")
            return

    context.on_frame(on_frame)

    # ============================================================
    #  刷新定时器（心跳表）
    # ============================================================
    from PyQt6.QtCore import QTimer
    hb_timer = QTimer(win)
    hb_timer.setInterval(1000)
    hb_timer.timeout.connect(refresh_hb_tree)
    hb_timer.start(1000)

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("canopenExplorer.open", on_open_cmd, "协议: CANopen")

    od_combo.currentIndexChanged.connect(on_od_picked)
    sdo_read_btn.clicked.connect(on_sdo_read)
    sdo_write_btn.clicked.connect(on_sdo_write)

    _log(log_view, "就绪", "NMT/SDO/Heartbeat/EMCY 监听已启动")
    win.show()
    sin.output.append("CANopen 插件已加载")


def deactivate():
    _hb_nodes.clear()
    sin.output.append("CANopen 插件已停用")
