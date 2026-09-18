# -*- coding: utf-8 -*-
"""frame-compare — Dual-source CAN frame compare.

Capture A/B from the live bus or load CSV; ID/payload/period diff + CSV export.
Subscribe-only during capture.
"""

from __future__ import annotations

import csv
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
    QHeaderView, QSpinBox, QDoubleSpinBox,
)

import sin
from _shared import plugin_shell, state_store

PLUGIN_ID = "frame-compare"

_buffers = {"A": None, "B": None}
_capturing = None
_running = True
_win = None


def _feed(buf, frame):
    st = buf.get(frame.id)
    now = time.time()
    if st is None:
        buf[frame.id] = {
            "data": bytes(frame.data), "count": 1, "last": now, "periods": []}
    else:
        dt = (now - st["last"]) * 1000.0
        if 0 < dt < 10000:
            st["periods"].append(dt)
            if len(st["periods"]) > 32:
                del st["periods"][:16]
        st["last"] = now
        st["count"] += 1
        st["data"] = bytes(frame.data)


def _on_frame(frame):
    if not _running or _capturing is None:
        return
    _feed(_buffers[_capturing], frame)


def compare(a, b, period_tol_pct=20.0):
    rows = []
    stats = {
        "only_a": 0, "only_b": 0, "data_diff": 0, "period_diff": 0, "same": 0}
    for cid in sorted(set(a.keys()) | set(b.keys())):
        sa, sb = a.get(cid), b.get(cid)
        if sa and not sb:
            rows.append((
                "Only A", "0x%X" % cid,
                "A seen %d times; missing in B" % sa["count"]))
            stats["only_a"] += 1
        elif sb and not sa:
            rows.append((
                "Only B", "0x%X" % cid,
                "B seen %d times; missing in A" % sb["count"]))
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
                            i,
                            "%02X" % da if da is not None else "--",
                            "%02X" % db if db is not None else "--"))
                notes.append("payload [%s]" % ", ".join(diff_bytes[:8]))
                stats["data_diff"] += 1
            pa = (
                sum(sa["periods"]) / len(sa["periods"]) if sa["periods"] else 0)
            pb = (
                sum(sb["periods"]) / len(sb["periods"]) if sb["periods"] else 0)
            if pa and pb and abs(pa - pb) / max(pa, pb) * 100.0 > period_tol_pct:
                notes.append("period %.1f ms vs %.1f ms" % (pa, pb))
                stats["period_diff"] += 1
            if notes:
                rows.append(("Diff", "0x%X" % cid, "; ".join(notes)))
            else:
                stats["same"] += 1
    return rows, stats


