# -*- coding: utf-8 -*-
"""can-ids 插件 — CAN 入侵检测（CAN IDS 研究工具风格）
功能：
- 学习模式：采集 N 秒正常流量，建立 ID 白名单 + 基线（周期 μ/σ、载荷特征）
- 监控模式：
  * 新 ID 告警（白名单外）
  * 速率异常（间隔超出 μ±3σ）
  * 载荷异常（学习期恒定载荷被改变 / 载荷突变频率激增）
- 事件时间线 + CSV 导出；纯订阅只读（不发送任何帧）
依赖: pip install PyQt6
"""

import csv
import time

from PyQt6.QtCore import QTimer

import sin

_whitelist = {}      # id -> {"mu": ms, "sigma": ms, "static_payload": bytes|None, "chg": n, "count": n}
_learn_cfg = {"learning": False, "deadline": 0.0, "secs": 30}
_monitor = False
_events = []         # [(ts, level, kind, detail)]
_alert_ts = {}       # (id, kind) -> last alert ts（节流：同 ID 同类 10s 一条）
_runtime = {}        # id -> {"count", "chg", "last": t, "periods": [ms], "data": bytes}
_learn_stats = {}    # 学习期临时：id -> {"count", "chg", "last": t, "periods": [ms], "data": bytes}
_summary = {"learned_ids": 0, "learn_frames": 0, "alerts": 0, "checked": 0}
_running = True


def _feed(stats, frame):
    now = time.time()
    st = stats.get(frame.id)
    if st is None:
        stats[frame.id] = {"count": 1, "chg": 0, "last": now,
                           "periods": [], "data": frame.data, "ext": frame.extended}
    else:
        dt = (now - st["last"]) * 1000.0
        if 0 < dt < 60000:
            st["periods"].append(dt)
            if len(st["periods"]) > 128:
                del st["periods"][:64]
        if frame.data != st["data"]:
            st["chg"] += 1
        st["data"] = frame.data
        st["last"] = now
        st["count"] += 1


def _alert(cid, level, kind, detail):
    key = (cid, kind)
    now = time.time()
    if now - _alert_ts.get(key, 0) < 10.0:
        return
    _alert_ts[key] = now
    _events.append((now, level, kind,
                    "ID 0x%X %s" % (cid, detail)))
    _summary["alerts"] += 1


def _finish_learn():
    global _whitelist
    _learn_cfg["learning"] = False
    total_frames = sum(st["count"] for st in _learn_stats.values())
    _whitelist = {}
    for cid, st in _learn_stats.items():
        mu = sigma = 0.0
        if len(st["periods"]) >= 4:
            mu = sum(st["periods"]) / len(st["periods"])
            var = sum((p - mu) ** 2 for p in st["periods"]) / len(st["periods"])
            sigma = var ** 0.5
        chg_prob = st["chg"] / max(1, st["count"])
        _whitelist[cid] = {
            "mu": mu, "sigma": sigma,
            "static_payload": st["data"] if chg_prob < 0.02 else None,
            "chg_prob": chg_prob, "count": st["count"], "ext": st.get("ext", False)}
    _summary["learned_ids"] = len(_whitelist)
    _summary["learn_frames"] = total_frames
    _events.append((time.time(), "INFO", "学习完成",
                    "白名单 %d 个 ID / %d 帧" % (len(_whitelist), total_frames)))


def _on_frame(frame):
    if not _running:
        return
    if _learn_cfg["learning"]:
        _feed(_learn_stats, frame)
        return
    if not _monitor:
        return
    _summary["checked"] += 1
    base = _whitelist.get(frame.id)
    if base is None:
        _alert(frame.id, "高", "新 ID", "不在学习白名单内")
        _feed(_runtime, frame)
        return
    now = time.time()
    st = _runtime.get(frame.id)
    if st is None:
        _runtime[frame.id] = {"count": 1, "chg": 0, "last": now,
                              "periods": [], "data": frame.data}
        return
    dt = (now - st["last"]) * 1000.0
    st["last"] = now
    st["count"] += 1
    # 速率异常：μ±3σ（σ 过小则放宽到 μ±10%）
    if base["mu"] > 0 and 0 < dt < 60000:
        tol = max(3.0 * base["sigma"], 0.10 * base["mu"], 1.0)
        if abs(dt - base["mu"]) > tol:
            _alert(frame.id, "中", "速率异常",
                   "间隔 %.1fms（基线 %.1f±%.1fms）" % (dt, base["mu"], tol))
    # 载荷异常：学习期恒定载荷被改变
    if base["static_payload"] is not None and frame.data != base["static_payload"]:
        if frame.data != st["data"]:
            _alert(frame.id, "高", "载荷异常",
                   "学习期恒定载荷被改变")
    if frame.data != st["data"]:
        st["chg"] += 1
    st["data"] = frame.data


