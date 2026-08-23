# -*- coding: utf-8 -*-
"""autosar-nm-monitor 插件 — AUTOSAR CAN 网络管理（NM）监视
功能：
- 可配置 NM ID 基址（默认 0x400，低字节 = 节点 ID），扫描 0x400-0x4FF
- NM PDU 解析：源节点 ID（byte0）/ 用户数据 / CBV 控制位向量
  （重复报文请求 / NM 协调器休眠就绪 / 主动唤醒 / PNI）
- 节点状态机推断：RepeatMessage / Normal / ReadySleep / BusSleep，
  逐节点周期统计与超时（静默）检测
- 状态时间线日志 + 节点表 + CSV 导出；纯监视不发送
依赖: pip install PyQt6
"""

import time

import sin

# 节点状态推断说明：
# - 正在发 NM 报文（CBV bit0=1 或周期很短）→ RepeatMessage
# - 正在发 NM 报文（CBV bit0=0）→ Normal（或 ReadySleep，无法从单帧区分，按最后 CBV 展示）
# - 之前发过、超时静默 → ReadySleep（若总线上仍有其他 NM）或 BusSleep（全网静默）
CBV_BITS = [
    (0, "重复报文请求 (Repeat Message Request)"),
    (3, "NM 协调器休眠就绪 (NM Coordinator Sleep Ready)"),
    (4, "主动唤醒 (Active Wakeup)"),
    (5, "部分网络信息 (PNI)"),
]

NM_PERIOD_MS = 100.0        # 典型 NM 周期（展示用）
NM_TIMEOUT_S = 2.0          # 静默判定

_nodes = {}             # node_id → {"last_ts","periods","last_cbv","state","count"}
_events = []
_base_id = 0x400
_mask_low = 0xFF
_total_nm = 0
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1500:
        del _events[:800]


def _cbv_text(cbv):
    bits = [name for bit, name in CBV_BITS if cbv & (1 << bit)]
    return "、".join(bits) if bits else "无"


def _on_frame(frame):
    global _total_nm
    if not _running:
        return
    fid = frame.id
    # NM ID 范围判定：base..base+0xFF 且低字节为节点号
    if not (_base_id <= fid <= _base_id + 0xFF):
        return
    data = frame.data
    if not data:
        return
    node = data[0]
    if node != (fid & _mask_low):
        # 部分实现 byte0 即节点号；不一致时以 byte0 为准并记事件
        _ev("WARN", "NM 0x%03X byte0=%02X 与 ID 低字节不一致" % (fid, node))
    cbv = data[-1] if len(data) >= 8 else (data[-1] if data else 0)
    user = data[1:-1] if len(data) > 2 else b""
    ts = time.time()

    _total_nm += 1
    st = _nodes.get(node)
    if st is None:
        st = {"last_ts": ts, "periods": [], "last_cbv": cbv, "state": "RepeatMessage",
              "count": 0, "last_user": user}
        _nodes[node] = st
        _ev("UP", "节点 %d 上线（ID 0x%03X）" % (node, fid))
    else:
        dt = (ts - st["last_ts"]) * 1000.0
        if 0 < dt < 5000:
            st["periods"].append(dt)
            if len(st["periods"]) > 64:
                del st["periods"][:32]
        st["last_ts"] = ts
        old_cbv = st["last_cbv"]
        if old_cbv != cbv:
            _ev("CBV", "节点 %d CBV 变化: %s → %s" % (node, _cbv_text(old_cbv), _cbv_text(cbv)))
    st["last_cbv"] = cbv
    st["count"] += 1
    st["last_user"] = user

    # 状态推断：CBV bit0=1 → RepeatMessage；否则 Normal
    new_state = "RepeatMessage" if (cbv & 0x01) else "Normal"
    if st["state"] != new_state:
        _ev("ST", "节点 %d: %s → %s" % (node, st["state"], new_state))
        st["state"] = new_state


def _infer_states():
    """超时节点状态修正（供刷新时调用）"""
    now = time.time()
    online = False
    for st in _nodes.values():
        if now - st["last_ts"] <= NM_TIMEOUT_S:
            online = True
            break
    result = {}
    for node, st in _nodes.items():
        age = now - st["last_ts"]
        if age <= NM_TIMEOUT_S:
            state = st["state"]
        elif online:
            state = "ReadySleep(静默)"
        else:
            state = "BusSleep(全网静默)"
        result[node] = (st, state, age)
    return result


