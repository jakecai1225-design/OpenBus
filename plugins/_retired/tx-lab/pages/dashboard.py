# -*- coding: utf-8 -*-
"""Dashboard workspace — LED/sparkline panel (from can-dashboard)."""

from __future__ import annotations

import math
import time
from collections import deque

from PyQt6.QtCore import Qt, QTimer, QRectF
from PyQt6.QtGui import QColor, QPen, QFont, QPainter
from PyQt6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QDoubleSpinBox,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from _shared import dbcparse, plugin_shell, state_store

PLUGIN_ID = "tx-lab"

KIND_NAMES = ["Gauge", "Bar", "Digital", "LED", "Sparkline"]


class _GaugeWidget:
    @staticmethod
    def paint(painter, rect, title, value, vmin, vmax, unit, warn):
        if vmax <= vmin:
            vmax = vmin + 1.0
        ratio = max(0.0, min(1.0, (value - vmin) / (max(1e-9, vmax - vmin))))
        cx = rect.center().x()
        cy = rect.top() + rect.height() * 0.62
        r = min(rect.width() * 0.42, rect.height() * 0.52)
        start_angle = 210 * 16
        span = -240 * 16
        painter.setPen(QPen(QColor("#90a4ae"), 10, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap))
        painter.drawArc(QRectF(cx - r, cy - r, 2 * r, 2 * r), start_angle, span)
        over = warn is not None and value >= warn
        color = QColor("#c62828") if over else QColor("#1565c0")
        painter.setPen(QPen(color, 10, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawArc(QRectF(cx - r, cy - r, 2 * r, 2 * r), start_angle,
                        int(span * ratio))
        angle = math.radians(210 - 240 * ratio)
        nx = cx + r * 0.72 * math.cos(angle)
        ny = cy - r * 0.72 * math.sin(angle)
        painter.setPen(QPen(QColor("#37474f"), 3))
        painter.drawLine(int(cx), int(cy), int(nx), int(ny))
        painter.setBrush(QColor("#37474f"))
        painter.drawEllipse(QRectF(cx - 5, cy - 5, 10, 10))
        painter.setPen(QColor(color))
        f = QFont()
        f.setPointSize(int(max(9, r * 0.22)))
        f.setBold(True)
        painter.setFont(f)
        painter.drawText(QRectF(cx - r, cy + r * 0.30, 2 * r, r * 0.5),
                         Qt.AlignmentFlag.AlignHCenter,
                         "%.1f %s" % (value, unit))
        painter.setPen(QColor("#546e7a"))
        f2 = QFont()
        f2.setPointSize(8)
        painter.setFont(f2)
        painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), 20),
                         Qt.AlignmentFlag.AlignHCenter, title)


class _BarWidget:
    @staticmethod
    def paint(painter, rect, title, value, vmin, vmax, unit, warn):
        if vmax <= vmin:
            vmax = vmin + 1.0
        ratio = max(0.0, min(1.0, (value - vmin) / (max(1e-9, vmax - vmin))))
        over = warn is not None and value >= warn
        color = QColor("#c62828") if over else QColor("#2e7d32")
        bar_w = rect.width() * 0.34
        x = rect.center().x() - bar_w / 2
        y0 = rect.top() + 30
        h = rect.height() - 66
        painter.setPen(QPen(QColor("#cfd8dc"), 1))
        painter.setBrush(QColor("#eceff1"))
        painter.drawRect(QRectF(x, y0, bar_w, h))
        painter.setBrush(color)
        painter.drawRect(QRectF(x, y0 + h * (1 - ratio), bar_w, h * ratio))
        painter.setPen(QColor("#546e7a"))
        painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), 20),
                         Qt.AlignmentFlag.AlignHCenter, title)
        f = QFont()
        f.setPointSize(10)
        f.setBold(True)
        painter.setFont(f)
        painter.setPen(color)
        painter.drawText(QRectF(rect.left(), y0 + h + 4, rect.width(), 24),
                         Qt.AlignmentFlag.AlignHCenter, "%.1f %s" % (value, unit))


