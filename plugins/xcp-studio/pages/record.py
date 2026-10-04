# -*- coding: utf-8 -*-
"""Record — CSV / MDF subset export."""

from __future__ import annotations

from PyQt6.QtWidgets import QFileDialog, QVBoxLayout, QWidget

from pages import _ui


def build(shell, session, log_fn) -> QWidget:
    root = QWidget()
    lay = QVBoxLayout(root)
    lay.setContentsMargins(12, 8, 12, 8)
    lay.setSpacing(8)

    start = _ui.primary_btn("Start record", "Capture poll/DAQ samples", "record")
    stop = _ui.ghost_btn("Stop", "Stop capture", "debug-stop")
    csv_btn = _ui.ghost_btn("Export CSV…", "Spreadsheet log", "export")
    mdf_btn = _ui.ghost_btn("Export MDF subset…", "Text MDF-like dump", "save")
    status = _ui.count_label("Idle")
    lay.addWidget(_ui.tool_strip(start, stop, csv_btn, mdf_btn, stretch_at=4))
    lay.addWidget(status)
    lay.addWidget(_ui.quiet_label(
        "CSV is the daily path. MDF subset is a text dump, not binary ASAM MDF4."))
    lay.addStretch(1)

    def _sync():
        n = max(0, len(session.record_rows) - 1)
        if session.recording:
            status.setText("Recording — %d samples" % n)
        else:
            status.setText("Stopped — %d samples" % n)

    def on_start():
        session.start_record()
        _sync()

    def on_stop():
        session.stop_record()
        _sync()

    def on_csv():
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export CSV", "xcp_record.csv", "CSV (*.csv)")
        if path and session.export_csv(path):
            log_fn("SYS", "-", b"", "CSV %s" % path)

    def on_mdf():
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export MDF subset", "xcp_record.mdf.txt",
            "Text (*.txt *.mdf);;All (*)")
        if path and session.export_mdf_stub(path):
            log_fn("SYS", "-", b"", "MDF subset %s" % path)

    start.clicked.connect(on_start)
    stop.clicked.connect(on_stop)
    csv_btn.clicked.connect(on_csv)
    mdf_btn.clicked.connect(on_mdf)
    session.on_changed(_sync)
    _sync()
    return root
