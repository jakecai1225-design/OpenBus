# -*- coding: utf-8 -*-
"""can-fuzzer 插件 — CAN 模糊测试（SavvyCAN fuzzer 风格）
功能：
- ID 范围选择（最小/最大 ID，标准/扩展帧，经典/FD）
- 变异模式：全随机载荷 / 单字节翻位 / 结构化模板（掩码 0 位保留原值、1 位随机）
- 限速发送（帧间隔 ms）与发送计数上限（0=不限）
- 突变统计：发送数 / 变异字节数 / 按 ID 分布
- ⚠ 会向总线发送报文：仅用于试验台架/离线环境
依赖: pip install PyQt6
"""

import random

from PyQt6.QtCore import QTimer

import sin

_state = {"running": False, "sent": 0, "mutated_bytes": 0, "limit": 0}
_dist = {}            # id -> 发送计数
_timer = None


def _make_payload(mode, dlc, mask):
    if mode == 0:                      # 全随机
        return bytes(random.getrandbits(8) for _ in range(dlc))
    if mode == 1:                      # 基于种子单字节翻位（种子=全 0）
        buf = bytearray(dlc)
        pos = random.randrange(dlc)
        buf[pos] = random.getrandbits(8)
        _state["mutated_bytes"] += 1
        return bytes(buf)
    # 结构化模板：mask 中 0 的字节保留 0x00，1 的字节随机
    buf = bytearray(dlc)
    for i in range(dlc):
        m = mask[i] if i < len(mask) else 0xFF
        buf[i] = random.getrandbits(8) & m
        if m:
            _state["mutated_bytes"] += 1
    return bytes(buf)


