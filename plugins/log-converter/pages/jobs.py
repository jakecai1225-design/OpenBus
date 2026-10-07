# -*- coding: utf-8 -*-
"""Jobs workspace — conversion history."""

from __future__ import annotations

import os
import time

from PyQt6.QtWidgets import (
    QAbstractItemView, QHeaderView, QMessageBox, QTableWidget,
    QTableWidgetItem, QVBoxLayout, QWidget,
)

from _shared import plugin_shell
from formats import engine_label
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    root.setObjectName("SuitePage")
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    refresh_btn = _ui.ghost_btn("Refresh", "Reload history", "refresh")
    rerun_btn = _ui.ghost_btn("Re-run", "Open Convert with this source", "play")
    clear_btn = _ui.ghost_btn("Clear history", "Delete saved job list", "trash")
    layout.addWidget(_ui.tool_strip(refresh_btn, rerun_btn, clear_btn))

    body = QWidget()
    body_lay = QVBoxLayout(body)
    body_lay.setContentsMargins(_ui.PAD_X, _ui.PAD_Y, _ui.PAD_X, _ui.PAD_Y)
    layout.addWidget(body, 1)

    table = QTableWidget(0, 8)
    table.setHorizontalHeaderLabels([
        "When", "Result", "Format", "Frames", "Duration",
        "Source", "Target", "Error"])
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.horizontalHeader().setSectionResizeMode(
        5, QHeaderView.ResizeMode.Stretch)
    table.horizontalHeader().setSectionResizeMode(
        6, QHeaderView.ResizeMode.Stretch)
    body_lay.addWidget(table, 1)

    def _reload():
        table.setRowCount(0)
        for rec in session.history:
            row = table.rowCount()
            table.insertRow(row)
            when = "—"
            if rec.finished:
                when = time.strftime(
                    "%Y-%m-%d %H:%M:%S", time.localtime(rec.finished))
            vals = [
                when,
                "OK" if rec.ok else "FAIL",
                engine_label(rec.fmt),
                str(rec.frames),
                "%.2fs" % rec.duration_s if rec.duration_s else "—",
                os.path.basename(rec.source) or rec.source,
                os.path.basename(rec.target) or rec.target,
                rec.error or ("batch" if rec.batch else ""),
            ]
            for c, text in enumerate(vals):
                item = QTableWidgetItem(text)
                if c == 5:
                    item.setToolTip(rec.source)
                if c == 6:
                    item.setToolTip(rec.target)
                table.setItem(row, c, item)

    def _selected_rec():
        rows = table.selectionModel().selectedRows()
        if not rows:
            return None
        idx = rows[0].row()
        if idx < 0 or idx >= len(session.history):
            return None
        return session.history[idx]

    def _on_rerun():
        rec = _selected_rec()
        if rec is None:
            QMessageBox.information(parent, "Jobs", "Select a history row")
            return
        if hasattr(parent, "rerun_convert"):
            parent.rerun_convert(rec.source, rec.fmt)
        else:
            plugin_shell.set_status(
                parent.window(), "Open Convert manually", 3000)

    def _on_clear():
        if not session.history:
            return
        r = QMessageBox.question(
            parent, "Clear history",
            "Delete %d job record(s)?" % len(session.history))
        if r != QMessageBox.StandardButton.Yes:
            return
        session.clear_history()
        _reload()
        log_fn("SYS", "-", b"", "Job history cleared")

    refresh_btn.clicked.connect(_reload)
    rerun_btn.clicked.connect(_on_rerun)
    clear_btn.clicked.connect(_on_clear)

    root.refresh_from_session = _reload  # type: ignore[attr-defined]
    _reload()
    return root
