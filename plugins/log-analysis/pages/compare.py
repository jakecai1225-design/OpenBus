# -*- coding: utf-8 -*-
"""Compare workspace — dual-source CAN frame compare (live or CSV)."""

from __future__ import annotations

import csv
import os
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QDoubleSpinBox, QFileDialog, QHBoxLayout, QHeaderView, QLabel,
    QMessageBox, QPushButton, QSpinBox, QTreeWidget, QTreeWidgetItem,
    QVBoxLayout, QWidget,
)

from _shared import plugin_shell, state_store

PAGE_STATE = "compare.json"


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


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    buffers = {"A": {}, "B": {}}
    capturing = {"side": None}
    results = {"rows": []}

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

    def _persist():
        state_store.save_state("log-analysis", {
            "capture_s": dur_spin.value(),
            "period_tol": tol_spin.value(),
        }, PAGE_STATE)

    def _set_status(side, text):
        if side == "A":
            status_a.setText(text)
        else:
            status_b.setText(text)

    def _on_frame(frame):
        side = capturing["side"]
        if side is None:
            return
        _feed(buffers[side], frame)

    def _start_capture(side):
        if capturing["side"] is not None:
            QMessageBox.information(parent, "Capture", "Already capturing")
            return
        buffers[side] = {}
        capturing["side"] = side
        _set_status(side, "%s: capturing…" % side)
        plugin_shell.set_status(parent, "Capturing %s…" % side)
        ms = dur_spin.value() * 1000

        def _finish():
            capturing["side"] = None
            n_ids = len(buffers[side])
            n_frames = sum(st["count"] for st in buffers[side].values())
            _set_status(side, "%s: %d IDs / %d frames" % (side, n_ids, n_frames))
            log_fn("RX", "-", b"", "Compare capture %s: %d IDs" % (side, n_ids))
            plugin_shell.set_status(
                parent, "Capture %s done (%d IDs)" % (side, n_ids), 3000)
            _persist()

        QTimer.singleShot(ms, _finish)

    def _load_csv(side):
        path, _ = QFileDialog.getOpenFileName(
            parent, "Load CSV for %s" % side, session.start_dir(),
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
            QMessageBox.warning(parent, "Load failed", str(e))
            return
        buffers[side] = buf
        session.note_log_path(path)
        _set_status(side, "%s: %d IDs (CSV)" % (side, len(buf)))
        plugin_shell.set_status(parent, "Loaded %s for %s" % (path, side), 3000)

    def _on_compare():
        a, b = buffers["A"], buffers["B"]
        if not a or not b:
            QMessageBox.information(
                parent, "Compare", "Capture or load both A and B first")
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
        log_fn("RX", "-", b"", "Compare: %d differences" % len(rows))
        plugin_shell.set_status(
            parent, "Compared — %d differences" % len(rows), 3000)

    def _on_export():
        if not results["rows"]:
            QMessageBox.information(parent, "Export", "Run Compare first")
            return
        path = plugin_shell.export_csv(
            parent,
            ["Kind", "ID", "Detail"],
            [list(r) for r in results["rows"]],
            "compare_diff.csv",
        )
        if path:
            session.note_export_path(path)
            plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    cap_a_btn.clicked.connect(lambda: _start_capture("A"))
    cap_b_btn.clicked.connect(lambda: _start_capture("B"))
    load_a_btn.clicked.connect(lambda: _load_csv("A"))
    load_b_btn.clicked.connect(lambda: _load_csv("B"))
    cmp_btn.clicked.connect(_on_compare)
    export_btn.clicked.connect(_on_export)
    dur_spin.valueChanged.connect(lambda _v: _persist())
    tol_spin.valueChanged.connect(lambda _v: _persist())

    session.on_bus_frame(_on_frame)

    saved = state_store.load_state("log-analysis", PAGE_STATE) or {}
    if saved.get("capture_s"):
        dur_spin.setValue(int(saved["capture_s"]))
    if saved.get("period_tol"):
        tol_spin.setValue(float(saved["period_tol"]))

    return root
