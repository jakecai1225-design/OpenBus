# -*- coding: utf-8 -*-
"""can-stress 插件 — CAN 压力测试（CANoe Stress Program 风格）
功能：
- 突发洪泛：按目标负载 % 计算帧率（假设 500kbps，经典 8 字节帧 ≈128bit）
- 畸形帧注入：随机 DLC（0-8）/ 极短帧（DLC=0）/ FD 长度序列（8→64 循环）
- 持续时间（秒，0=不限）与帧间隔配置；统计面板（发送数/实际帧率/时长）
- ⚠ 会向总线发送报文：仅用于试验台架/离线环境
依赖: pip install PyQt6
"""

import time

from PyQt6.QtCore import QTimer

import sin

_state = {"running": False, "sent": 0, "started": 0.0, "duration": 0,
          "mode": 0, "target_fps": 0.0, "errors": 0}
_timer = None
_fd_seq = [8, 12, 16, 20, 24, 32, 48, 64]
_fd_idx = 0
_counter = 0


def _next_payload():
    global _counter, _fd_idx
    mode = _state["mode"]
    if mode == 0:                        # 常规洪泛：递增计数载荷
        _counter = (_counter + 1) & 0xFFFFFFFF
        return bytes([(_counter >> s) & 0xFF for s in (24, 16, 8, 0)] + [0x00] * 4)
    if mode == 1:                        # 随机 DLC（0-8）
        import random
        n = random.randint(0, 8)
        return bytes(random.getrandbits(8) for _ in range(n))
    if mode == 2:                        # 极短帧（DLC=0）
        return b""
    # FD 长度序列
    n = _fd_seq[_fd_idx % len(_fd_seq)]
    _fd_idx += 1
    return bytes([i & 0xFF for i in range(n)])


