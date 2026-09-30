# -*- coding: utf-8 -*-
"""Replay workspace — play an ASC slice onto the bus."""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

import sin
from _shared import vscode_theme

from core.asc_slice import read_asc


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "ASC replay",
        "Play a Vector-style ASC file in timestamp order. Stop aborts immediately.")
    row = QHBoxLayout()
    open_btn = QPushButton("Open ASC…")
    start_btn = QPushButton("Start")
    stop_btn = QPushButton("Stop")
    speed = QComboBox()
    speed.addItem("1x", 1.0)
    speed.addItem("2x", 2.0)
    speed.addItem("5x", 5.0)
    speed.addItem("10x", 10.0)
    status = QLabel("No file")
    row.addWidget(open_btn)
    row.addWidget(start_btn)
    row.addWidget(stop_btn)
    row.addWidget(QLabel("Speed"))
    row.addWidget(speed)
    row.addStretch(1)
    row.addWidget(status)
    body.addLayout(row)
    layout.addWidget(card)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Time s", "CH", "ID", "DLC", "Data"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    frames = []
    cursor = {"i": 0}
    origin = {"log": 0.0, "wall": 0.0}
    running = {"v": False}

    timer = QTimer(root)
    timer.setInterval(20)

    def _fill_tree():
        tree.clear()
        show = frames[:400]
        for ts, ch, cid, data, _ext in show:
            tree.addTopLevelItem(QTreeWidgetItem([
                "%.6f" % ts,
                str(ch),
                "0x%X" % cid,
                str(len(data)),
                " ".join("%02X" % b for b in data),
            ]))
        extra = ""
        if len(frames) > len(show):
            extra = " (showing %d)" % len(show)
        status.setText("%d frames%s" % (len(frames), extra))

    def _on_open():
        path, _ = QFileDialog.getOpenFileName(
            root, "Open ASC", "", "ASC (*.asc);;All files (*)")
        if not path:
            return
        _stop()
        loaded, skipped = read_asc(path)
        frames.clear()
        frames.extend(loaded)
        cursor["i"] = 0
        _fill_tree()
        log_fn("REPLAY", "Loaded %d frames from %s (skipped %d)" % (
            len(frames), path, skipped))

    def _send(frame):
        _ts, _ch, cid, data, ext = frame
        try:
            sin.frames.send(cid, data, extended=ext)
        except TypeError:
            sin.frames.send(cid, data)

    def _tick():
        if not running["v"] or not frames:
            return
        rate = float(speed.currentData() or 1.0)
        elapsed = (time.monotonic() - origin["wall"]) * rate
        sent = 0
        while cursor["i"] < len(frames) and sent < 64:
            ts = frames[cursor["i"]][0]
            if ts - origin["log"] > elapsed:
                break
            _send(frames[cursor["i"]])
            cursor["i"] += 1
            sent += 1
        status.setText("%d / %d" % (cursor["i"], len(frames)))
        if cursor["i"] >= len(frames):
            _stop()
            log_fn("REPLAY", "Replay finished (%d frames)" % len(frames))

    def _start():
        if not frames:
            status.setText("Open an ASC file first")
            return
        cursor["i"] = 0
        origin["log"] = frames[0][0]
        origin["wall"] = time.monotonic()
        running["v"] = True
        timer.start()
        log_fn("REPLAY", "Start x%g" % float(speed.currentData() or 1.0))

    def _stop():
        running["v"] = False
        timer.stop()
        if frames:
            status.setText("Stopped %d / %d" % (cursor["i"], len(frames)))

    timer.timeout.connect(_tick)
    open_btn.clicked.connect(_on_open)
    start_btn.clicked.connect(_start)
    stop_btn.clicked.connect(_stop)
    return root
