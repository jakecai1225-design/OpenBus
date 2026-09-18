# -*- coding: utf-8 -*-
"""can-reverse — Bit-level reverse analysis (heatmap + A/B).

Per-ID flip heatmap, signal-boundary hints, A/B snapshot diff, CSV export.
Subscribe-only.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QMessageBox, QHeaderView, QTabWidget,
    QTableWidget, QTableWidgetItem, QComboBox, QTextEdit,
)

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "can-reverse"

_states = {}
_snapshots = {}
_running = True
_win = None


def _on_frame(frame):
    if not _running:
        return
    data = frame.data
    if not data:
        return
    st = _states.get(frame.id)
    if st is None:
        st = {
            "flips": [0] * 64, "last": None, "count": 0,
            "first": time.time(), "ext": frame.extended,
        }
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
                    st["flips"][byte_i * 8 + bit] += 1


def _recommend_segments(flips):
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
    global _running, _win
    _running = True
    _states.clear()
    _snapshots.clear()

    win = sin.ui.create_window("CAN Reverse (Bit Analysis)")
    win.resize(1040, 680)
    plugin_shell.attach_status_bar(win, "Listening…")
    _win = win

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    snap_a_btn = QPushButton("Snapshot A")
    snap_b_btn = QPushButton("Snapshot B")
    diff_btn = QPushButton("A/B compare")
    export_btn = QPushButton("Export CSV")
    clear_btn = QPushButton("Clear")
    for w in (snap_a_btn, snap_b_btn, diff_btn):
        top.addWidget(w)
    top.addStretch(1)
    top.addWidget(export_btn)
    top.addWidget(clear_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Subscribe-only bit flip heatmap. Capture Snapshot A/B then compare. "
        "Continuous changing bit runs (≥4) are suggested as signal segments."))

    summary = QLabel("Observing bit flips…")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    list_tab = QWidget()
    lv = QVBoxLayout(list_tab)
    tree = QTreeWidget()
    tree.setHeaderLabels([
        "ID", "Frames", "Active bytes", "Total flips",
        "Suggested segments (start-end|len)"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    lv.addWidget(tree, 1)
    tabs.addTab(list_tab, "ID activity")

    matrix_tab = QWidget()
    mv = QVBoxLayout(matrix_tab)
    combo = QComboBox()
    matrix = QTableWidget()
    mv.addWidget(combo)
    mv.addWidget(matrix, 1)
    tabs.addTab(matrix_tab, "Bit heatmap")

    diff_tab = QWidget()
    dv = QVBoxLayout(diff_tab)
    diff_view = QTextEdit()
    diff_view.setReadOnly(True)
    diff_view.setPlaceholderText("Run A/B compare to see differing bits here.")
    dv.addWidget(diff_view, 1)
    tabs.addTab(diff_tab, "A/B diff")

    def _persist_meta():
        state_store.save_state(PLUGIN_ID, {
            "snap_a_ids": len(_snapshots.get("A") or {}),
            "snap_b_ids": len(_snapshots.get("B") or {}),
        })

    def _refresh():
        summary.setText("IDs %d · frames %d · snap A=%d B=%d" % (
            len(_states),
            sum(s["count"] for s in _states.values()),
            len(_snapshots.get("A") or {}),
            len(_snapshots.get("B") or {}),
        ))
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
            active_bytes = sum(
                1 for b in range(8) if any(flips[b * 8:(b + 1) * 8]))
            segs = _recommend_segments(flips)
            seg_text = "; ".join(
                "%d-%d|%d" % (a, b, b - a + 1) for a, b, _ in segs) or "-"
            tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid, str(st["count"]), str(active_bytes),
                str(sum(flips)), seg_text]))
        _refresh_matrix()

    def _refresh_matrix(_idx=None):
        cid = combo.currentData()
        if cid is None or cid not in _states:
            matrix.setRowCount(0)
            return
        st = _states[cid]
        max_flip = max(st["flips"]) or 1
        matrix.setRowCount(8)
        matrix.setColumnCount(8)
        matrix.setVerticalHeaderLabels(["byte%d" % b for b in range(8)])
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

    def _on_snap(name):
        snap = {}
        for cid, st in _states.items():
            if st["last"] is not None:
                snap[cid] = bytes(st["last"])
        _snapshots[name] = snap
        _persist_meta()
        plugin_shell.set_status(
            win, "Snapshot %s: %d IDs" % (name, len(snap)), 3000)

    def _on_diff():
        if "A" not in _snapshots or "B" not in _snapshots:
            QMessageBox.information(win, "Compare", "Take Snapshot A and B first")
            return
        a, b = _snapshots["A"], _snapshots["B"]
        lines = []
        for cid in sorted(set(a.keys()) | set(b.keys())):
            da, db = a.get(cid), b.get(cid)
            if da is None or db is None:
                lines.append("0x%X — only in %s" % (cid, "A" if da else "B"))
                continue
            if da == db:
                continue
            bits = []
            for byte_i in range(min(len(da), len(db))):
                d = da[byte_i] ^ db[byte_i]
                for bit in range(8):
                    if d & (0x80 >> bit):
                        bits.append(byte_i * 8 + bit)
            extra = ""
            if len(da) != len(db):
                extra = " (DLC %d vs %d)" % (len(da), len(db))
            lines.append(
                "0x%X — differing bits: %s%s"
                % (cid, ", ".join(str(x) for x in bits[:24])
                   + ("…" if len(bits) > 24 else ""), extra))
        if not lines:
            diff_view.setPlainText("Snapshots A and B are identical.")
            plugin_shell.set_status(win, "A/B identical", 2500)
        else:
            diff_view.setPlainText(
                "%d difference(s):\n\n%s" % (len(lines), "\n".join(lines)))
            tabs.setCurrentIndex(2)
            plugin_shell.set_status(win, "A/B: %d diffs" % len(lines), 3000)

    def _on_export():
        if not _states:
            QMessageBox.information(win, "Export", "No data yet")
            return
        rows = []
        for cid in sorted(_states.keys()):
            st = _states[cid]
            rows.append(
                ["0x%X" % cid, st["count"]] + list(st["flips"]))
        headers = ["ID", "Frames"] + ["bit%d" % i for i in range(64)]
        path = plugin_shell.export_csv(win, headers, rows, "bit_analysis.csv")
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    def _on_clear():
        _states.clear()
        _snapshots.clear()
        tree.clear()
        matrix.setRowCount(0)
        combo.clear()
        diff_view.clear()
        summary.setText("Observing bit flips…")
        _persist_meta()
        plugin_shell.set_status(win, "Cleared", 2000)

    snap_a_btn.clicked.connect(lambda: _on_snap("A"))
    snap_b_btn.clicked.connect(lambda: _on_snap("B"))
    diff_btn.clicked.connect(_on_diff)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)
    combo.currentIndexChanged.connect(_refresh_matrix)
    plugin_shell.bind_shortcut(win, "Ctrl+E", _on_export)

    context.on_frame(_on_frame)
    context.register_command(
        "canReverse.open", plugin_shell.bind_raise(win), "Tools: Reverse Bits")

    timer = QTimer(win)
    timer.timeout.connect(_refresh)
    timer.start(1000)

    win.show()
    sin.output.append("can-reverse loaded (heatmap + A/B compare)")


def deactivate():
    global _running, _win
    _running = False
    _win = None
    try:
        sin.output.append("can-reverse deactivated")
    except Exception:
        pass