class _DigitalWidget:
    @staticmethod
    def paint(painter, rect, title, value, vmin, vmax, unit, warn):
        over = warn is not None and value >= warn
        color = QColor("#c62828") if over else QColor("#1565c0")
        painter.setPen(QColor("#546e7a"))
        painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), 20),
                         Qt.AlignmentFlag.AlignHCenter, title)
        f = QFont()
        f.setPointSize(int(max(14, rect.width() * 0.14)))
        f.setBold(True)
        painter.setFont(f)
        painter.setPen(color)
        painter.drawText(QRectF(rect.left(), rect.top() + rect.height() * 0.30,
                                rect.width(), rect.height() * 0.40),
                         Qt.AlignmentFlag.AlignCenter, "%.2f" % value)
        painter.setPen(QColor("#78909c"))
        f2 = QFont()
        f2.setPointSize(10)
        painter.setFont(f2)
        painter.drawText(QRectF(rect.left(), rect.top() + rect.height() * 0.68,
                                rect.width(), 20),
                         Qt.AlignmentFlag.AlignHCenter, unit)


class _LedWidget:
    @staticmethod
    def paint(painter, rect, title, value, vmin, vmax, unit, warn):
        thresh = warn if warn is not None else ((vmin + vmax) / 2.0)
        on = value >= thresh
        color = QColor("#c62828") if on else QColor("#90a4ae")
        painter.setPen(QColor("#546e7a"))
        painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), 20),
                         Qt.AlignmentFlag.AlignHCenter, title)
        cx = rect.center().x()
        cy = rect.center().y()
        r = min(rect.width(), rect.height()) * 0.22
        painter.setBrush(color)
        painter.setPen(QPen(QColor("#455a64"), 2))
        painter.drawEllipse(QRectF(cx - r, cy - r, 2 * r, 2 * r))
        painter.setPen(QColor("#37474f"))
        painter.drawText(QRectF(rect.left(), cy + r + 8, rect.width(), 20),
                         Qt.AlignmentFlag.AlignHCenter,
                         "ON" if on else "OFF")


class _SparklineWidget:
    @staticmethod
    def paint(painter, rect, title, value, vmin, vmax, unit, warn, history=None):
        painter.setPen(QColor("#546e7a"))
        painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), 18),
                         Qt.AlignmentFlag.AlignHCenter, title)
        plot = QRectF(rect.left() + 8, rect.top() + 24,
                      rect.width() - 16, rect.height() - 48)
        painter.setPen(QPen(QColor("#cfd8dc"), 1))
        painter.drawRect(plot)
        pts = list(history or [])
        if len(pts) >= 2:
            ys = [p[1] for p in pts]
            lo = min(ys) if vmin is None else vmin
            hi = max(ys) if vmax is None else vmax
            if hi <= lo:
                hi = lo + 1.0
            painter.setPen(QPen(QColor("#1565c0"), 2))
            path_pts = []
            for i, (_, v) in enumerate(pts):
                x = plot.left() + plot.width() * i / max(1, len(pts) - 1)
                y = plot.bottom() - plot.height() * (v - lo) / (hi - lo)
                path_pts.append((x, y))
            for i in range(1, len(path_pts)):
                painter.drawLine(
                    int(path_pts[i - 1][0]), int(path_pts[i - 1][1]),
                    int(path_pts[i][0]), int(path_pts[i][1]))
        over = warn is not None and value >= warn
        painter.setPen(QColor("#c62828") if over else QColor("#1565c0"))
        painter.drawText(QRectF(rect.left(), rect.bottom() - 22, rect.width(), 18),
                         Qt.AlignmentFlag.AlignHCenter, "%.2f %s" % (value, unit))


PAINTERS = [_GaugeWidget, _BarWidget, _DigitalWidget, _LedWidget, _SparklineWidget]


class _Cell:
    def __init__(self, wid, msg, sig, kind, vmin, vmax, warn, values, history):
        self.wid = wid
        self.msg = msg
        self.sig = sig
        self.kind = kind
        self.vmin = vmin
        self.vmax = vmax
        self.warn = warn
        self.value = 0.0
        self.has_data = False
        self._values = values
        self._history = history
        values[self.wid] = (0.0, False)

    def update_value(self):
        v, has = self._values.get(self.wid, (0.0, False))
        self.value = v
        self.has_data = has

    def paint(self, painter, rect):
        if not self.has_data:
            painter.setPen(QColor("#b0bec5"))
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, "Waiting for data…")
            painter.setPen(QColor("#cfd8dc"))
            painter.drawRect(rect.adjusted(2, 2, -2, -2))
            return
        if self.kind == 4:
            PAINTERS[4].paint(
                painter, rect, self.sig.name, self.value,
                self.vmin, self.vmax, self.sig.unit or "", self.warn,
                self._history.get(self.wid))
        else:
            PAINTERS[self.kind].paint(
                painter, rect, self.sig.name, self.value,
                self.vmin, self.vmax, self.sig.unit or "", self.warn)
        painter.setPen(QPen(QColor("#b0bec5"), 1))
        painter.drawRect(rect.adjusted(2, 2, -2, -2))