def activate(context):
    global _running, _base_id
    _nodes.clear()
    del _events[:]

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QTabWidget, QSpinBox
        )
        from PyQt6.QtCore import QTimer
    except ImportError:
        sin.output.append("AUTOSAR NM 监视插件需要 PyQt6: pip install PyQt6")
        return

    _running = True
    win = sin.ui.create_window("AUTOSAR CAN 网络管理监视")
    win.resize(940, 610)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    summary = QLabel("等待 NM 报文（默认 ID 基址 0x400，低字节=节点号）...")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    top.addWidget(QLabel("NM 基址(hex)"))
    base_spin = QSpinBox()
    base_spin.setPrefix("0x")
    base_spin.setDisplayIntegerBase(16)
    base_spin.setRange(0x100, 0x7F00)
    base_spin.setValue(_base_id)
    top.addWidget(base_spin)
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
    node_tree.setHeaderLabels(["节点 ID", "状态", "报文数", "周期均值(ms)",
                               "周期抖动σ(ms)", "最后 CBV", "静默(s)"])
    node_tree.setRootIsDecorated(False)
    node_tree.setAlternatingRowColors(True)
    nh = node_tree.header()
    nh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    nv.addWidget(node_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(node_tab, "节点状态")
    tabs.addTab(log_tab, "状态时间线")

    hint = QLabel("状态推断：发帧中(CBV bit0=1)=RepeatMessage，发帧中(bit0=0)=Normal，"
                  "静默>%.0fs 且他人在线=ReadySleep，全网静默=BusSleep" % NM_TIMEOUT_S)
    hint.setStyleSheet("color: #888; font-size: 11px;")
    layout.addWidget(hint)

    context.on_frame(_on_frame)

    def refresh():
        states = _infer_states()
        active = sum(1 for _, (_, s, _) in states.items() if s in ("RepeatMessage", "Normal"))
        summary.setText("NM 帧总数 %d    在线节点 %d/%d    静默 %d"
                        % (_total_nm, active, len(states), len(states) - active))

        node_tree.clear()
        for node, (st, state, age) in sorted(states.items()):
            periods = st["periods"]
            if periods:
                pavg = sum(periods) / len(periods)
                var = sum((p - pavg) ** 2 for p in periods) / len(periods)
                jitter = var ** 0.5
                pavg_s, jitter_s = "%.1f" % pavg, "%.1f" % jitter
            else:
                pavg_s, jitter_s = "-", "-"
            node_tree.addTopLevelItem(QTreeWidgetItem([
                str(node), state, str(st["count"]), pavg_s, jitter_s,
                _cbv_text(st["last_cbv"]), "%.1f" % age]))

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-200:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

    timer = QTimer()
    timer.timeout.connect(refresh)
    timer.start(500)

    def on_base_changed(v):
        global _base_id
        _base_id = int(v)

    def on_clear():
        _nodes.clear()
        del _events[:]

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 NM 记录 CSV",
                                              "autosar_nm.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            states = _infer_states()
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("节点ID,状态,报文数,周期均值ms,周期抖动ms,最后CBV,静默s\n")
                for node, (st, state, age) in sorted(states.items()):
                    periods = st["periods"]
                    pavg = ("%.1f" % (sum(periods) / len(periods))) if periods else "-"
                    f.write("%d,%s,%d,%s,-,%s,%.1f\n"
                            % (node, state, st["count"], pavg,
                               _cbv_text(st["last_cbv"]).replace(",", "、"), age))
                f.write("\n时间线\n时间,类型,内容\n")
                for ts, k, t in _events:
                    f.write("%s,%s,%s\n" % (time.strftime("%H:%M:%S", time.localtime(ts)), k, t))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("autosarNmMonitor.open", on_open_cmd, "协议: AUTOSAR NM")

    base_spin.valueChanged.connect(on_base_changed)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append("AUTOSAR NM 监视插件已加载（订阅实时帧，NM 状态机被动监视）")


def deactivate():
    global _running
    _running = False
    sin.output.append("AUTOSAR NM 监视插件已停用")
