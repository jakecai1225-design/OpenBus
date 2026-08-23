# -*- coding: utf-8 -*-
"""can-simulator 插件 — CAN 节点仿真器（Restbus 轻量版）
功能：
- DBC 驱动 Restbus：按 GenMsgCycleTime 周期发送选中报文
- 信号值模式：恒值 / 递增 / 正弦 / 随机 / 斜坡（每信号可配）
- 启停控制、实时编码值监视、发送统计
- 仅用户点击「开始仿真」才发帧（安全基线）
依赖: pip install PyQt6
"""

import math
import random
import time

from PyQt6.QtCore import QTimer

import sin
import dbcparse

_dbc = None
_sims = []          # [{msg, cycle, phases: {sig: val}, modes: {sig: mode}, sent}]
_running = False
_t0 = 0.0


def activate(context):
    global _dbc, _running
    _dbc = None
    _running = False
    del _sims[:]

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QComboBox, QCheckBox, QDialog,
            QDialogButtonBox, QScrollArea, QFrame
        )
    except ImportError:
        sin.output.append("节点仿真器插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN 节点仿真器 (Restbus)")
    win.resize(1000, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_btn = QPushButton("加载 DBC…")
    config_btn = QPushButton("配置报文/信号模式…")
    start_btn = QPushButton("开始仿真")
    stop_btn = QPushButton("停止")
    top.addWidget(load_btn)
    top.addWidget(config_btn)
    top.addStretch(1)
    top.addWidget(start_btn)
    top.addWidget(stop_btn)
    layout.addLayout(top)

    dbc_label = QLabel("未加载 DBC")
    dbc_label.setStyleSheet("color:#888;font-weight:bold;")
    layout.addWidget(dbc_label)

    tree = QTreeWidget()
    tree.setHeaderLabels(["报文", "ID", "周期ms", "信号数", "已发送", "最后数据"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    header = tree.header()
    header.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    log_view.setMaximumHeight(120)
    layout.addWidget(log_view)

    def _log(text):
        log_view.append("[%s] %s" % (time.strftime("%H:%M:%S"), text))

    def _refresh():
        tree.clear()
        for s in _sims:
            msg = s["msg"]
            item = QTreeWidgetItem([
                msg.name, "0x%X" % msg.can_id, str(s["cycle"]),
                str(len(msg.signals)), str(s["sent"]),
                " ".join("%02X" % b for b in s.get("last", b""))[:47]])
            tree.addTopLevelItem(item)

    def _on_load():
        global _dbc
        path, _ = QFileDialog.getOpenFileName(win, "加载 DBC", "", "DBC 文件 (*.dbc)")
        if not path:
            return
        db = dbcparse.parse_file(path)
        cyclic = [m for m in db.messages.values() if m.cycle_time > 0]
        if not cyclic:
            QMessageBox.warning(win, "加载失败", "DBC 无周期报文（GenMsgCycleTime）")
            return
        _dbc = db
        del _sims[:]
        for m in cyclic:
            _sims.append({"msg": m, "cycle": m.cycle_time,
                          "phases": {}, "modes": {}, "sent": 0, "last": b""})
        dbc_label.setText("已加载 %s: %d 报文（周期 %d）"
                          % (path.split("\\")[-1], len(db.messages), len(cyclic)))
        dbc_label.setStyleSheet("color:#2e7d32;font-weight:bold;")
        _refresh()
        _log("DBC 加载: %d 周期报文进入仿真列表" % len(cyclic))

    def _on_config():
        if not _sims:
            QMessageBox.information(win, "提示", "请先加载 DBC")
            return
        dlg = QDialog(win)
        dlg.setWindowTitle("仿真配置")
        dlg.resize(760, 560)
        dl = QVBoxLayout(dlg)
        tree2 = QTreeWidget()
        tree2.setHeaderLabels(["报文/信号", "模式", "参数（初值/幅值/步长）"])
        mode_combos = {}
        param_edits = {}
        for s in _sims:
            m = s["msg"]
            parent = QTreeWidgetItem(["%s (0x%X, %dms)" % (m.name, m.can_id, s["cycle"])])
            tree2.addTopLevelItem(parent)
            for sig in m.signals:
                child = QTreeWidgetItem([sig.name])
                parent.addChild(child)
                combo = QComboBox()
                combo.addItems(["恒值", "递增", "正弦", "随机", "斜坡"])
                combo.setCurrentText(s["modes"].get(sig.name, "恒值"))
                mode_combos[(m.can_id, sig.name)] = combo
                tree2.setItemWidget(child, 1, combo)
                edit = __import__("PyQt6.QtWidgets", fromlist=["QLineEdit"]).QLineEdit(
                    "%.2f" % (s["phases"].get(sig.name, sig.minimum)))
                param_edits[(m.can_id, sig.name)] = edit
                tree2.setItemWidget(child, 2, edit)
        tree2.expandAll()
        dl.addWidget(tree2, 1)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        dl.addWidget(bb)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        for s in _sims:
            m = s["msg"]
            for sig in m.signals:
                key = (m.can_id, sig.name)
                s["modes"][sig.name] = mode_combos[key].currentText()
                try:
                    s["phases"][sig.name] = float(param_edits[key].text())
                except ValueError:
                    s["phases"][sig.name] = sig.minimum
        _log("仿真配置已更新")
        _refresh()

    def _compute_value(s, sig, t):
        mode = s["modes"].get(sig.name, "恒值")
        base = s["phases"].get(sig.name, sig.minimum)
        amp = max(abs(sig.maximum - sig.minimum) / 2.0, 1.0)
        mid = (sig.maximum + sig.minimum) / 2.0
        if mode == "恒值":
            v = base
        elif mode == "递增":
            step = max(amp / 50.0, abs(sig.factor))
            v = base + (t * step)
        elif mode == "正弦":
            v = mid + amp * math.sin(t * 2.0 * math.pi / 5.0 + base)
        elif mode == "随机":
            v = random.uniform(sig.minimum, sig.maximum)
        elif mode == "斜坡":
            v = sig.minimum + (sig.maximum - sig.minimum) * ((t % 10.0) / 10.0)
        else:
            v = base
        # 限幅
        if sig.factor >= 0:
            v = max(sig.minimum, min(sig.maximum, v))
        return v

    def _on_tick():
        global _t0
        t = time.time() - _t0
        for s in _sims:
            msg = s["msg"]
            values = {}
            for sig in msg.signals:
                values[sig.name] = _compute_value(s, sig, t)
            # 更新递增相位
            for sig in msg.signals:
                if s["modes"].get(sig.name) == "递增":
                    s["phases"][sig.name] = values[sig.name]
            data = bytes(dbcparse.encode_message(msg, values, dlc=max(msg.dlc, 8)))
            s["last"] = data
            s["sent"] += 1
            sin.frames.send(msg.can_id, data, extended=msg.extended)
        _refresh()

    sim_timer = QTimer()

    def _on_start():
        global _running, _t0
        if not _sims:
            QMessageBox.information(win, "提示", "请先加载 DBC")
            return
        _running = True
        _t0 = time.time()
        tick = max(10, min(s["cycle"] for s in _sims))
        sim_timer.timeout.connect(_on_tick)
        sim_timer.start(tick)
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        _log("仿真开始: %d 报文, tick %dms" % (len(_sims), tick))

    def _on_stop():
        global _running
        _running = False
        sim_timer.stop()
        try:
            sim_timer.timeout.disconnect(_on_tick)
        except TypeError:
            pass
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        _log("仿真停止")

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("canSimulator.open", _on_open_cmd, "仿真: 节点仿真器")

    load_btn.clicked.connect(_on_load)
    config_btn.clicked.connect(_on_config)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    stop_btn.setEnabled(False)

    refresh_timer = QTimer()
    refresh_timer.timeout.connect(_refresh)
    refresh_timer.start(1000)

    win.show()
    sin.output.append("节点仿真器插件已加载（DBC Restbus，激活期间零发送）")


def deactivate():
    global _running
    _running = False
    sin.output.append("节点仿真器插件已停用")
