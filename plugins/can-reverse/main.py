# -*- coding: utf-8 -*-
"""can-reverse 插件 — CAN 逆向位分析（SavvyCAN bit-gravity 风格）
功能：
- 逐 ID 位活动矩阵：64 位翻转热力图（自定义表格渲染）
- 变化位统计（每位翻转次数，稳定位高亮）
- 信号边界推荐：连续变化段 + 字节对齐边界
- 快照对比：A/B 两快照差异位
- 纯订阅只读（激活即订阅，安全）
依赖: pip install PyQt6
"""

import time

from PyQt6.QtCore import QTimer

import sin

_states = {}     # id -> {"flips": [64], "last": bytes or None, "count", "first", "ext"}
_snapshots = {}  # name -> {id: bytes}
_running = True


def _on_frame(frame):
    if not _running:
        return
    data = frame.data
    if not data:
        return
    st = _states.get(frame.id)
    if st is None:
        st = {"flips": [0] * 64, "last": None, "count": 0,
              "first": time.time(), "ext": frame.extended,
              "changes": [0] * 64}
        _states[frame.id] = st
    st["count"] += 1
    if st["last"] is None:
        st["last"] = data
        return
    prev = st["last"]
    st["last"] = data
    n = max(len(prev), len(data))
    for byte_i in range(min(n, 8)):
        a = prev[byte_i] if byte_i < len(prev) else 0
        b = data[byte_i] if byte_i < len(data) else 0
        if a != b:
            diff = a ^ b
            for bit in range(8):
                if diff & (0x80 >> bit):
                    idx = byte_i * 8 + bit
                    st["flips"][idx] += 1


def _recommend_segments(flips):
    """连续变化段推断（≥4 连续变化位合并为候选信号边界）"""
    segs = []
    start = None
    for i, f in enumerate(flips):
        if f > 0:
            if start is None:
                start = i
        else:
            if start is not None:
                if i - start >= 4:
                    segs.append((start, i - 1, sum(flips[start:i])))
                start = None
    if start is not None and 64 - start >= 4:
        segs.append((start, 63, sum(flips[start:64])))
    return segs


