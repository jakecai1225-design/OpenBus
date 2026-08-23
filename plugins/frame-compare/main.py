# -*- coding: utf-8 -*-
"""frame-compare 插件 — 双源报文比对（SavvyCAN compare 风格）
功能：
- 双缓冲采集：A / B 各捕获 N 秒实时帧（或从 CSV 加载）
- 逐 ID / 逐字节比对：缺帧 / 多帧 / 载荷差异 / 周期差异统计
- 差异清单（Top 差异）+ CSV 导出
- 纯订阅只读（捕获不发送）
依赖: pip install PyQt6
"""

import csv
import time

from PyQt6.QtCore import QTimer

import sin

_buffers = {"A": None, "B": None}    # {id: {"data": bytes, "count": n, "periods": [ms]}}
_capturing = None
_capture_start = 0.0
_running = True


def _feed(buf, frame):
    st = buf.get(frame.id)
    now = time.time()
    if st is None:
        buf[frame.id] = {"data": frame.data, "count": 1, "last": now, "periods": []}
    else:
        dt = (now - st["last"]) * 1000.0
        if 0 < dt < 10000:
            st["periods"].append(dt)
            if len(st["periods"]) > 32:
                del st["periods"][:16]
        st["last"] = now
        st["count"] += 1
        st["data"] = frame.data


def _on_frame(frame):
    if not _running or _capturing is None:
        return
    _feed(_buffers[_capturing], frame)


def compare(a, b, period_tol_pct=20.0):
    """返回 (rows, stats)
    rows: (kind, id, detail)"""
    rows = []
    stats = {"only_a": 0, "only_b": 0, "data_diff": 0, "period_diff": 0, "same": 0}
    for cid in sorted(set(a.keys()) | set(b.keys())):
        sa = a.get(cid)
        sb = b.get(cid)
        if sa and not sb:
            rows.append(("仅 A 有", "0x%X" % cid, "A 出现 %d 次，B 缺失" % sa["count"]))
            stats["only_a"] += 1
        elif sb and not sa:
            rows.append(("仅 B 有", "0x%X" % cid, "B 出现 %d 次，A 缺失" % sb["count"]))
            stats["only_b"] += 1
        else:
            notes = []
            if sa["data"] != sb["data"]:
                diff_bytes = []
                for i in range(max(len(sa["data"]), len(sb["data"]))):
                    da = sa["data"][i] if i < len(sa["data"]) else None
                    db = sb["data"][i] if i < len(sb["data"]) else None
                    if da != db:
                        diff_bytes.append("%d: %s→%s" % (
                            i, "%02X" % da if da is not None else "--",
                            "%02X" % db if db is not None else "--"))
                notes.append("载荷差异 [%s]" % ", ".join(diff_bytes[:8]))
                stats["data_diff"] += 1
            pa = sum(sa["periods"]) / len(sa["periods"]) if sa["periods"] else 0
            pb = sum(sb["periods"]) / len(sb["periods"]) if sb["periods"] else 0
            if pa and pb and abs(pa - pb) / max(pa, pb) * 100.0 > period_tol_pct:
                notes.append("周期差异 %.1fms vs %.1fms" % (pa, pb))
                stats["period_diff"] += 1
            if notes:
                rows.append(("差异", "0x%X" % cid, "; ".join(notes)))
            else:
                stats["same"] += 1
    return rows, stats