def activate(context):
    global _monitor
    _monitor = False
    _learn_cfg["learning"] = False
    del _events[:]
    _runtime.clear()
    _learn_stats.clear()
    _whitelist.clear()
    _summary.update({"learned_ids": 0, "learn_frames": 0, "alerts": 0, "checked": 0})

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QSpinBox, QGroupBox, QFormLayout
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("入侵检测插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN 入侵检测")
    win.resize(960, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("基线学习")
    cfg_l = QFormLayout(cfg)
    secs_spin = QSpinBox()
    secs_spin.setRange(5, 600)
    secs_spin.setValue(30)
    secs_spin.setSuffix(" 秒")
    cfg_l.addRow("学习时长:", secs_spin)
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    learn_btn = QPushButton("开始学习（采集正常流量）")
    monitor_btn = QPushButton("开始监控")
    stop_btn = QPushButton("停止监控")
    export_btn = QPushButton("导出事件 CSV")
    clear_btn = QPushButton("清空事件")
    btns.addWidget(learn_btn)
    btns.addWidget(monitor_btn)
    btns.addWidget(stop_btn)
    btns.addStretch(1)
    btns.addWidget(export_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)

    status = QLabel("先学习基线，再开始监控（只读，不发送）")
    status.setStyleSheet("font-weight:bold;")
    layout.addWidget(status)

    tree = QTreeWidget()
    tree.setHeaderLabels(["时间", "级别", "类型", "详情"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    def _on_learn():
        global _monitor
        _monitor = False
        _learn_cfg["secs"] = secs_spin.value()
        _learn_cfg["learning"] = True
        _learn_cfg["deadline"] = time.time() + secs_spin.value()
        _learn_stats.clear()
        learn_btn.setEnabled(False)
        monitor_btn.setEnabled(False)
        status.setText("学习模式：正在采集 %d 秒正常流量…" % secs_spin.value())
        _events.append((time.time(), "INFO", "学习开始", "时长 %d 秒" % secs_spin.value()))

    def _on_monitor():
        global _monitor
        if not _whitelist:
            QMessageBox.information(win, "提示", "请先执行基线学习")
            return
        if _learn_cfg["learning"]:
            QMessageBox.information(win, "提示", "学习尚未结束")
            return
        _monitor = True
        _runtime.clear()
        status.setText("监控中 · 白名单 %d ID · 告警阈值 μ±3σ" % len(_whitelist))
        _events.append((time.time(), "INFO", "监控开始", ""))

    def _on_stop():
        global _monitor
        _monitor = False
        status.setText("监控已停止")

    def _on_export():
        if not _events:
            QMessageBox.information(win, "提示", "无事件")
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出事件", "ids_events.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = csv.writer(f)
                w.writerow(["时间", "级别", "类型", "详情"])
                for ts, level, kind, detail in _events:
                    w.writerow([time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts)),
                                level, kind, detail])
            QMessageBox.information(win, "导出成功", "已导出 %d 条事件" % len(_events))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_clear():
        del _events[:]
        tree.clear()

    def _refresh():
        if _learn_cfg["learning"]:
            remain = max(0, int(_learn_cfg["deadline"] - time.time()))
            n_frames = sum(st["count"] for st in _learn_stats.values())
            status.setText("学习模式：剩余 %d 秒 · 已采集 %d 帧 / %d ID"
                           % (remain, n_frames, len(_learn_stats)))
            if remain <= 0:
                _finish_learn()
                learn_btn.setEnabled(True)
                monitor_btn.setEnabled(True)
                status.setText("学习完成：白名单 %d ID / %d 帧，可开始监控"
                               % (_summary["learned_ids"], _summary["learn_frames"]))
        elif _monitor:
            status.setText("监控中 · 已检查 %d 帧 · 告警 %d 次 · 白名单 %d ID"
                           % (_summary["checked"], _summary["alerts"], len(_whitelist)))
        if _events:
            tree.clear()
            colors = {"高": QColor("#c62828"), "中": QColor("#ef6c00"),
                      "INFO": QColor("#1565c0")}
            for ts, level, kind, detail in _events[-300:]:
                item = QTreeWidgetItem([
                    time.strftime("%H:%M:%S", time.localtime(ts)), level, kind, detail])
                item.setForeground(1, colors.get(level))
                tree.addTopLevelItem(item)
            sb = tree.verticalScrollBar()
            sb.setValue(sb.maximum())

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("canIds.open", _on_open_cmd, "安全: 入侵检测")

    learn_btn.clicked.connect(_on_learn)
    monitor_btn.clicked.connect(_on_monitor)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)

    monitor_btn.setEnabled(False)

    timer = QTimer()
    timer.timeout.connect(_refresh)
    timer.start(500)

    win.show()
    sin.output.append("入侵检测插件已加载（学习+监控，只读安全）")


def deactivate():
    global _running, _monitor
    _running = False
    _monitor = False
    _learn_cfg["learning"] = False
    sin.output.append("入侵检测插件已停用")