def activate(context):
    global _timer, _fd_idx, _counter
    _state.update({"running": False, "sent": 0, "started": 0.0,
                   "duration": 0, "mode": 0, "target_fps": 0.0, "errors": 0})
    _fd_idx = 0
    _counter = 0

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QGroupBox, QFormLayout, QComboBox, QSpinBox, QDoubleSpinBox,
            QCheckBox, QMessageBox
        )
    except ImportError:
        sin.output.append("压力测试插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN 压力测试")
    win.resize(880, 560)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    warn = QLabel("⚠ 压力测试会向总线注入大流量/畸形帧 — 仅在试验台架 / 隔离环境使用！")
    warn.setStyleSheet("color:#c62828; font-weight:bold;")
    layout.addWidget(warn)

    cfg = QGroupBox("压力配置")
    cfg_l = QFormLayout(cfg)
    id_edit = QSpinBox()
    id_edit.setRange(1, 0x7FF)
    id_edit.setValue(0x66)
    id_edit.setPrefix("0x")
    id_edit.setDisplayIntegerBase(16)
    cfg_l.addRow("目标 ID:", id_edit)
    mode_combo = QComboBox()
    mode_combo.addItems(["常规洪泛（递增计数载荷）", "畸形帧：随机 DLC（0-8）",
                         "畸形帧：极短帧（DLC=0）", "CAN FD 长度序列（8→64 循环）"])
    cfg_l.addRow("注入模式:", mode_combo)
    load_spin = QDoubleSpinBox()
    load_spin.setRange(1, 100)
    load_spin.setValue(50)
    load_spin.setSuffix(" %")
    cfg_l.addRow("目标负载:", load_spin)
    bitrate_spin = QComboBox()
    bitrate_spin.addItems(["125 kbps", "250 kbps", "500 kbps", "1 Mbps"])
    bitrate_spin.setCurrentIndex(2)
    cfg_l.addRow("总线波特率:", bitrate_spin)
    duration_spin = QSpinBox()
    duration_spin.setRange(0, 3600)
    duration_spin.setValue(10)
    duration_spin.setSpecialValueText("不限")
    duration_spin.setSuffix(" 秒")
    cfg_l.addRow("持续时间:", duration_spin)
    gap_spin = QSpinBox()
    gap_spin.setRange(0, 10000)
    gap_spin.setValue(0)
    gap_spin.setSuffix(" ms（最小间隔保护）")
    cfg_l.addRow("最小帧间隔:", gap_spin)
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    start_btn = QPushButton("▶ 开始压测")
    stop_btn = QPushButton("■ 停止")
    btns.addWidget(start_btn)
    btns.addWidget(stop_btn)
    btns.addStretch(1)
    layout.addLayout(btns)

    stats = QLabel("就绪（未发送任何帧）")
    stats.setStyleSheet("font-weight:bold;")
    layout.addWidget(stats)

    log = QLabel("")
    log.setWordWrap(True)
    layout.addWidget(log)

    def _fps_from_load():
        bitrate_kbps = [125, 250, 500, 1000][bitrate_spin.currentIndex()]
        # 经典 8 字节帧含填充位/IFS 约 128 bit；FD 按有效 DLC 估算
        mode = mode_combo.currentIndex()
        bits_per_frame = 128 if mode != 3 else 700
        return bitrate_kbps * 1000.0 * (load_spin.value() / 100.0) / bits_per_frame

    def _tick():
        if not _state["running"]:
            return
        if _state["duration"] and time.time() - _state["started"] >= _state["duration"]:
            _state["running"] = False
            _timer.stop()
            start_btn.setEnabled(True)
            stats.setText("压测完成 · 共发送 %d 帧 / %0.1fs"
                          % (_state["sent"], time.time() - _state["started"]))
            return
        mode = mode_combo.currentIndex()
        is_fd = mode == 3
        payload = _next_payload()
        try:
            sin.frames.send(id_edit.value(), payload, False, is_fd)
            _state["sent"] += 1
        except Exception as e:
            _state["errors"] += 1
            log.setText("发送异常: %s" % e)

    def _on_start():
        _state["mode"] = mode_combo.currentIndex()
        _state["target_fps"] = _fps_from_load()
        _state["duration"] = duration_spin.value()
        if _state["target_fps"] <= 0:
            QMessageBox.warning(win, "配置错误", "目标帧率计算为 0")
            return
        interval_ms = 1000.0 / _state["target_fps"]
        if gap_spin.value() and interval_ms < gap_spin.value():
            interval_ms = gap_spin.value()
        interval_ms = max(1, int(interval_ms))
        _state["started"] = time.time()
        _state["sent"] = 0
        _state["errors"] = 0
        _state["running"] = True
        _timer.start(interval_ms)
        start_btn.setEnabled(False)
        stats.setText("压测中 · 目标 %.0f 帧/s · 间隔 %dms" % (
            _state["target_fps"], interval_ms))

    def _on_stop():
        _state["running"] = False
        _timer.stop()
        start_btn.setEnabled(True)
        elapsed = time.time() - _state["started"] if _state["started"] else 0
        stats.setText("已停止 · 发送 %d 帧 / %.1fs · 实际 ≈%.0f 帧/s" % (
            _state["sent"], elapsed,
            _state["sent"] / elapsed if elapsed > 0.5 else 0))

    def _refresh():
        if _state["running"]:
            elapsed = time.time() - _state["started"]
            actual = _state["sent"] / elapsed if elapsed > 0.5 else 0
            remain = ""
            if _state["duration"]:
                remain = " · 剩余 %d s" % max(
                    0, int(_state["duration"] - elapsed))
            stats.setText("压测中 · 已发 %d 帧 · 目标 %.0f 帧/s · 实际 ≈%.0f 帧/s%s"
                          % (_state["sent"], _state["target_fps"], actual, remain))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("canStress.open", _on_open_cmd, "安全: 压力测试")

    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)

    _timer = QTimer()
    _timer.timeout.connect(_tick)

    refresher = QTimer()
    refresher.timeout.connect(_refresh)
    refresher.start(500)

    win.show()
    sin.output.append("压力测试插件已加载（默认不发送，点击开始后才注入）")


def deactivate():
    global _timer
    _state["running"] = False
    if _timer is not None:
        _timer.stop()
    sin.output.append("压力测试插件已停用")
