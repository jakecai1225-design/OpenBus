# -*- coding: utf-8 -*-
"""Toolkit workspace — ASC/CSV trim, filter, merge, split, redact."""

from __future__ import annotations

import os

from PyQt6.QtWidgets import (
    QCheckBox, QComboBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel,
    QLineEdit, QMessageBox, QProgressBar, QPushButton, QSpinBox, QTabWidget,
    QVBoxLayout, QWidget,
)

from _shared import plugin_shell, state_store

from core import load_any, write_asc, write_csv_log

PAGE_STATE = "toolkit.json"


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    loaded = {"frames": [], "names": [], "paths": []}

    top = QHBoxLayout()
    load_btn = QPushButton("Load logs…")
    clear_btn = QPushButton("Clear loaded")
    info_label = QLabel("No files loaded")
    info_label.setStyleSheet("color:#78909c;")
    top.addWidget(load_btn)
    top.addWidget(clear_btn)
    top.addWidget(info_label, 1)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Offline ASC/CSV toolbox: trim by time, filter by ID, merge, split, "
        "and redact. Multi-select load; output ASC or CSV."))

    progress = QProgressBar()
    progress.setRange(0, 100)
    progress.setValue(0)
    progress.setVisible(False)
    layout.addWidget(progress)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    trim_tab = QWidget()
    tv = QFormLayout(trim_tab)
    t_start = QLineEdit("0")
    t_end = QLineEdit("999999")
    id_filter = QLineEdit("")
    id_filter.setPlaceholderText("empty = no filter; e.g. 0x100,0x200")
    id_mode = QComboBox()
    id_mode.addItems(["Whitelist (keep)", "Blacklist (drop)"])
    out_fmt_trim = QComboBox()
    out_fmt_trim.addItems(["ASC", "CSV"])
    tv.addRow("Start time (s):", t_start)
    tv.addRow("End time (s):", t_end)
    tv.addRow("ID list:", id_filter)
    tv.addRow("ID mode:", id_mode)
    tv.addRow("Output format:", out_fmt_trim)
    trim_btn = QPushButton("Trim + filter and save…")
    tv.addRow(trim_btn)
    tabs.addTab(trim_tab, "Trim / Filter")

    merge_tab = QWidget()
    mv = QFormLayout(merge_tab)
    out_fmt_merge = QComboBox()
    out_fmt_merge.addItems(["ASC", "CSV"])
    mv.addRow("Output format:", out_fmt_merge)
    merge_btn = QPushButton("Merge all loaded (time-sorted) and save…")
    mv.addRow(merge_btn)
    tabs.addTab(merge_tab, "Merge")

    split_tab = QWidget()
    sv = QFormLayout(split_tab)
    split_mode = QComboBox()
    split_mode.addItems(["By frame count", "By duration (s)"])
    split_size = QSpinBox()
    split_size.setRange(100, 10000000)
    split_size.setValue(100000)
    out_fmt_split = QComboBox()
    out_fmt_split.addItems(["ASC", "CSV"])
    sv.addRow("Split by:", split_mode)
    sv.addRow("Chunk size:", split_size)
    sv.addRow("Output format:", out_fmt_split)
    split_btn = QPushButton("Split and save…")
    sv.addRow(split_btn)
    tabs.addTab(split_tab, "Split")

    mask_tab = QWidget()
    kv = QFormLayout(mask_tab)
    mask_id_chk = QCheckBox("Mask ID low byte to 0xFF")
    mask_payload_chk = QCheckBox("Zero payload bytes 3…end")
    out_fmt_mask = QComboBox()
    out_fmt_mask.addItems(["ASC", "CSV"])
    kv.addRow(mask_id_chk)
    kv.addRow(mask_payload_chk)
    kv.addRow("Output format:", out_fmt_mask)
    mask_btn = QPushButton("Redact and save…")
    kv.addRow(mask_btn)
    tabs.addTab(mask_tab, "Redact")

    def _persist(**extra):
        data = {
            "t_start": t_start.text(),
            "t_end": t_end.text(),
            "id_filter": id_filter.text(),
            "id_mode": id_mode.currentIndex(),
            "out_fmt_trim": out_fmt_trim.currentIndex(),
            "split_mode": split_mode.currentIndex(),
            "split_size": split_size.value(),
        }
        data.update(extra)
        state_store.save_state("log-analysis", data, PAGE_STATE)

    def _update_info():
        if not loaded["frames"]:
            info_label.setText("No files loaded")
            info_label.setStyleSheet("color:#78909c;")
            return
        t0 = loaded["frames"][0][0]
        t1 = loaded["frames"][-1][0]
        ids = len({fr[2] for fr in loaded["frames"]})
        info_label.setText(
            "%d file(s) · %d frames · %d IDs · t=%.3f…%.3f s"
            % (len(loaded["names"]), len(loaded["frames"]), ids, t0, t1))
        info_label.setStyleSheet("color:#2e7d32;")

    def _on_load():
        start_dir = session.start_dir()
        paths, _ = QFileDialog.getOpenFileNames(
            parent, "Load CAN logs", start_dir,
            "Logs (*.asc *.csv *.log *.txt);;All files (*)")
        if not paths:
            return
        progress.setVisible(True)
        progress.setValue(0)
        total = 0
        for i, path in enumerate(paths):
            frames, err = load_any(path)
            if frames is None:
                QMessageBox.warning(
                    parent, "Load failed", "%s: %s" % (path, err))
                continue
            loaded["frames"].extend(frames)
            loaded["names"].append(os.path.basename(path))
            loaded["paths"].append(path)
            session.note_log_path(path)
            total += len(frames)
            progress.setValue(int(100 * (i + 1) / len(paths)))
        progress.setVisible(False)
        if not loaded["frames"]:
            QMessageBox.warning(parent, "Load", "No frames parsed")
            return
        loaded["frames"].sort(key=lambda fr: fr[0])
        _update_info()
        _persist()
        log_fn("RX", "-", b"", "Toolkit loaded %d frames from %d file(s)"
               % (total, len(paths)))
        plugin_shell.set_status(
            parent, "Loaded %d frames from %d file(s)" % (total, len(paths)),
            4000)

    def _on_clear():
        loaded["frames"].clear()
        loaded["names"].clear()
        loaded["paths"].clear()
        _update_info()
        plugin_shell.set_status(parent, "Cleared", 2000)

    def _parse_ids(text):
        ids = set()
        for tok in text.replace(",", " ").split():
            tok = tok.strip().strip(",")
            if not tok:
                continue
            try:
                ids.add(int(tok, 0))
            except ValueError:
                pass
        return ids

    def _save(frames, fmt_index):
        if not frames:
            QMessageBox.information(parent, "Save", "No frames to save")
            return
        ext = "asc" if fmt_index == 0 else "csv"
        start = session.start_dir()
        path, _ = QFileDialog.getSaveFileName(
            parent, "Save log",
            os.path.join(start, "processed.%s" % ext) if start else "processed.%s" % ext,
            "Log (*.%s)" % ext)
        if not path:
            return
        try:
            progress.setVisible(True)
            progress.setValue(50)
            if fmt_index == 0:
                write_asc(path, frames)
            else:
                write_csv_log(path, frames)
            progress.setValue(100)
            progress.setVisible(False)
            session.note_export_path(path)
            _persist()
            log_fn("RX", "-", b"", "Toolkit saved %d frames → %s"
                   % (len(frames), path))
            plugin_shell.set_status(
                parent, "Saved %d frames → %s" % (len(frames), path), 5000)
        except OSError as e:
            progress.setVisible(False)
            QMessageBox.warning(parent, "Save failed", str(e))

    def _need_data():
        if not loaded["frames"]:
            QMessageBox.information(parent, "Toolkit", "Load logs first")
            return False
        return True

    def _on_trim():
        if not _need_data():
            return
        try:
            t0 = float(t_start.text() or 0)
            t1 = float(t_end.text() or 1e9)
        except ValueError:
            QMessageBox.warning(parent, "Format", "Times must be numbers")
            return
        ids = _parse_ids(id_filter.text())
        whitelist = id_mode.currentIndex() == 0
        out = []
        for fr in loaded["frames"]:
            if not (t0 <= fr[0] <= t1):
                continue
            if ids:
                keep = (fr[2] in ids) if whitelist else (fr[2] not in ids)
                if not keep:
                    continue
            out.append(fr)
        plugin_shell.set_status(
            parent, "Trim/filter: %d → %d frames"
            % (len(loaded["frames"]), len(out)), 3000)
        _persist()
        _save(out, out_fmt_trim.currentIndex())

    def _on_merge():
        if not _need_data():
            return
        frames = sorted(loaded["frames"], key=lambda fr: fr[0])
        plugin_shell.set_status(
            parent, "Merge %d files → %d frames"
            % (len(loaded["names"]), len(frames)), 3000)
        _save(frames, out_fmt_merge.currentIndex())

    def _on_split():
        if not _need_data():
            return
        start = session.start_dir()
        path, _ = QFileDialog.getSaveFileName(
            parent, "Split output prefix",
            os.path.join(start, "split_part.asc") if start else "split_part.asc",
            "ASC (*.asc);;CSV (*.csv)")
        if not path:
            return
        base = path.rsplit(".", 1)[0]
        fmt = out_fmt_split.currentIndex()
        by_count = split_mode.currentIndex() == 0
        size = split_size.value()
        frames = loaded["frames"]
        parts = []
        if by_count:
            for i in range(0, len(frames), size):
                parts.append(frames[i:i + size])
        else:
            t0 = frames[0][0]
            cur = []
            for fr in frames:
                if fr[0] - t0 > size and cur:
                    parts.append(cur)
                    cur = []
                    t0 = fr[0]
                cur.append(fr)
            if cur:
                parts.append(cur)
        try:
            progress.setVisible(True)
            for i, part in enumerate(parts):
                out_path = "%s_%03d.%s" % (
                    base, i + 1, "asc" if fmt == 0 else "csv")
                if fmt == 0:
                    write_asc(out_path, part)
                else:
                    write_csv_log(out_path, part)
                progress.setValue(int(100 * (i + 1) / max(1, len(parts))))
            progress.setVisible(False)
            if parts:
                session.note_export_path("%s_001.%s" % (base, "asc" if fmt == 0 else "csv"))
            _persist()
            plugin_shell.set_status(
                parent, "Split into %d parts" % len(parts), 4000)
            QMessageBox.information(
                parent, "Split done",
                "%d parts → %s_001…" % (len(parts), base))
        except OSError as e:
            progress.setVisible(False)
            QMessageBox.warning(parent, "Split failed", str(e))

    def _on_mask():
        if not _need_data():
            return
        if not (mask_id_chk.isChecked() or mask_payload_chk.isChecked()):
            QMessageBox.information(
                parent, "Redact", "Select at least one redact option")
            return
        out = []
        for ts, ch, cid, direction, dlc, data in loaded["frames"]:
            if mask_id_chk.isChecked():
                if cid <= 0x7FF:
                    cid = (cid & 0x700) | 0x0FF
                else:
                    cid = (cid & ~0xFF) | 0xFF
            if mask_payload_chk.isChecked() and len(data) > 3:
                data = data[:3] + b"\x00" * (len(data) - 3)
            out.append((ts, ch, cid, direction, dlc, data))
        plugin_shell.set_status(parent, "Redacted %d frames" % len(out), 3000)
        _save(out, out_fmt_mask.currentIndex())

    load_btn.clicked.connect(_on_load)
    clear_btn.clicked.connect(_on_clear)
    trim_btn.clicked.connect(_on_trim)
    merge_btn.clicked.connect(_on_merge)
    split_btn.clicked.connect(_on_split)
    mask_btn.clicked.connect(_on_mask)

    saved = state_store.load_state("log-analysis", PAGE_STATE) or {}
    if saved.get("t_start") is not None:
        t_start.setText(str(saved["t_start"]))
    if saved.get("t_end") is not None:
        t_end.setText(str(saved["t_end"]))
    if saved.get("id_filter") is not None:
        id_filter.setText(str(saved["id_filter"]))
    if isinstance(saved.get("id_mode"), int):
        id_mode.setCurrentIndex(saved["id_mode"])
    if isinstance(saved.get("out_fmt_trim"), int):
        out_fmt_trim.setCurrentIndex(saved["out_fmt_trim"])
    if isinstance(saved.get("split_mode"), int):
        split_mode.setCurrentIndex(saved["split_mode"])
    if saved.get("split_size"):
        split_size.setValue(int(saved["split_size"]))

    return root