def activate(context):
    global _timer
    _state.update({"running": False, "sent": 0, "mutated_bytes": 0, "limit": 0})
    _dist.clear()

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QHeaderView, QGroupBox,
            QFormLayout, QLineEdit, QComboBox, QSpinBox, QCheckBox,
            QMessageBox
        )
    except ImportError:
        sin.output.append("模糊测试插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN 模糊测试")
    win.resize(940, 600)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    warn = QLabel("⚠ 模糊测试会向总线发送随机报文 — 请仅在试验台架 / 隔离环境使用！")
    warn.setStyleSheet("color:#c62828; font-weight:bold;")
    layout.addWidget(warn)

    cfg = QGroupBox("模糊配置")
    cfg_l = QFormLayout(cfg)
    id_min = QLineEdit("0x100")
    id_max = QLineEdit("0x1FF")
    cfg_l.addRow("ID 范围:", _range_row(id_min, id_max))
    ext_chk = QCheckBox("扩展帧（29bit）")
    fd_chk = QCheckBox("CAN FD")
    cfg_l.addRow("帧属性:", _h(ext_chk, fd_chk))
    dlc_spin = QSpinBox()
    dlc_spin.setRange(1, 64)
    dlc_spin.setValue(8)
    cfg_l.addRow("数据长度:", dlc_spin)
    mode_combo = QComboBox()
    mode_combo.addItems(["全随机载荷", "单字节翻位", "结构化模板（掩码）"])
    cfg_l.addRow("变异模式:", mode_combo)
    mask_edit = QLineEdit("FF FF FF FF 00 00 00 00")
    mask_edit.setPlaceholderText("掩码：0=字节保留，1=字节随机，如 FF FF FF FF 00 00 00 00")
    cfg_l.addRow("结构化掩码:", mask_edit)
    interval_spin = QSpinBox()
    interval_spin.setRange(1, 10000)
    interval_spin.setValue(50)
    interval_spin.setSuffix(" ms")
    cfg_l.addRow("发送间隔:", interval_spin)
    limit_spin = QSpinBox()
    limit_spin.setRange(0, 1000000)
    limit_spin.setValue(500)
    limit_spin.setSpecialValueText("不限")
    cfg_l.addRow("发送上限:", limit_spin)
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    start_btn = QPushButton("▶ 开始模糊测试")
    stop_btn = QPushButton("■ 停止")
    export_btn = QPushButton("导出统计 CSV…")
    btns.addWidget(start_btn)
    btns.addWidget(stop_btn)
    btns.addStretch(1)
    btns.addWidget(export_btn)
    layout.addLayout(btns)

    stats = QLabel("就绪（未发送任何帧）")
    stats.setStyleSheet("font-weight:bold;")
    layout.addWidget(stats)

    tree = QTreeWidget()
    tree.setHeaderLabels(["ID", "发送数", "占比%", "最近载荷"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    def _parse_id(text):
        try:
            v = int(text, 0)
            if 0 <= v <= 0x1FFFFFFF:
                return v
        except ValueError:
            pass
        return None

    def _parse_mask():
        try:
            toks = mask_edit.text().replace(" ", "").strip()
            if not toks:
                return None
            if len(toks) % 2:
                toks = "0" + toks
            return bytes.fromhex(toks)
        except ValueError:
            return b""

    def _tick():
        if not _state["running"]:
            return
        id_lo = _parse_id(id_min.text())
        id_hi = _parse_id(id_max.text())
        if id_lo is None or id_hi is None or id_lo > id_hi:
            _state["running"] = False
            return
        dlc = dlc_spin.value()
        if fd_chk.isChecked():
            dlc = min(64, max(8, dlc))
        mask = _parse_mask() if mode_combo.currentIndex() == 2 else None
        if mask == b"":
            _state["running"] = False
            stats.setText("掩码格式错误，已停止")
            return
        cid = random.randint(id_lo, id_hi)
        payload = _make_payload(mode_combo.currentIndex(), dlc, mask)
        try:
            sin.frames.send(cid, payload, ext_chk.isChecked(), fd_chk.isChecked())
        except Exception as e:      # 发送链路异常不中断统计
            stats.setText("发送异常: %s" % e)
            return
        _state["sent"] += 1
        _dist[cid] = _dist.get(cid, 0) + 1
        _dist["_last"] = payload.hex().upper()
        if _state["limit"] and _state["sent"] >= _state["limit"]:
            _state["running"] = False
            start_btn.setEnabled(True)
            stats.setText("已达发送上限 %d 帧，已停止" % _state["limit"])

    def _on_start():
        id_lo = _parse_id(id_min.text())
        id_hi = _parse_id(id_max.text())
        if id_lo is None or id_hi is None or id_lo > id_hi:
            QMessageBox.warning(win, "格式错误", "ID 范围需为十六进制且 最小 ≤ 最大")
            return
        if mode_combo.currentIndex() == 2:
            mask = _parse_mask()
            if mask == b"":
                QMessageBox.warning(win, "格式错误", "结构化掩码需为十六进制字节串")
                return
        _state["limit"] = limit_spin.value()
        _state["running"] = True
        _timer.start(interval_spin.value())
        start_btn.setEnabled(False)
        stats.setText("模糊测试进行中…")

    def _on_stop():
        _state["running"] = False
        _timer.stop()
        start_btn.setEnabled(True)
        stats.setText("已停止 · 共发送 %d 帧" % _state["sent"])

    def _on_export():
        if not _dist:
            QMessageBox.information(win, "提示", "无统计数据")
            return
        from PyQt6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getSaveFileName(win, "导出统计", "fuzz_stats.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            import csv as _csv
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = _csv.writer(f)
                w.writerow(["ID", "发送数", "占比%"])
                rows = [(k, v) for k, v in _dist.items() if k != "_last"]
                for cid, n in sorted(rows, key=lambda kv: -kv[1]):
                    w.writerow(["0x%X" % cid, n,
                                "%.2f" % (100.0 * n / max(1, _state["sent"]))])
            QMessageBox.information(win, "导出成功", path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _refresh():
        if _state["running"]:
            stats.setText("发送中 · 已发 %d 帧 · 变异字节 %d · 间隔 %dms"
                          % (_state["sent"], _state["mutated_bytes"],
                             interval_spin.value()))
        tree.clear()
        rows = [(k, v) for k, v in _dist.items() if k != "_last"]
        for cid, n in sorted(rows, key=lambda kv: -kv[1])[:100]:
            tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid, str(n),
                "%.2f" % (100.0 * n / max(1, _state["sent"])), ""]))

    def _on_interval(v):
        if _state["running"]:
            _timer.start(v)

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("canFuzzer.open", _on_open_cmd, "安全: 模糊测试")

    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)
    interval_spin.valueChanged.connect(_on_interval)

    _timer = QTimer()
    _timer.timeout.connect(_tick)

    refresher = QTimer()
    refresher.timeout.connect(_refresh)
    refresher.start(500)

    stop_btn.setEnabled(True)
    win.show()
    sin.output.append("模糊测试插件已加载（默认不发送，点击开始后才发送）")


def _range_row(id_min, id_max):
    from PyQt6.QtWidgets import QWidget, QHBoxLayout, QLabel
    box = QWidget()
    lay = QHBoxLayout(box)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.addWidget(id_min)
    lay.addWidget(QLabel("~"))
    lay.addWidget(id_max)
    return box


def _h(*widgets):
    from PyQt6.QtWidgets import QWidget, QHBoxLayout
    box = QWidget()
    lay = QHBoxLayout(box)
    lay.setContentsMargins(0, 0, 0, 0)
    for w in widgets:
        lay.addWidget(w)
    lay.addStretch(1)
    return box


def deactivate():
    global _timer
    _state["running"] = False
    if _timer is not None:
        _timer.stop()
    sin.output.append("模糊测试插件已停用")