class _CanvasWidget(QWidget):
    def __init__(self, get_widgets):
        super().__init__()
        self.setMinimumSize(660, 420)
        self.setStyleSheet("background:#ffffff;")
        self._get_widgets = get_widgets

    def paintEvent(self, event):
        painter = QPainter(self)
        try:
            widgets = self._get_widgets()
            cells = [e["cell"] for e in widgets]
            if not cells:
                painter.setPen(QColor("#90a4ae"))
                painter.drawText(
                    self.rect(), Qt.AlignmentFlag.AlignCenter,
                    "Load a DBC on the strip, then Add widget to bind signals.")
                return
            cols = 3
            rows = (len(cells) + cols - 1) // cols
            w = self.width() / cols
            h = max(150.0, self.height() / rows)
            for i, cell in enumerate(cells):
                r = QRectF((i % cols) * w + 4, (i // cols) * h + 4, w - 8, h - 8)
                cell.paint(painter, r)
        finally:
            painter.end()


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    outer = QHBoxLayout(root)

    widgets: list = []
    values: dict = {}
    history: dict = {}
    next_wid = {"n": 1}
    paused = {"v": False}

    side = QWidget()
    side.setMaximumWidth(260)
    sv = QVBoxLayout(side)
    add_btn = QPushButton("Add widget…")
    del_btn = QPushButton("Remove selected")
    pause_btn = QPushButton("Pause")
    save_btn = QPushButton("Save layout")
    load_btn = QPushButton("Load layout")
    export_btn = QPushButton("Export snapshot CSV")
    for b in (add_btn, del_btn, pause_btn, save_btn, load_btn, export_btn):
        sv.addWidget(b)
    sv.addWidget(plugin_shell.help_label(
        "Uses suite DBC. Ctrl+E exports CSV. LED uses warn as threshold."))
    sv.addWidget(QLabel("Bound widgets:"))
    wlist = QListWidget()
    sv.addWidget(wlist, 1)
    outer.addWidget(side)

    canvas = _CanvasWidget(lambda: widgets)
    scroll = QScrollArea()
    scroll.setWidget(canvas)
    scroll.setWidgetResizable(True)
    outer.addWidget(scroll, 1)

    def _log(text, color=None):
        log_fn("DASH", text, color)

    def _refresh_list():
        wlist.clear()
        for e in widgets:
            wlist.addItem("%s · %s" % (e["sig"].name, KIND_NAMES[e["cell"].kind]))

    def _repaint():
        for e in widgets:
            e["cell"].update_value()
        canvas.update()

    def _on_frame(frame):
        if paused["v"]:
            return
        for entry in widgets:
            msg = entry["msg"]
            if frame.id != msg.can_id:
                continue
            v = dbcparse.signal_phys(entry["sig"], frame.data)
            if v is not None:
                wid = entry["cell"].wid
                values[wid] = (v, True)
                hist = history.setdefault(wid, deque(maxlen=64))
                hist.append((time.time(), v))

    def _layout_data():
        items = []
        for e in widgets:
            c = e["cell"]
            items.append({
                "can_id": e["msg"].can_id,
                "signal": e["sig"].name,
                "kind": c.kind,
                "vmin": c.vmin,
                "vmax": c.vmax,
                "warn": c.warn,
            })
        return {"dbc_path": session.dbc_path or "", "widgets": items}

    def _apply_layout(data):
        if not data:
            return
        path = data.get("dbc_path") or ""
        if path and (not session.dbc_path or session.dbc_path != path):
            if not session.load_dbc(path):
                return
        db = session.dbc
        del widgets[:]
        values.clear()
        history.clear()
        if not db:
            return
        for item in data.get("widgets", []):
            msg = db.messages.get(int(item["can_id"]))
            if not msg:
                continue
            sig = msg.signal(item["signal"])
            if not sig:
                continue
            wid = next_wid["n"]
            next_wid["n"] += 1
            cell = _Cell(
                wid, msg, sig, int(item.get("kind", 0)),
                float(item.get("vmin", sig.minimum)),
                float(item.get("vmax", max(sig.maximum, sig.minimum + 1))),
                item.get("warn"), values, history)
            widgets.append({"cell": cell, "msg": msg, "sig": sig})
        _refresh_list()
        _log("Layout restored (%d widgets)" % len(widgets))
        plugin_shell.set_status(
            parent, "Layout restored (%d widgets)" % len(widgets))

    def _on_add():
        db = session.dbc
        if not db or not db.messages:
            QMessageBox.information(root, "Dashboard", "Load a DBC first")
            return
        dlg = QDialog(root)
        dlg.setWindowTitle("Add widget")
        dlg.resize(380, 280)
        form = QFormLayout(dlg)
        msg_combo = QComboBox()
        for m in db.messages.values():
            msg_combo.addItem("0x%X %s" % (m.can_id, m.name), m.can_id)
        sig_combo = QComboBox()
        kind_combo = QComboBox()
        kind_combo.addItems(KIND_NAMES)
        vmin_spin = QDoubleSpinBox()
        vmax_spin = QDoubleSpinBox()
        warn_spin = QDoubleSpinBox()
        for sp in (vmin_spin, vmax_spin, warn_spin):
            sp.setRange(-1e9, 1e9)
            sp.setDecimals(3)

        def _on_msg(idx):
            sig_combo.clear()
            m = db.messages[msg_combo.itemData(idx)]
            for s in m.signals:
                sig_combo.addItem(s.name, s.name)

        def _on_sig(_idx):
            m = db.messages[msg_combo.currentData()]
            s = m.signal(sig_combo.currentData())
            if s:
                vmin_spin.setValue(s.minimum)
                vmax_spin.setValue(max(s.maximum, s.minimum + 1))
                warn_spin.setValue(s.maximum)

        msg_combo.currentIndexChanged.connect(_on_msg)
        sig_combo.currentIndexChanged.connect(_on_sig)
        _on_msg(0)
        if sig_combo.count():
            _on_sig(0)
        form.addRow("Message:", msg_combo)
        form.addRow("Signal:", sig_combo)
        form.addRow("Widget:", kind_combo)
        form.addRow("Min:", vmin_spin)
        form.addRow("Max:", vmax_spin)
        form.addRow("Warn / LED thresh:", warn_spin)
        btns = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel)
        btns.accepted.connect(dlg.accept)
        btns.rejected.connect(dlg.reject)
        form.addRow(btns)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        m = db.messages[msg_combo.currentData()]
        s = m.signal(sig_combo.currentData())
        if not s:
            return
        warn = warn_spin.value() if warn_spin.value() < vmax_spin.value() else None
        wid = next_wid["n"]
        next_wid["n"] += 1
        cell = _Cell(
            wid, m, s, kind_combo.currentIndex(),
            vmin_spin.value(), vmax_spin.value(), warn, values, history)
        widgets.append({"cell": cell, "msg": m, "sig": s})
        _refresh_list()
        _log("Added widget %s" % s.name)

    def _on_del():
        row = wlist.currentRow()
        if 0 <= row < len(widgets):
            wid = widgets[row]["cell"].wid
            values.pop(wid, None)
            history.pop(wid, None)
            del widgets[row]
            _refresh_list()
            _repaint()

    def _on_pause():
        paused["v"] = not paused["v"]
        pause_btn.setText("Resume" if paused["v"] else "Pause")
        plugin_shell.set_status(parent, "Paused" if paused["v"] else "Live")

    def _on_save():
        path = state_store.save_state(PLUGIN_ID, _layout_data(), "layout.json")
        plugin_shell.set_status(parent, "Saved %s" % path, 4000)

    def _on_load():
        data = state_store.load_state(PLUGIN_ID, "layout.json")
        if not data:
            QMessageBox.information(root, "Dashboard", "No saved layout")
            return
        _apply_layout(data)

    def _on_export():
        rows = []
        for e in widgets:
            c = e["cell"]
            c.update_value()
            rows.append([
                "0x%X" % e["msg"].can_id, e["msg"].name, e["sig"].name,
                "%.6g" % c.value if c.has_data else "", KIND_NAMES[c.kind],
            ])
        path = plugin_shell.export_csv(
            parent, ["can_id", "message", "signal", "value", "widget"], rows,
            "dashboard_snapshot.csv")
        if path:
            plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    session.on_bus_frame(_on_frame)

    add_btn.clicked.connect(_on_add)
    del_btn.clicked.connect(_on_del)
    pause_btn.clicked.connect(_on_pause)
    save_btn.clicked.connect(_on_save)
    load_btn.clicked.connect(_on_load)
    export_btn.clicked.connect(_on_export)
    plugin_shell.bind_shortcut(parent, "Ctrl+E", _on_export)

    saved = state_store.load_state(PLUGIN_ID, "layout.json")
    if saved:
        _apply_layout(saved)

    timer = QTimer(root)
    timer.timeout.connect(_repaint)
    timer.start(100)

    return root