def activate(context):
    global _running
    _running = True
    _states.clear()
    _snapshots.clear()

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QTabWidget, QTableWidget, QTableWidgetItem,
            QInputDialog, QComboBox
        )
        from PyQt6.QtGui import QColor
        from PyQt6.QtCore import Qt
    except ImportError:
        sin.output.append("逆向位分析插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN 逆向位分析")
    win.resize(1040, 660)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    snap_a_btn = QPushButton("快照 A")
    snap_b_btn = QPushButton("快照 B")
    diff_btn = QPushButton("A/B 差异对比")
    export_btn = QPushButton("导出 CSV")
    clear_btn = QPushButton("清零")
    top.addWidget(snap_a_btn)
    top.addWidget(snap_b_btn)
    top.addWidget(diff_btn)
    top.addStretch(1)
    top.addWidget(export_btn)
    top.addWidget(clear_btn)
    layout.addLayout(top)

    summary = QLabel("订阅实时帧中（观察位翻转以推断信号边界）...")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    # Tab 1: ID 列表 + 推荐边界
    list_tab = QWidget()
    lv = QVBoxLayout(list_tab)
    tree = QTreeWidget()
    tree.setHeaderLabels(["ID", "帧数", "变化字节数", "总翻转位", "推荐信号段（起点-终点|长度）"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    lv.addWidget(tree, 1)
    tabs.addTab(list_tab, "ID 位活动")

    # Tab 2: 位热力矩阵
    matrix_tab = QWidget()
    mv = QVBoxLayout(matrix_tab)
    combo = QComboBox()
    matrix = QTableWidget()
    mv.addWidget(combo)
    mv.addWidget(matrix, 1)
    tabs.addTab(matrix_tab, "位翻转热力图")

    def _refresh():
        summary.setText("ID %d 个 · 总帧 %d" % (
            len(_states), sum(s["count"] for s in _states.values())))
        tree.clear()
        current = combo.currentData()
        combo.blockSignals(True)
        combo.clear()
        for cid in sorted(_states.keys()):
            combo.addItem("0x%X" % cid, cid)
        if current in _states:
            combo.setCurrentIndex(combo.findData(current))
        combo.blockSignals(False)

        for cid in sorted(_states.keys()):
            st = _states[cid]
            flips = st["flips"]
            active_bytes = sum(1 for b in range(8) if any(flips[b * 8:(b + 1) * 8]))
            total_flips = sum(flips)
            segs = _recommend_segments(flips)
            seg_text = "; ".join("%d-%d|%d位" % (a, b, b - a + 1) for a, b, _ in segs) or "-"
            tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid, str(st["count"]), str(active_bytes),
                str(total_flips), seg_text]))

    def _refresh_matrix(cid=None):
        if cid is None:
            cid = combo.currentData()
        if cid is None or cid not in _states:
            matrix.setRowCount(0)
            return
        st = _states[cid]
        max_flip = max(st["flips"]) or 1
        matrix.setRowCount(8)
        matrix.setColumnCount(8)
        matrix.setVerticalHeaderLabels(["字节%d" % b for b in range(8)])
        matrix.setHorizontalHeaderLabels(["bit%d" % (7 - i) for i in range(8)])
        for byte_i in range(8):
            for bit in range(8):
                idx = byte_i * 8 + bit
                f = st["flips"][idx]
                ratio = f / max_flip
                if f == 0:
                    color = QColor("#f5f5f5")
                elif ratio > 0.7:
                    color = QColor("#c62828")
                elif ratio > 0.4:
                    color = QColor("#ef6c00")
                else:
                    color = QColor("#fff59d")
                item = QTableWidgetItem(str(f))
                item.setBackground(color)
                item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                matrix.setItem(byte_i, bit, item)

    def _on_combo(cid):
        _refresh_matrix(cid)

    def _on_snap(name):
        snap = {}
        for cid, st in _states.items():
            if st["last"] is not None:
                snap[cid] = st["last"]
        _snapshots[name] = snap
        QMessageBox.information(win, "快照已保存",
                                "快照 %s: %d 个 ID" % (name, len(snap)))

    def _on_diff():
        if "A" not in _snapshots or "B" not in _snapshots:
            QMessageBox.information(win, "提示", "请先保存快照 A 和快照 B")
            return
        a, b = _snapshots["A"], _snapshots["B"]
        diffs = []
        for cid in sorted(set(a.keys()) | set(b.keys())):
            da = a.get(cid)
            db = b.get(cid)
            if da is None or db is None:
                diffs.append((cid, "仅 %s" % ("A" if da else "B")))
                continue
            if da != db:
                bits = []
                for byte_i in range(min(len(da), len(db))):
                    d = da[byte_i] ^ db[byte_i]
                    for bit in range(8):
                        if d & (0x80 >> bit):
                            bits.append(byte_i * 8 + bit)
                diffs.append((cid, "差异位: %s" % (
                    ", ".join(str(b) for b in bits[:16]) + ("..." if len(bits) > 16 else ""))))
        if not diffs:
            QMessageBox.information(win, "对比结果", "两快照完全一致")
            return
        msg = "\n".join("0x%X → %s" % (c, d) for c, d in diffs[:30])
        QMessageBox.information(win, "A/B 差异（%d 项）" % len(diffs), msg)

    def _on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出位分析", "bit_analysis.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("ID,帧数," + ",".join("bit%d" % i for i in range(64)) + "\n")
                for cid in sorted(_states.keys()):
                    st = _states[cid]
                    f.write("0x%X,%d,%s\n" % (cid, st["count"],
                             ",".join(str(x) for x in st["flips"])))
            QMessageBox.information(win, "导出成功", "已导出 %d ID" % len(_states))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("canReverse.open", _on_open_cmd, "分析: 逆向位分析")

    snap_a_btn.clicked.connect(lambda: _on_snap("A"))
    snap_b_btn.clicked.connect(lambda: _on_snap("B"))
    diff_btn.clicked.connect(_on_diff)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(lambda: (_states.clear(), _refresh()))
    combo.currentIndexChanged.connect(_on_combo)

    timer = QTimer()
    timer.timeout.connect(_refresh)
    timer.start(1000)

    win.show()
    sin.output.append("逆向位分析插件已加载（订阅实时帧，位翻转统计，只读安全）")


def deactivate():
    global _running
    _running = False
    sin.output.append("逆向位分析插件已停用")
