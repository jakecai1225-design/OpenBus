# -*- coding: utf-8 -*-
"""Convert workspace — single-file CAN / CAN FD log conversion."""

from __future__ import annotations

import os
import subprocess
import sys

from PyQt6.QtWidgets import (
    QCheckBox, QComboBox, QFileDialog, QFormLayout, QGroupBox, QHBoxLayout,
    QLabel, QLineEdit, QMessageBox, QProgressBar, QPushButton, QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core.convert_runner import cancel_active, start_convert
from formats import (
    ENGINE_FORMATS, OPEN_FILTER, SAVE_FILTERS, detect_format, engine_label,
    human_size, probe_file, suggest_target,
)
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    root.setObjectName("SuitePage")
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    browse_btn = _ui.ghost_btn("Browse…", "Open source log", "folder")
    convert_btn = _ui.primary_btn("Convert", "Run host CanFileIO convert", "play")
    cancel_btn = _ui.ghost_btn("Cancel", "Cancel running job", "close")
    cancel_btn.setEnabled(False)
    open_out_btn = _ui.ghost_btn("Reveal", "Reveal output in Explorer", "folder")
    open_out_btn.setEnabled(False)
    layout.addWidget(_ui.tool_strip(
        browse_btn, convert_btn, cancel_btn, open_out_btn))

    body = QWidget()
    body_lay = QVBoxLayout(body)
    body_lay.setContentsMargins(_ui.PAD_X, _ui.PAD_Y, _ui.PAD_X, _ui.PAD_Y)
    body_lay.setSpacing(10)
    layout.addWidget(body, 1)

    src_box = QGroupBox("Source")
    src_form = QFormLayout(src_box)
    src_edit = QLineEdit()
    src_edit.setPlaceholderText("Path to .blf / .asc / .csv / .pcap / .trc")
    src_meta = QLabel("No file selected")
    src_meta.setObjectName("SuiteHint")
    src_meta.setWordWrap(True)
    src_form.addRow("File:", src_edit)
    src_form.addRow("", src_meta)
    body_lay.addWidget(src_box)

    tgt_box = QGroupBox("Target")
    tgt_form = QFormLayout(tgt_box)
    fmt_combo = QComboBox()
    for key in ENGINE_FORMATS:
        fmt_combo.addItem(engine_label(key), key)
    # Prefer session last format
    idx = fmt_combo.findData(session.last_fmt)
    if idx >= 0:
        fmt_combo.setCurrentIndex(idx)
    out_edit = QLineEdit()
    out_edit.setPlaceholderText("Output path (auto-filled from source + format)")
    pick_out_btn = QPushButton("Save as…")
    pick_out_btn.setObjectName("GhostButton")
    out_row = QHBoxLayout()
    out_row.addWidget(out_edit, 1)
    out_row.addWidget(pick_out_btn)
    tgt_form.addRow("Format:", fmt_combo)
    tgt_form.addRow("Output:", out_row)
    body_lay.addWidget(tgt_box)

    opt_box = QGroupBox("Options")
    opt_lay = QVBoxLayout(opt_box)
    overwrite_chk = QCheckBox("Overwrite existing target without asking")
    open_after_chk = QCheckBox("Reveal output folder when finished")
    open_after_chk.setChecked(True)
    stamp_chk = QCheckBox("Append _converted before extension")
    opt_lay.addWidget(overwrite_chk)
    opt_lay.addWidget(open_after_chk)
    opt_lay.addWidget(stamp_chk)
    body_lay.addWidget(opt_box)

    progress = QProgressBar()
    progress.setRange(0, 100)
    progress.setValue(0)
    progress.setTextVisible(True)
    progress.setFormat("%p%")
    body_lay.addWidget(progress)

    status = QLabel("Ready — host engine: BLF · ASC · CSV · PCAP · TRC")
    status.setObjectName("SuiteHint")
    status.setWordWrap(True)
    body_lay.addWidget(status)
    body_lay.addStretch(1)

    state = {"last_target": "", "busy": False}

    def _fmt_key() -> str:
        return str(fmt_combo.currentData() or "asc")

    def _refresh_meta():
        path = src_edit.text().strip()
        if not path:
            src_meta.setText("No file selected")
            return
        probe = probe_file(path)
        if not probe["exists"]:
            src_meta.setText("File not found")
            return
        bits = [
            probe.get("format_label") or "Unknown",
            human_size(int(probe.get("size") or 0)),
        ]
        if probe.get("can_fd_hint"):
            bits.append("CAN FD: %s" % probe["can_fd_hint"])
        if probe.get("notes"):
            bits.append(probe["notes"][0])
        src_meta.setText(" · ".join(bits))

    def _auto_out():
        src = src_edit.text().strip()
        if not src:
            return
        fmt = _fmt_key()
        path = suggest_target(src, fmt)
        if stamp_chk.isChecked():
            base, ext = os.path.splitext(path)
            path = base + "_converted" + ext
        out_edit.setText(path)

    def _on_browse():
        path, _ = QFileDialog.getOpenFileName(
            parent, "Open CAN log", session.start_dir(), OPEN_FILTER)
        if not path:
            return
        src_edit.setText(path)
        session.note_source(path)
        detected = detect_format(path)
        if detected:
            # Prefer a different target format when possible
            prefer = "asc" if detected == "blf" else (
                "blf" if detected == "asc" else "asc")
            i = fmt_combo.findData(prefer)
            if i >= 0:
                fmt_combo.setCurrentIndex(i)
        _refresh_meta()
        _auto_out()
        status.setText("Source loaded — choose format and Convert")

    def _on_pick_out():
        fmt = _fmt_key()
        filt = SAVE_FILTERS.get(fmt, "All files (*)")
        default = out_edit.text().strip() or suggest_target(
            src_edit.text().strip() or "log", fmt)
        path, _ = QFileDialog.getSaveFileName(
            parent, "Save converted log", default, filt)
        if path:
            out_edit.setText(path)

    def _set_busy(busy: bool):
        state["busy"] = busy
        convert_btn.setEnabled(not busy)
        cancel_btn.setEnabled(busy)
        browse_btn.setEnabled(not busy)
        fmt_combo.setEnabled(not busy)

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
                subprocess.Popen(["open", "-R", path] if os.path.isfile(path)
                                 else ["open", folder])
            else:
                subprocess.Popen(["xdg-open", folder])
        except Exception as e:
            log_fn("ERR", "-", b"", "Reveal failed: %s" % e)

    def _on_done(rec):
        _set_busy(False)
        progress.setValue(100 if rec.ok else progress.value())
        state["last_target"] = rec.target if rec.ok else ""
        open_out_btn.setEnabled(bool(rec.ok and rec.target))
        if rec.ok:
            status.setText(
                "Done — %d frames · %.2fs · %s" % (
                    rec.frames, rec.duration_s, rec.target))
            plugin_shell.set_status(
                parent.window(), "Converted %d frames" % rec.frames, 4000)
            if open_after_chk.isChecked():
                _reveal(rec.target)
            if hasattr(parent, "notify_jobs_changed"):
                parent.notify_jobs_changed()
        else:
            status.setText("Failed — %s" % (rec.error or "unknown error"))
            QMessageBox.warning(
                parent, "Convert failed", rec.error or "Unknown error")

    def _on_convert():
        if state["busy"]:
            return
        src = src_edit.text().strip()
        out = out_edit.text().strip()
        fmt = _fmt_key()
        if not src:
            QMessageBox.information(parent, "Convert", "Select a source file")
            return
        if not out:
            _auto_out()
            out = out_edit.text().strip()
        if not out:
            QMessageBox.information(parent, "Convert", "Set an output path")
            return
        if os.path.isfile(out) and not overwrite_chk.isChecked():
            r = QMessageBox.question(
                parent, "Overwrite?",
                "Target already exists:\n%s\n\nOverwrite?" % out)
            if r != QMessageBox.StandardButton.Yes:
                return
        progress.setValue(0)
        status.setText("Converting…")
        _set_busy(True)

        def on_prog(pct: int):
            progress.setValue(max(0, min(100, pct)))

        job = start_convert(
            session, src, out, fmt,
            on_progress=on_prog, on_done=_on_done)
        if job is None and not state["busy"]:
            # sync failure already finished
            pass
        elif job is None:
            _set_busy(False)

    def _on_cancel():
        if cancel_active(session):
            status.setText("Cancel requested…")

    browse_btn.clicked.connect(_on_browse)
    convert_btn.clicked.connect(_on_convert)
    cancel_btn.clicked.connect(_on_cancel)
    open_out_btn.clicked.connect(
        lambda: _reveal(state["last_target"] or out_edit.text().strip()))
    pick_out_btn.clicked.connect(_on_pick_out)
    fmt_combo.currentIndexChanged.connect(lambda _i: _auto_out())
    stamp_chk.toggled.connect(lambda _v: _auto_out())
    src_edit.editingFinished.connect(lambda: (_refresh_meta(), _auto_out()))

    root.refresh_from_session = lambda: None  # type: ignore[attr-defined]
    root.set_source = lambda p: (  # type: ignore[attr-defined]
        src_edit.setText(p), _refresh_meta(), _auto_out())
    return root
