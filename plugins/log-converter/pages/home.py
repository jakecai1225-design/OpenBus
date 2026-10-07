# -*- coding: utf-8 -*-
"""Single Convert workspace — add files, pick format, convert (1..N)."""

from __future__ import annotations

import os
import subprocess
import sys

from PyQt6.QtWidgets import (
    QAbstractItemView, QCheckBox, QComboBox, QFileDialog, QHBoxLayout,
    QHeaderView, QLabel, QLineEdit, QMessageBox, QProgressBar, QTableWidget,
    QTableWidgetItem, QVBoxLayout, QWidget,
)

from _shared import plugin_shell
from core.convert_runner import cancel_active, start_convert
from formats import (
    ENGINE_FORMATS, OPEN_FILTER, detect_format, engine_label, human_size,
    probe_file, suggest_target,
)
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    root.setObjectName("SuitePage")
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    add_btn = _ui.ghost_btn("Add…", "Add one or more CAN logs", "add")
    clear_btn = _ui.ghost_btn("Clear", "Clear the file list", "clear")
    convert_btn = _ui.primary_btn("Convert", "Convert listed files", "play")
    cancel_btn = _ui.ghost_btn("Cancel", "Cancel running job", "close")
    cancel_btn.setEnabled(False)
    reveal_btn = _ui.ghost_btn("Reveal", "Reveal last output", "folder")
    reveal_btn.setEnabled(False)
    layout.addWidget(_ui.tool_strip(
        add_btn, clear_btn, convert_btn, cancel_btn, reveal_btn))

    body = QWidget()
    body_lay = QVBoxLayout(body)
    body_lay.setContentsMargins(_ui.PAD_X, _ui.PAD_Y, _ui.PAD_X, _ui.PAD_Y)
    body_lay.setSpacing(8)
    layout.addWidget(body, 1)

    # Compact controls row
    row = QHBoxLayout()
    row.setSpacing(8)
    fmt_combo = QComboBox()
    for key in ENGINE_FORMATS:
        fmt_combo.addItem(engine_label(key), key)
    idx = fmt_combo.findData(session.last_fmt)
    if idx >= 0:
        fmt_combo.setCurrentIndex(idx)
    fmt_combo.setMinimumWidth(120)
    out_dir = QLineEdit(session.last_out_dir or "")
    out_dir.setPlaceholderText("Output folder (empty = beside each source)")
    pick_dir = _ui.ghost_btn("…", "Pick output folder", "folder")
    overwrite_chk = QCheckBox("Overwrite")
    overwrite_chk.setToolTip("Overwrite existing targets without asking")
    reveal_chk = QCheckBox("Reveal when done")
    reveal_chk.setChecked(True)
    row.addWidget(QLabel("To"))
    row.addWidget(fmt_combo)
    row.addWidget(QLabel("Out"))
    row.addWidget(out_dir, 1)
    row.addWidget(pick_dir)
    row.addWidget(overwrite_chk)
    row.addWidget(reveal_chk)
    body_lay.addLayout(row)

    meta = QLabel(
        "BLF · ASC · CSV · PCAP/PCAPNG · TRC  —  host CanFileIO · CAN / CAN FD")
    meta.setObjectName("SuiteHint")
    meta.setWordWrap(True)
    body_lay.addWidget(meta)

    table = QTableWidget(0, 5)
    table.setHorizontalHeaderLabels(
        ["Source", "Detected", "Size", "Status", "Output"])
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    table.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    table.horizontalHeader().setSectionResizeMode(4, QHeaderView.ResizeMode.Stretch)
    if hasattr(_ui, "style_table"):
        _ui.style_table(table)
    body_lay.addWidget(table, 1)

    progress = QProgressBar()
    progress.setRange(0, 100)
    progress.setValue(0)
    progress.setTextVisible(True)
    body_lay.addWidget(progress)

    status = QLabel("Add logs, choose target format, Convert.")
    status.setObjectName("SuiteHint")
    status.setWordWrap(True)
    body_lay.addWidget(status)

    queue: list[dict] = []
    run = {"busy": False, "index": -1, "last_out": ""}

    def _fmt() -> str:
        return str(fmt_combo.currentData() or "asc")

    def _refresh():
        table.setRowCount(0)
        for item in queue:
            r = table.rowCount()
            table.insertRow(r)
            vals = [
                os.path.basename(item["source"]),
                (item.get("detected") or "?").upper(),
                human_size(int(item.get("size") or 0)),
                item.get("status") or "Queued",
                os.path.basename(item.get("target") or "") or "—",
            ]
            for c, text in enumerate(vals):
                cell = QTableWidgetItem(text)
                if c == 0:
                    cell.setToolTip(item["source"])
                if c == 4:
                    cell.setToolTip(item.get("target") or "")
                table.setItem(r, c, cell)
        n = len(queue)
        if n == 1:
            probe = probe_file(queue[0]["source"])
            bits = [probe.get("format_label") or "?", human_size(
                int(probe.get("size") or 0))]
            if probe.get("can_fd_hint"):
                bits.append("CAN FD: %s" % probe["can_fd_hint"])
            if probe.get("notes"):
                bits.append(probe["notes"][0])
            meta.setText(" · ".join(bits))
        else:
            meta.setText(
                "%d file(s) → %s  ·  BLF · ASC · CSV · PCAP · TRC"
                % (n, engine_label(_fmt())))

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
        _refresh()
        if added:
            # smart default target when adding a single different format
            if len(queue) == 1:
                det = queue[0].get("detected") or ""
                prefer = "asc" if det == "blf" else (
                    "blf" if det == "asc" else session.last_fmt or "asc")
                i = fmt_combo.findData(prefer)
                if i >= 0:
                    fmt_combo.setCurrentIndex(i)
            status.setText("Queued %d file(s)" % added)
            plugin_shell.set_status(
                parent.window(), "Queued %d file(s)" % added, 2500)

    def _set_busy(busy: bool):
        run["busy"] = busy
        convert_btn.setEnabled(not busy)
        cancel_btn.setEnabled(busy)
        add_btn.setEnabled(not busy)
        clear_btn.setEnabled(not busy)
        fmt_combo.setEnabled(not busy)

    def _target_for(src: str) -> str:
        base = suggest_target(src, _fmt())
        dest = out_dir.text().strip()
        if dest:
            return os.path.join(dest, os.path.basename(base))
        return base

    def _reveal(path: str):
        if not path:
            return
        folder = path if os.path.isdir(path) else os.path.dirname(path)
        if not folder or not os.path.isdir(folder):
            return
        try:
            if sys.platform.startswith("win"):
                if os.path.isfile(path):
                    subprocess.Popen(["explorer", "/select,", path])
                else:
                    os.startfile(folder)  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.Popen(
                    ["open", "-R", path] if os.path.isfile(path)
                    else ["open", folder])
            else:
                subprocess.Popen(["xdg-open", folder])
        except Exception as e:
            log_fn("ERR", "-", b"", "Reveal failed: %s" % e)

    def _run_next():
        i = run["index"] + 1
        while i < len(queue) and queue[i].get("status") not in (
                "Queued", "Retry"):
            i += 1
        if i >= len(queue):
            _set_busy(False)
            progress.setValue(100)
            ok = sum(1 for q in queue if q.get("status") == "OK")
            fail = sum(1 for q in queue if q.get("status") == "Failed")
            status.setText("Done — %d OK, %d failed" % (ok, fail))
            plugin_shell.set_status(parent.window(), status.text(), 4000)
            reveal_btn.setEnabled(bool(run["last_out"]))
            if reveal_chk.isChecked() and run["last_out"]:
                _reveal(run["last_out"])
            return

        run["index"] = i
        item = queue[i]
        src = item["source"]
        tgt = _target_for(src)
        if detect_format(src) == _fmt() and os.path.normcase(
                os.path.abspath(src)) == os.path.normcase(os.path.abspath(tgt)):
            item["status"] = "Skipped"
            item["target"] = tgt
            _refresh()
            _run_next()
            return
        if os.path.isfile(tgt) and not overwrite_chk.isChecked():
            item["status"] = "Exists"
            item["target"] = tgt
            _refresh()
            _run_next()
            return

        item["status"] = "Running"
        item["target"] = tgt
        _refresh()
        total = max(1, len(queue))
        progress.setValue(int(100 * i / total))
        status.setText("Converting %s…" % os.path.basename(src))

        def on_prog(pct: int):
            base = int(100 * i / total)
            span = max(1, int(100 / total))
            progress.setValue(base + int(span * pct / 100))

        def on_done(rec):
            item["status"] = "OK" if rec.ok else "Failed"
            item["target"] = rec.target or tgt
            if rec.ok:
                run["last_out"] = rec.target
            _refresh()
            _run_next()

        start_convert(
            session, src, tgt, _fmt(),
            batch=len(queue) > 1,
            on_progress=on_prog, on_done=on_done)

    def _on_add():
        paths, _ = QFileDialog.getOpenFileNames(
            parent, "Add CAN logs", session.start_dir(), OPEN_FILTER)
        if paths:
            _add_paths(paths)

    def _on_clear():
        if run["busy"]:
            return
        queue.clear()
        progress.setValue(0)
        run["last_out"] = ""
        reveal_btn.setEnabled(False)
        _refresh()
        status.setText("Cleared")

    def _on_convert():
        if run["busy"]:
            return
        if not queue:
            QMessageBox.information(parent, "Convert", "Add at least one log")
            return
        for q in queue:
            if q.get("status") in ("OK", "Failed", "Skipped", "Exists"):
                q["status"] = "Queued"
        run["index"] = -1
        run["last_out"] = ""
        session.last_fmt = _fmt()
        if out_dir.text().strip():
            session.last_out_dir = out_dir.text().strip()
        _set_busy(True)
        log_fn("SYS", "-", b"", "Convert %d → %s" % (len(queue), _fmt().upper()))
        _run_next()

    def _on_cancel():
        cancel_active(session)
        for j in range(run["index"] + 1, len(queue)):
            if queue[j].get("status") == "Queued":
                queue[j]["status"] = "Cancelled"
        _refresh()
        status.setText("Cancel requested…")

    def _on_pick_dir():
        d = QFileDialog.getExistingDirectory(
            parent, "Output folder", out_dir.text() or session.start_dir())
        if d:
            out_dir.setText(d)

    add_btn.clicked.connect(_on_add)
    clear_btn.clicked.connect(_on_clear)
    convert_btn.clicked.connect(_on_convert)
    cancel_btn.clicked.connect(_on_cancel)
    reveal_btn.clicked.connect(lambda: _reveal(run["last_out"]))
    pick_dir.clicked.connect(_on_pick_dir)
    fmt_combo.currentIndexChanged.connect(lambda _i: _refresh())

    def set_source(path: str):
        queue.clear()
        _add_paths([path] if path else [])

    root.set_source = set_source  # type: ignore[attr-defined]
    root.add_paths = _add_paths  # type: ignore[attr-defined]
    root.refresh_from_session = lambda: None  # type: ignore[attr-defined]
    return root
