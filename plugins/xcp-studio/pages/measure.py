# -*- coding: utf-8 -*-
"""Measure — poll / DAQ lite + live table + sparkline."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QPainter, QPen, QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QHeaderView,
    QSizePolicy,
    QTableWidget,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pages import _ui


class _Spark(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._pts: list[float] = []
        self.setFixedHeight(48)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.setToolTip("Recent samples of the first selected measurement")

    def push(self, value: float):
        self._pts.append(value)
        self._pts = self._pts[-80:]
        self.update()

    def paintEvent(self, _ev):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#1e1e1e"))
        if len(self._pts) < 2:
            return
        lo, hi = min(self._pts), max(self._pts)
        span = (hi - lo) or 1.0
        w, h = max(1, self.width() - 4), max(1, self.height() - 4)
        pen = QPen(QColor("#4FC3F7"))
        pen.setWidth(1)
        p.setPen(pen)
        n = len(self._pts)
        pts = []
        for i, v in enumerate(self._pts):
            x = 2 + int(i * (w - 1) / max(1, n - 1))
            y = 2 + int((1.0 - (v - lo) / span) * (h - 1))
            pts.append((x, y))
        for a, b in zip(pts, pts[1:]):
            p.drawLine(a[0], a[1], b[0], b[1])


def build(shell, session, log_fn) -> QWidget:
    root = QWidget()
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)

    start_p = _ui.primary_btn("Start poll", "SHORT_UPLOAD loop", "play")
    stop_p = _ui.ghost_btn("Stop poll", "Stop SHORT_UPLOAD", "debug-stop")
    start_d = _ui.ghost_btn("Start DAQ", "START_STOP_SYNCH start", "graph")
    stop_d = _ui.ghost_btn("Stop DAQ", "START_STOP_SYNCH stop", "debug-stop")
    count = _ui.count_label()
    lay.addWidget(_ui.tool_strip(
        start_p, stop_p, start_d, stop_d, count, stretch_at=4))

    split = QHBoxLayout()
    split.setContentsMargins(0, 0, 0, 0)

    tree = QTreeWidget()
    tree.setHeaderLabels(["On", "Name", "Address"])
    _ui.style_tree(tree)
    tree.setRootIsDecorated(False)
    tree.setMaximumWidth(280)

    right = QWidget()
    rlay = QVBoxLayout(right)
    rlay.setContentsMargins(0, 0, 0, 0)
    rlay.setSpacing(0)
    spark = _Spark()
    table = QTableWidget(0, 3)
    table.setHorizontalHeaderLabels(["Name", "Value", "Kind"])
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.verticalHeader().setVisible(False)
    table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    rlay.addWidget(spark)
    rlay.addWidget(table, 1)

    host = QWidget()
    hlay = QHBoxLayout(host)
    hlay.setContentsMargins(0, 0, 0, 0)
    hlay.setSpacing(0)
    hlay.addWidget(tree)
    hlay.addWidget(right, 1)
    lay.addWidget(host, 1)

    items: dict = {}

    def _selected_keys():
        keys = []
        for i in range(tree.topLevelItemCount()):
            it = tree.topLevelItem(i)
            if it and it.checkState(0) == Qt.CheckState.Checked:
                key = it.data(0, Qt.ItemDataRole.UserRole)
                if key:
                    keys.append(key)
        return keys

    def refresh_tree():
        tree.clear()
        items.clear()
        for sym in session.symbols():
            if sym.kind != "MEASUREMENT":
                continue
            it = QTreeWidgetItem(["", sym.name, "0x%X" % sym.address if sym.address else ""])
            it.setFlags(it.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            it.setCheckState(0, Qt.CheckState.Unchecked)
            it.setData(0, Qt.ItemDataRole.UserRole, (sym.kind, sym.name))
            tree.addTopLevelItem(it)
            items[(sym.kind, sym.name)] = it
        for key in session.measure_set:
            it = items.get(key)
            if it:
                it.setCheckState(0, Qt.CheckState.Checked)

    def refresh_values():
        keys = _selected_keys() or list(session.measure_set)
        table.setRowCount(len(keys))
        first = None
        for row, key in enumerate(keys):
            kind, name = key
            val = session.live_values.get(key, "—")
            table.setItem(row, 0, QTableWidgetItem(name))
            table.setItem(row, 1, QTableWidgetItem(str(val)))
            table.setItem(row, 2, QTableWidgetItem(kind))
            if first is None and val not in ("—", None, ""):
                first = val
        count.setText("%d live" % len(session.live_values))
        if first is not None:
            try:
                spark.push(float(str(first).replace("0x", ""), 16) if str(first).startswith("0x") else float(first))
            except ValueError:
                pass

    def on_start_poll():
        keys = _selected_keys()
        if not keys:
            log_fn("ERR", "-", b"", "Select at least one MEASUREMENT")
            return
        session.start_polling(keys)
        log_fn("SYS", "-", b"", "Polling %d signals" % len(keys))

    def on_stop_poll():
        session.stop_polling()
        log_fn("SYS", "-", b"", "Polling stopped")

    def on_start_daq():
        keys = _selected_keys()
        if keys:
            session.measure_set = list(keys)

        def done(ok, _p, note):
            log_fn("SYS" if ok else "ERR", "-", b"", note or "DAQ")

        session.start_daq(on_done=done)

    def on_stop_daq():
        def done(ok, _p, note):
            log_fn("SYS" if ok else "ERR", "-", b"", note or "DAQ stop")

        session.stop_daq(on_done=done)

    start_p.clicked.connect(on_start_poll)
    stop_p.clicked.connect(on_stop_poll)
    start_d.clicked.connect(on_start_daq)
    stop_d.clicked.connect(on_stop_daq)
    session.on_a2l_changed(refresh_tree)
    session.on_changed(refresh_values)
    refresh_tree()
    refresh_values()
    return root
