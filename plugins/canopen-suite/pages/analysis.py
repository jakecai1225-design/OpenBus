# -*- coding: utf-8 -*-
"""Analysis — live / offline CANopen decode (EDS-driven).

Simplified layout: one tool row → Trace table | Watch.
NMT / raw TX live under Live activity and Network menu (not here).
"""

from __future__ import annotations

import csv
import os
import time

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QFileDialog,
    QLabel,
    QSplitter,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, vscode_theme as T
from core.eds_decode import EdsBusDecoder
from pages import _ui


# Preset → allowed Kind prefixes / exact names (None = all).
FILTER_PRESETS = (
    ("All traffic", None),
    ("PDO only", frozenset({"TPDO", "RPDO"})),
    ("SDO only", frozenset({"TSDO", "RSDO"})),
    ("Network (NMT · SYNC · HB)", frozenset({"NMT", "SYNC", "TIME", "HB", "GUARD"})),
    ("EMCY · LSS", frozenset({"EMCY", "LSS"})),
)


def _kind_matches(kind: str, allowed) -> bool:
    if allowed is None:
        return True
    k = kind or ""
    for prefix in allowed:
        if k == prefix or k.startswith(prefix):
            return True
    return False


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    decoder = EdsBusDecoder()

    empty_open = _ui.primary_btn(
        "Open EDS…", "Load an EDS so frames decode to named objects", "folder")
    empty_import = _ui.ghost_btn(
        "Import CSV", "Offline Trace without OD names", "folder")
    empty = _ui.empty_state(
        "No EDS for decode",
        "Open an EDS, or Import CSV for offline Trace.",
        actions=[empty_open, empty_import])

    work = QWidget()
    work_lay = QVBoxLayout(work)
    work_lay.setContentsMargins(0, 0, 0, 0)
    work_lay.setSpacing(0)

    status = QLabel("Open an EDS, then watch the bus")
    status.setObjectName("SuiteCount")
    status.setToolTip(
        "Decoder uses the EDS draft / Live OD.\n"
        "Use Live → NMT for Start / Stop / Pre-op.")

    scope = QComboBox()
    scope.setFixedHeight(_ui.CTRL_H)
    scope.setMinimumWidth(160)
    scope.setMaximumWidth(220)
    scope.setToolTip("What to show in the Trace list")
    for label, _kinds in FILTER_PRESETS:
        scope.addItem(label)

    pause_chk = QCheckBox("Pause")
    pause_chk.setToolTip("Freeze live updates")
    pause_chk.setFixedHeight(_ui.CTRL_H)

    import_btn = _ui.ghost_btn(
        "Import", "Import offline CSV (Time, ID, Data)", "folder")
    clear_btn = _ui.ghost_btn("Clear", "Clear Trace and Watch", "clear")
    export_btn = _ui.ghost_btn("Export", "Export Trace as CSV", "export")

    work_lay.addWidget(_ui.tool_strip(
        status, scope, pause_chk,
        import_btn, clear_btn, export_btn, stretch_at=1))

    table = QTableWidget(0, 6)
    table.setHorizontalHeaderLabels([
        "Time", "ID", "Type", "Node", "What happened", "Data",
    ])
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.setWordWrap(False)
    table.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    table.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    table.setVerticalScrollMode(QAbstractItemView.ScrollMode.ScrollPerPixel)
    _ui.style_table(table)
    _ui.configure_columns(
        table, stretch=4, mins={0: 88, 1: 56, 2: 56, 3: 40, 4: 160, 5: 80})

    watch = QTableWidget(0, 4)
    watch.setHorizontalHeaderLabels(["Object", "Name", "Value", "Updated"])
    watch.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    watch.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    watch.setVerticalScrollMode(QAbstractItemView.ScrollMode.ScrollPerPixel)
    _ui.style_table(watch)
    _ui.configure_columns(
        watch, stretch=1, mins={0: 88, 1: 96, 2: 72, 3: 72})

    watch_host = QWidget()
    watch_host.setObjectName("SuitePropPanel")
    wh = QVBoxLayout(watch_host)
    wh.setContentsMargins(0, 0, 0, 0)
    wh.setSpacing(0)
    wh.addWidget(_ui.panel_header("Watch"))
    wh.addWidget(watch, 1)

    split = QSplitter()
    split.addWidget(table)
    split.addWidget(watch_host)
    _ui.configure_splitter(split, golden=True, master_left=True)
    work_lay.addWidget(split, 1)

    gate = QStackedWidget()
    gate.addWidget(empty)
    gate.addWidget(work)
    layout.addWidget(gate, 1)

    watch_rows = {}
    flag_brush = QColor(getattr(T, "WARN_FG", "#C48A00"))

    def _has_od() -> bool:
        return bool(session.draft_entries or session.od_entries)

    def _sync_gate(_=None):
        # Always allow work surface when importing offline; for live prefer OD
        if _has_od() or table.rowCount() > 0:
            gate.setCurrentWidget(work)
            if _has_od():
                _rebuild_decoder()
        else:
            gate.setCurrentWidget(empty)

    def _rebuild_decoder(_=None):
        entries = session.draft_entries or session.od_entries
        n = decoder.rebuild(entries, session.node_id)
        path = os.path.basename(session.eds_path or "") or "draft"
        if n:
            status.setText("EDS %s · Node %d · %d PDO maps" % (
                path, session.node_id, n))
        else:
            status.setText(
                "EDS %s · Node %d · no PDO maps yet" % (path, session.node_id))

    def _allowed_kinds():
        i = scope.currentIndex()
        if i < 0 or i >= len(FILTER_PRESETS):
            return None
        return FILTER_PRESETS[i][1]

    def _upsert_watch(ts: float, node, values):
        if not values:
            return
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        nid = "-" if node is None else str(node)
        for v in values:
            key = (nid, v.index, v.subindex)
            obj = ("0x%04X:%02X" % (v.index, v.subindex)
                   if v.subindex else "0x%04X" % v.index)
            display = v.text
            if v.extra:
                display = "%s  (%s)" % (v.text, v.extra)
            if key in watch_rows:
                row = watch_rows[key]
                watch.item(row, 2).setText(display)
                watch.item(row, 3).setText(tstr)
                tip = "%s · Node %s" % (v.name or obj, nid)
                watch.item(row, 2).setToolTip(tip)
            else:
                row = watch.rowCount()
                watch.insertRow(row)
                watch_rows[key] = row
                cells = [obj, v.name or "", display, tstr]
                for col, text in enumerate(cells):
                    it = QTableWidgetItem(text)
                    if col == 2:
                        it.setToolTip("%s · Node %s" % (v.name or obj, nid))
                    watch.setItem(row, col, it)

    def _append_row(ts: float, can_id: int, data: bytes):
        ix = decoder.decode(can_id, data or b"", ts=ts)
        if not _kind_matches(ix.kind, _allowed_kinds()):
            return
        flag = bool(ix.anomalies)
        summary = ix.summary
        if flag:
            summary = "⚠ " + summary
        _upsert_watch(ts, ix.node_id, ix.values)
        hex_str = " ".join("%02X" % b for b in (data or b""))
        row = table.rowCount()
        table.insertRow(row)
        tstr = (time.strftime("%H:%M:%S", time.localtime(ts))
                + ".%03d" % int(ts % 1 * 1000))
        vals = [
            tstr,
            "0x%03X" % (can_id & 0x7FF),
            ix.kind,
            str(ix.node_id) if ix.node_id is not None else "-",
            summary,
            hex_str,
        ]
        for col, text in enumerate(vals):
            item = QTableWidgetItem(str(text))
            if col == 4:
                tip = ix.detail or ix.summary
                if ix.layer:
                    tip += "\nLayer: %s" % ix.layer
                if ix.values:
                    tip += "\n" + "\n".join(
                        "%s = %s%s" % (
                            v.display(), v.text,
                            (" · " + v.extra) if v.extra else "")
                        for v in ix.values)
                if ix.anomalies:
                    tip += "\n! " + "; ".join(ix.anomalies)
                item.setToolTip(tip)
                if flag:
                    item.setForeground(flag_brush)
            table.setItem(row, col, item)
        while table.rowCount() > 4000:
            table.removeRow(0)
        table.scrollToBottom()

    def on_frame(frame):
        if pause_chk.isChecked():
            return
        data = bytes(frame.data) if frame.data else b""
        gate.setCurrentWidget(work)
        _append_row(time.time(), int(getattr(frame, "id", 0)), data)

    def on_clear():
        table.setRowCount(0)
        watch.setRowCount(0)
        watch_rows.clear()

    def on_export():
        rows = []
        for r in range(table.rowCount()):
            rows.append([
                table.item(r, c).text() if table.item(r, c) else ""
                for c in range(6)
            ])
        path = plugin_shell.export_csv(
            parent,
            ["Time", "ID", "Type", "Node", "What happened", "Data"],
            rows,
            "canopen_analysis.csv",
        )
        if path:
            plugin_shell.set_status(parent, "Exported %s" % path, 3000)

    def on_import():
        path, _ = QFileDialog.getOpenFileName(
            parent, "Import CAN log (CSV)", "",
            "CSV (*.csv);;All (*.*)")
        if not path:
            return
        n = 0
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                reader = csv.reader(f)
                next(reader, None)
                for row in reader:
                    if len(row) < 2:
                        continue
                    can_id = 0
                    data = b""
                    for cell in row:
                        c = cell.strip()
                        if not c:
                            continue
                        if " " in c and all(
                                len(p) <= 2 for p in c.split()):
                            try:
                                data = bytes(
                                    int(p, 16) for p in c.split() if p)
                                continue
                            except ValueError:
                                pass
                        if c.lower().startswith("0x") or c.isdigit():
                            try:
                                can_id = int(c, 0) & 0x7FF
                            except ValueError:
                                pass
                    if can_id or data:
                        _append_row(time.time(), can_id, data)
                        n += 1
        except OSError as exc:
            plugin_shell.set_status(parent, str(exc), 4000)
            return
        plugin_shell.set_status(parent, "Imported %d frames" % n, 3000)
        log_fn("SYS", "-", b"", "Analysis import %s (%d)" % (path, n))
        gate.setCurrentWidget(work)
        _rebuild_decoder()

    session.on_bus_frame(on_frame)
    session.on_od_changed(_sync_gate)
    if hasattr(session, "on_node_changed"):
        session.on_node_changed(_rebuild_decoder)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)
    import_btn.clicked.connect(on_import)
    empty_import.clicked.connect(on_import)
    empty_open.clicked.connect(
        lambda: parent.run_action("eds.open")
        if hasattr(parent, "run_action") else None)
    _sync_gate()

    root.decoder = decoder  # type: ignore[attr-defined]
    return root