def activate(context):
    global _capturing, _running, _win
    _capturing = None
    _running = True
    _buffers["A"] = {}
    _buffers["B"] = {}

    win = sin.ui.create_window("Frame Compare")
    win.resize(980, 640)
    plugin_shell.attach_status_bar(win, "Ready — capture or load A and B")
    _win = win

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    dur_spin = QSpinBox()
    dur_spin.setRange(1, 60)
    dur_spin.setValue(5)
    dur_spin.setSuffix(" s")
    top.addWidget(QLabel("Capture:"))
    top.addWidget(dur_spin)
    cap_a_btn = QPushButton("Capture A")
    cap_b_btn = QPushButton("Capture B")
    load_a_btn = QPushButton("Load A CSV…")
    load_b_btn = QPushButton("Load B CSV…")
    cmp_btn = QPushButton("Compare")
    export_btn = QPushButton("Export CSV")
    for w in (cap_a_btn, load_a_btn, cap_b_btn, load_b_btn):
        top.addWidget(w)
    top.addStretch(1)
    top.addWidget(cmp_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Capture live frames into A/B (subscribe-only) or load CSV logs. "
        "Compare finds missing IDs, payload diffs, and period drift."))

    status_a = QLabel("A: empty")
    status_b = QLabel("B: empty")
    layout.addWidget(status_a)
    layout.addWidget(status_b)

    tol_row = QHBoxLayout()
    tol_spin = QDoubleSpinBox()
    tol_spin.setRange(1, 100)
    tol_spin.setValue(20.0)
    tol_spin.setSuffix(" %")
    tol_row.addWidget(QLabel("Period tolerance:"))
    tol_row.addWidget(tol_spin)
    tol_row.addStretch(1)
    layout.addLayout(tol_row)

    summary = QLabel("Capture or load A and B, then Compare")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Kind", "ID", "Detail"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    results = {"rows": []}

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "capture_s": dur_spin.value(),
            "period_tol": tol_spin.value(),
        })

    def _set_status(side, text):
        if side == "A":
            status_a.setText(text)
        else:
            status_b.setText(text)

    def _start_capture(side):
        global _capturing
        if _capturing is not None:
            QMessageBox.information(win, "Capture", "Already capturing")
            return
        _buffers[side] = {}
        _capturing = side
        _set_status(side, "%s: capturing…" % side)
        plugin_shell.set_status(win, "Capturing %s…" % side)
        ms = dur_spin.value() * 1000

        def _finish():
            global _capturing
            _capturing = None
            n_ids = len(_buffers[side])
            n_frames = sum(st["count"] for st in _buffers[side].values())
            _set_status(side, "%s: %d IDs / %d frames" % (side, n_ids, n_frames))
            plugin_shell.set_status(
                win, "Capture %s done (%d IDs)" % (side, n_ids), 3000)
            _persist()

        QTimer.singleShot(ms, _finish)

    def _load_csv(side):
        path, _ = QFileDialog.getOpenFileName(
            win, "Load CSV for %s" % side, "",
            "CSV (*.csv);;All files (*)")
        if not path:
            return
        buf = {}
        try:
            with open(path, "r", encoding="utf-8-sig", errors="replace") as f:
                reader = csv.reader(f)
                for row in reader:
                    if len(row) < 2:
                        continue
                    try:
                        # Support timestamp,id,...data or id,...data
                        if len(row) >= 4:
                            cid = int(row[1], 0)
                            data_hex = row[-1]
                        else:
                            cid = int(row[0], 0)
                            data_hex = row[-1]
                        data = (
                            bytes.fromhex(data_hex.replace(" ", ""))
                            if data_hex else b"")
                        if cid in buf:
                            buf[cid]["count"] += 1
                            buf[cid]["data"] = data
                        else:
                            buf[cid] = {
                                "data": data, "count": 1, "periods": []}
                    except (ValueError, IndexError):
                        continue
        except OSError as e:
            QMessageBox.warning(win, "Load failed", str(e))
            return
        _buffers[side] = buf
        _set_status(side, "%s: %d IDs (CSV)" % (side, len(buf)))
        plugin_shell.set_status(win, "Loaded %s for %s" % (path, side), 3000)

    def _on_compare():
        a, b = _buffers["A"], _buffers["B"]
        if not a or not b:
            QMessageBox.information(
                win, "Compare", "Capture or load both A and B first")
            return
        rows, stats = compare(a, b, tol_spin.value())
        results["rows"] = rows
        tree.clear()
        colors = {
            "Only A": QColor("#ef6c00"),
            "Only B": QColor("#1565c0"),
            "Diff": QColor("#c62828"),
        }
        for kind, cid, detail in rows:
            item = QTreeWidgetItem([kind, cid, detail])
            if kind in colors:
                item.setForeground(0, colors[kind])
            tree.addTopLevelItem(item)
        summary.setText(
            "Same %d · Only A %d · Only B %d · payload %d · period %d"
            % (stats["same"], stats["only_a"], stats["only_b"],
               stats["data_diff"], stats["period_diff"]))
        _persist()
        plugin_shell.set_status(
            win, "Compared — %d differences" % len(rows), 3000)

    def _on_export():
        if not results["rows"]:
            QMessageBox.information(win, "Export", "Run Compare first")
            return
        path = plugin_shell.export_csv(
            win,
            ["Kind", "ID", "Detail"],
            [list(r) for r in results["rows"]],
            "compare_diff.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    cap_a_btn.clicked.connect(lambda: _start_capture("A"))
    cap_b_btn.clicked.connect(lambda: _start_capture("B"))
    load_a_btn.clicked.connect(lambda: _load_csv("A"))
    load_b_btn.clicked.connect(lambda: _load_csv("B"))
    cmp_btn.clicked.connect(_on_compare)
    export_btn.clicked.connect(_on_export)
    dur_spin.valueChanged.connect(lambda _v: _persist())
    tol_spin.valueChanged.connect(lambda _v: _persist())
    plugin_shell.bind_shortcut(win, "Ctrl+E", _on_export)

    context.on_frame(_on_frame)
    context.register_command(
        "frameCompare.open", plugin_shell.bind_raise(win), "Tools: Frame Compare")

    saved = state_store.load_state(PLUGIN_ID, default={}) or {}
    if saved.get("capture_s"):
        dur_spin.setValue(int(saved["capture_s"]))
    if saved.get("period_tol"):
        tol_spin.setValue(float(saved["period_tol"]))

    win.show()
    sin.output.append("frame-compare loaded (dual source + CSV)")


def deactivate():
    global _running, _capturing, _win
    _running = False
    _capturing = None
    _win = None
    try:
        sin.output.append("frame-compare deactivated")
    except Exception:
        pass
