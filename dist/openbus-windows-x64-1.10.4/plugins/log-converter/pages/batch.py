# -*- coding: utf-8 -*-
"""Batch workspace — convert many logs to one target format."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView, QComboBox, QFileDialog, QHBoxLayout, QHeaderView,
    QLabel, QLineEdit, QMessageBox, QProgressBar, QTableWidget,
    QTableWidgetItem, QVBoxLayout, QWidget,
)

from _shared import plugin_shell
from core.convert_runner import cancel_active, start_convert
from formats import (
    ENGINE_FORMATS, OPEN_FILTER, detect_format, engine_label, human_size,
    suggest_target,
)
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    root.setObjectName("SuitePage")
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    add_btn = _ui.ghost_btn("Add files…", "Add logs to the queue", "add")
    add_dir_btn = _ui.ghost_btn("Add folder…", "Scan folder for logs", "folder")
    clear_btn = _ui.ghost_btn("Clear", "Remove all queue rows", "clear")
    start_btn = _ui.primary_btn("Start batch", "Convert queue sequentially", "play")
    cancel_btn = _ui.ghost_btn("Cancel", "Cancel active convert", "close")
    cancel_btn.setEnabled(False)
    layout.addWidget(_ui.tool_strip(
        add_btn, add_dir_btn, clear_btn, start_btn, cancel_btn))

    body = QWidget()
    body_lay = QVBoxLayout(body)
    body_lay.setContentsMargins(_ui.PAD_X, _ui.PAD_Y, _ui.PAD_X, _ui.PAD_Y)
    body_lay.setSpacing(8)
    layout.addWidget(body, 1)

    opts = QHBoxLayout()
    fmt_combo = QComboBox()
    for key in ENGINE_FORMATS:
        fmt_combo.addItem(engine_label(key), key)
    idx = fmt_combo.findData(session.last_fmt)
    if idx >= 0:
        fmt_combo.setCurrentIndex(idx)
    out_dir = QLineEdit(session.last_out_dir or "")
    out_dir.setPlaceholderText("Output folder (empty = beside each source)")
    pick_dir = _ui.ghost_btn("…", "Pick output folder", "folder")
    opts.addWidget(QLabel("Target format:"))
    opts.addWidget(fmt_combo)
    opts.addWidget(QLabel("Output dir:"), 0)
    opts.addWidget(out_dir, 1)
    opts.addWidget(pick_dir)
    body_lay.addLayout(opts)

    table = QTableWidget(0, 5)
    table.setHorizontalHeaderLabels(
        ["Source", "Detected", "Size", "Status", "Output"])
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    table.horizontalHeader().setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
    _ui.style_table(table) if hasattr(_ui, "style_table") else None
    body_lay.addWidget(table, 1)

    progress = QProgressBar()
    progress.setRange(0, 100)
    progress.setValue(0)
    body_lay.addWidget(progress)
    hint = QLabel(
        "Queue classic CAN and CAN FD logs. Same host engine as Convert "
        "(BLF · ASC · CSV · PCAP · TRC). Rows run one after another.")
    hint.setObjectName("SuiteHint")
    hint.setWordWrap(True)
    body_lay.addWidget(hint)

    queue: list[dict] = []
    run_state = {"busy": False, "index": -1, "job": None}

    def _fmt() -> str:
        return str(fmt_combo.currentData() or "asc")

    def _refresh_table():
        table.setRowCount(0)
        for item in queue:
            row = table.rowCount()
            table.insertRow(row)
            vals = [
                os.path.basename(item["source"]),
                (item.get("detected") or "?").upper(),
                human_size(int(item.get("size") or 0)),
                item.get("status") or "Queued",
                os.path.basename(item.get("target") or "") or "—",
            ]
            for c, text in enumerate(vals):
                cell = QTableWidgetItem(text)
                cell.setToolTip(item.get("source") if c == 0 else (
                    item.get("target") if c == 4 else text))
                table.setItem(row, c, cell)

    def _add_paths(paths: list[str]):
        seen = {os.path.normcase(os.path.abspath(q["source"])) for q in queue}
        added = 0
        for path in paths:
            if not path or not os.path.isfile(path):
                continue
            key = os.path.normcase(os.path.abspath(path))
            if key in seen:
                continue
            detected = detect_format(path)
            if not detected:
                continue
            try:
                size = os.path.getsize(path)
            except OSError:
                size = 0
            queue.append({
                "source": path,
                "detected": detected,
                "size": size,
                "status": "Queued",
                "target": "",
            })
            seen.add(key)
            session.note_source(path)
            added += 1
        _refresh_table()
        if added:
            plugin_shell.set_status(
                parent.window(), "Queued %d file(s)" % added, 3000)

    def _on_add():
        paths, _ = QFileDialog.getOpenFileNames(
            parent, "Add CAN logs", session.start_dir(), OPEN_FILTER)
        if paths:
            _add_paths(paths)

    def _on_add_dir():
        folder = QFileDialog.getExistingDirectory(
            parent, "Scan folder for CAN logs", session.start_dir())
        if not folder:
            return
        found = []
        for root_dir, _dirs, files in os.walk(folder):
            for name in files:
                ext = os.path.splitext(name)[1].lower()
                if ext in (".blf", ".asc", ".csv", ".pcap", ".pcapng", ".trc"):
                    found.append(os.path.join(root_dir, name))
            # shallow by default — stop after top + 1 level for safety
            break
        # also one nested level
        for name in os.listdir(folder):
            sub = os.path.join(folder, name)
            if not os.path.isdir(sub):
                continue
            try:
                for fn in os.listdir(sub):
                    ext = os.path.splitext(fn)[1].lower()
                    if ext in (".blf", ".asc", ".csv", ".pcap", ".pcapng", ".trc"):
                        found.append(os.path.join(sub, fn))
            except OSError:
                continue
        if not found:
            QMessageBox.information(
                parent, "Batch", "No supported logs found in folder")
            return
        _add_paths(found)

    def _on_pick_dir():
        d = QFileDialog.getExistingDirectory(
            parent, "Output folder", out_dir.text() or session.start_dir())
        if d:
            out_dir.setText(d)
            session.last_out_dir = d

    def _set_busy(busy: bool):
        run_state["busy"] = busy
        start_btn.setEnabled(not busy)
        cancel_btn.setEnabled(busy)
        add_btn.setEnabled(not busy)
        add_dir_btn.setEnabled(not busy)
        clear_btn.setEnabled(not busy)
        fmt_combo.setEnabled(not busy)

    def _target_for(src: str) -> str:
        fmt = _fmt()
        base = suggest_target(src, fmt)
        dest_root = out_dir.text().strip()
        if dest_root:
            return os.path.join(dest_root, os.path.basename(base))
        return base

    def _run_next():
        i = run_state["index"] + 1
        while i < len(queue) and queue[i].get("status") not in (
                "Queued", "Retry"):
            i += 1
        if i >= len(queue):
            _set_busy(False)
            progress.setValue(100)
            done = sum(1 for q in queue if q.get("status") == "OK")
            fail = sum(1 for q in queue if q.get("status") == "Failed")
            plugin_shell.set_status(
                parent.window(),
                "Batch finished — %d OK, %d failed" % (done, fail), 5000)
            log_fn("SYS", "-", b"",
                   "Batch finished OK=%d FAIL=%d" % (done, fail))
            if hasattr(parent, "notify_jobs_changed"):
                parent.notify_jobs_changed()
            return

        run_state["index"] = i
        item = queue[i]
        src = item["source"]
        tgt = _target_for(src)
        # skip same-format same-path
        if detect_format(src) == _fmt() and os.path.normcase(
                os.path.abspath(src)) == os.path.normcase(os.path.abspath(tgt)):
            item["status"] = "Skipped"
            item["target"] = tgt
            _refresh_table()
            _run_next()
            return

        item["status"] = "Running"
        item["target"] = tgt
        _refresh_table()
        total = max(1, len(queue))
        progress.setValue(int(100 * i / total))

        def on_prog(pct: int):
            # blend file progress into batch bar
            base = int(100 * i / total)
            span = int(100 / total)
            progress.setValue(base + int(span * pct / 100))

        def on_done(rec):
            item["status"] = "OK" if rec.ok else "Failed"
            item["target"] = rec.target or tgt
            if not rec.ok:
                item["error"] = rec.error
            _refresh_table()
            _run_next()

        job = start_convert(
            session, src, tgt, _fmt(), batch=True,
            on_progress=on_prog, on_done=on_done)
        run_state["job"] = job
        if job is None:
            # finished sync via on_done already
            pass

    def _on_start():
        if run_state["busy"]:
            return
        if not queue:
            QMessageBox.information(parent, "Batch", "Queue is empty")
            return
        # reset failed/queued
        for q in queue:
            if q.get("status") in ("Failed", "Skipped", "OK"):
                continue
            q["status"] = "Queued"
        run_state["index"] = -1
        _set_busy(True)
        session.last_fmt = _fmt()
        if out_dir.text().strip():
            session.last_out_dir = out_dir.text().strip()
        log_fn("SYS", "-", b"", "Batch start · %d file(s) → %s" % (
            len(queue), _fmt().upper()))
        _run_next()

    def _on_cancel():
        cancel_active(session)
        # mark remaining as cancelled after current
        for j in range(run_state["index"] + 1, len(queue)):
            if queue[j].get("status") == "Queued":
                queue[j]["status"] = "Cancelled"
        _refresh_table()

    def _on_clear():
        if run_state["busy"]:
            return
        queue.clear()
        _refresh_table()
        progress.setValue(0)

    add_btn.clicked.connect(_on_add)
    add_dir_btn.clicked.connect(_on_add_dir)
    clear_btn.clicked.connect(_on_clear)
    start_btn.clicked.connect(_on_start)
    cancel_btn.clicked.connect(_on_cancel)
    pick_dir.clicked.connect(_on_pick_dir)

    root.refresh_from_session = lambda: None  # type: ignore[attr-defined]
    return root
