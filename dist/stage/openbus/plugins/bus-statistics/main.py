"""bus-statistics 插件 — 总线统计分析（G9，替代原 C++ FrameStatisticsView + BusStatistics UI）

功能：
- 实时统计：按 CAN ID 帧数 / 周期 min-avg-max-σ / 频率 / 累计字节数
- 汇总：总帧数、ID 数、总线负载率、统计时长
- 波特率可选（负载率计算），统计启停 / 清零 / 导出 CSV
- 数据源：context.on_frame 订阅（宿主 100ms 批量推送），1s QTimer 聚合刷新

依赖: pip install PyQt6
"""

import time
from collections import deque

import sin


# 统计状态（模块级，便于 deactivate 后残留窗口安全）
_stats = {}          # can_id → {"count","bytes","periods"(deque64),"last_ts","first_ts"}
_total_frames = 0
_total_bytes_bits = 0
_start_time = None   # 首帧时间戳
_last_time = None
_bitrate = 500000
_running = True


def _frame_bits(frame):
    """估算帧占用总线位数（含近似位填充）"""
    dlc = frame.dlc
    base = 47 + 8 * dlc          # 标准帧：SOF+仲裁+控制+数据+CRC+ACK+EOF+IFS 近似
    if frame.extended:
        base += 20               # 扩展标识符 20 位
    if frame.fd:
        base += 10               # FD BRS/ESI/EDL 等额外位近似
    stuff = (34 + 8 * dlc) // 4  # 位填充近似（每 4 位 1 填充）
    return base + stuff


def _reset():
    global _stats, _total_frames, _total_bytes_bits, _start_time, _last_time
    _stats.clear()
    _total_frames = 0
    _total_bytes_bits = 0
    _start_time = None
    _last_time = None


def activate(context):
    global _running, _bitrate

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QComboBox, QTreeWidget, QTreeWidgetItem, QFileDialog,
            QMessageBox, QHeaderView
        )
        from PyQt6.QtCore import QTimer
    except ImportError:
        sin.output.append("总线统计插件需要 PyQt6: pip install PyQt6")
        return

    _reset()
    _running = True

    win = sin.ui.create_window("总线统计分析")
    win.resize(880, 560)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    # ---- 顶部：汇总 + 控制 ----
    top = QHBoxLayout()
    summary = QLabel("等待数据...")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)

    rate_combo = QComboBox()
    for r in (125000, 250000, 500000, 1000000, 2000000, 5000000, 8000000):
        label = f"{r // 1000} kbps" if r < 1000000 else f"{r // 1000000} Mbps"
        rate_combo.addItem(label, r)
    rate_combo.setCurrentIndex(2)   # 默认 500k
    _bitrate = 500000

    start_btn = QPushButton("暂停")
    clear_btn = QPushButton("清零")
    export_btn = QPushButton("导出 CSV")
    top.addWidget(QLabel("波特率:"))
    top.addWidget(rate_combo)
    top.addWidget(start_btn)
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    # ---- 统计表 ----
    tree = QTreeWidget()
    tree.setHeaderLabels(["CAN ID", "帧数", "周期 min (ms)", "周期 avg (ms)",
                          "周期 max (ms)", "抖动 σ (ms)", "频率 (Hz)", "字节数"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSortingEnabled(True)
    header = tree.header()
    header.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    hint = QLabel("数据源 = 当前工程实时总线帧；周期/频率基于帧时间戳，抖动 σ 取最近 64 个周期样本")
    hint.setStyleSheet("color: #888; font-size: 11px;")
    layout.addWidget(hint)

    # ---- 帧处理（宿主批量推送）----
    def on_frame(frame):
        if not _running:
            return
        global _total_frames, _total_bytes_bits, _start_time, _last_time
        fid = frame.id
        st = _stats.get(fid)
        ts = frame.timestamp
        if st is None:
            st = {"count": 0, "bytes": 0, "periods": deque(maxlen=64),
                  "last_ts": None, "first_ts": ts}
            _stats[fid] = st
        if st["last_ts"] is not None:
            dt = (ts - st["last_ts"]) * 1000.0   # ms
            if dt > 0:
                st["periods"].append(dt)
        st["last_ts"] = ts
        st["count"] += 1
        nbytes = len(frame.data) if frame.data else frame.dlc
        st["bytes"] += nbytes
        _total_frames += 1
        _total_bytes_bits += _frame_bits(frame)
        if _start_time is None:
            _start_time = ts
        _last_time = ts

    context.on_frame(on_frame)

    # ---- 1s 刷新 ----
    def refresh():
        duration = (_last_time - _start_time) if (_start_time is not None and _last_time is not None) else 0.0
        load = (_total_bytes_bits / duration / _bitrate * 100.0) if duration > 0 else 0.0
        summary.setText(f"总帧数 {_total_frames}    ID 数 {len(_stats)}    "
                        f"时长 {duration:.1f}s    负载率 {load:.2f}%")

        tree.setSortingEnabled(False)   # 批量更新期间关闭排序
        tree.clear()
        for fid, st in _stats.items():
            periods = st["periods"]
            if periods:
                pmin, pmax = min(periods), max(periods)
                pavg = sum(periods) / len(periods)
                var = sum((p - pavg) ** 2 for p in periods) / len(periods)
                jitter = var ** 0.5
                freq = 1000.0 / pavg if pavg > 0 else 0.0
                cells = [f"0x{fid:X}", str(st["count"]),
                         f"{pmin:.2f}", f"{pavg:.2f}", f"{pmax:.2f}",
                         f"{jitter:.2f}", f"{freq:.1f}", str(st["bytes"])]
            else:
                cells = [f"0x{fid:X}", str(st["count"]),
                         "-", "-", "-", "-", "-", str(st["bytes"])]
            tree.addTopLevelItem(QTreeWidgetItem(cells))
        tree.setSortingEnabled(True)

    timer = QTimer()
    timer.timeout.connect(refresh)
    timer.start(1000)
    refresh()

    # ---- 控制 ----
    def on_toggle():
        global _running
        _running = not _running
        start_btn.setText("继续" if not _running else "暂停")

    def on_clear():
        _reset()
        tree.clear()
        summary.setText("等待数据...")

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出统计 CSV", "bus_statistics.csv",
                                              "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("CAN_ID,帧数,周期min_ms,周期avg_ms,周期max_ms,抖动σ_ms,频率Hz,字节数\n")
                for fid, st in _stats.items():
                    periods = st["periods"]
                    if periods:
                        pmin, pmax = min(periods), max(periods)
                        pavg = sum(periods) / len(periods)
                        jitter = (sum((p - pavg) ** 2 for p in periods) / len(periods)) ** 0.5
                        freq = 1000.0 / pavg if pavg > 0 else 0.0
                        f.write(f"0x{fid:X},{st['count']},{pmin:.3f},{pavg:.3f},{pmax:.3f},"
                                f"{jitter:.3f},{freq:.2f},{st['bytes']}\n")
                    else:
                        f.write(f"0x{fid:X},{st['count']},-,-,-,-,-,{st['bytes']}\n")
            QMessageBox.information(win, "导出成功", f"已导出到:\n{path}")
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_rate_changed(index):
        global _bitrate
        _bitrate = rate_combo.itemData(index) or 500000

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("busStatistics.open", on_open_cmd, "工具: 总线统计分析")

    start_btn.clicked.connect(on_toggle)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)
    rate_combo.currentIndexChanged.connect(on_rate_changed)

    win.show()
    sin.output.append("总线统计插件已加载（订阅实时帧）")


def deactivate():
    global _running
    _running = False
    sin.output.append("总线统计插件已停用")