def activate(context):
    global _capturing, _running
    _capturing = None
    _running = True
    _buffers["A"] = {}
    _buffers["B"] = {}

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QSpinBox, QDoubleSpinBox
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("双源比对插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("双源报文比对")
    win.resize(960, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    cap_a_btn = QPushButton("捕获 A（5s）")
    cap_b_btn = QPushButton("捕获 B（5s）")
    load_a_btn = QPushButton("从 CSV 加载 A…")
    load_b_btn = QPushButton("从 CSV 加载 B…")
    cmp_btn = QPushButton("执行比对")
    export_btn = QPushButton("导出差异 CSV")
    top.addWidget(cap_a_btn)
    top.addWidget(load_a_btn)
    top.addWidget(cap_b_btn)
    top.addWidget(load_b_btn)
    top.addStretch(1)
    top.addWidget(cmp_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    status_a = QLabel("A: 空")
    status_b = QLabel("B: 空")
    tol_spin = QDoubleSpinBox()
    tol_spin.setRange(1, 100)
    tol_spin.setValue(20.0)
    tol_spin.setSuffix(" %")
    layout.addWidget(status_a)
    layout.addWidget(status_b)

    tol_row = QHBoxLayout()
    tol_row.addWidget(QLabel("周期差异容忍:"))
    tol_row.addWidget(tol_spin)
    tol_row.addStretch(1)
    layout.addLayout(tol_row)

    summary = QLabel("捕获或加载 A/B 两路数据后比对")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["类别", "ID", "详情"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    results = {"rows": []}

    def _start_capture(side):
        global _capturing, _capture_start
        _buffers[side] = {}
        _capturing = side
        _capture_start = time.time()
        if side == "A":
            status_a.setText("A: 捕获中…")
        else:
            status_b.setText("B: 捕获中…")

        def _finish():
            global _capturing
            _capturing = None
            n_ids = len(_buffers[side])
            n_frames = sum(st["count"] for st in _buffers[side].values())
            text = "A" if side == "A" else "B"
            if side == "A":
                status_a.setText("A: %d ID / %d 帧" % (n_ids, n_frames))
            else:
                status_b.setText("B: %d ID / %d 帧" % (n_ids, n_frames))

        QTimer.singleShot(5000, _finish)

    def _load_csv(side):
        path, _ = QFileDialog.getOpenFileName(win, "加载 CSV 日志（%s 侧）" % side, "",
                                              "CSV (*.csv);;所有文件 (*)")
        if not path:
            return
        buf = {}
        try:
            with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
                reader = csv.reader(f)
                for row in reader:
                    if len(row) < 4:
                        continue
                    try:
                        cid = int(row[1], 0)
                        data = bytes.fromhex(row[-1].replace(" ", "")) if row[-1] else b""
                        buf[cid] = {"data": data, "count": 1, "periods": []}
                    except (ValueError, IndexError):
                        continue
        except OSError as e:
            QMessageBox.warning(win, "加载失败", str(e))
            return
        _buffers[side] = buf
        if side == "A":
            status_a.setText("A: %d ID（CSV）" % len(buf))
        else:
            status_b.setText("B: %d ID（CSV）" % len(buf))

    def _on_compare():
        a, b = _buffers["A"], _buffers["B"]
        if not a or not b:
            QMessageBox.information(win, "提示", "请先捕获或加载 A 和 B 两路数据")
            return
        rows, stats = compare(a, b, tol_spin.value())
        results["rows"] = rows
        tree.clear()
        colors = {"仅 A 有": QColor("#ef6c00"), "仅 B 有": QColor("#1565c0"),
                  "差异": QColor("#c62828")}
        for kind, cid, detail in rows:
            item = QTreeWidgetItem([kind, cid, detail])
            item.setForeground(0, colors.get(kind))
            tree.addTopLevelItem(item)
        summary.setText("比对: 一致 %d · 仅A %d · 仅B %d · 载荷差 %d · 周期差 %d" % (
            stats["same"], stats["only_a"], stats["only_b"],
            stats["data_diff"], stats["period_diff"]))

    def _on_export():
        if not results["rows"]:
            QMessageBox.information(win, "提示", "请先执行比对")
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出差异", "compare_diff.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = csv.writer(f)
                w.writerow(["类别", "ID", "详情"])
                for row in results["rows"]:
                    w.writerow(row)
            QMessageBox.information(win, "导出成功", "已导出 %d 条" % len(results["rows"]))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("frameCompare.open", _on_open_cmd, "分析: 双源比对")

    cap_a_btn.clicked.connect(lambda: _start_capture("A"))
    cap_b_btn.clicked.connect(lambda: _start_capture("B"))
    load_a_btn.clicked.connect(lambda: _load_csv("A"))
    load_b_btn.clicked.connect(lambda: _load_csv("B"))
    cmp_btn.clicked.connect(_on_compare)
    export_btn.clicked.connect(_on_export)

    win.show()
    sin.output.append("双源比对插件已加载（订阅捕获 + CSV 加载，只读安全）")


def deactivate():
    global _running, _capturing
    _running = False
    _capturing = None
    sin.output.append("双源比对插件已停用")
