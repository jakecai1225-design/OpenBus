# -*- coding: utf-8 -*-
"""can-dashboard — live DBC gauges (CANoe Instrumentation / PCAN Panel style).

- Bind signals to gauge / bar / digital / LED / sparkline
- Layout JSON save/load via state_store
- Workspace DBC picker; snapshot CSV export
"""

from __future__ import annotations

import math
import time
from collections import deque

from PyQt6.QtCore import Qt, QTimer, QRectF
from PyQt6.QtGui import QColor, QPen, QFont, QPainter
from PyQt6.QtWidgets import QWidget

import sin
from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "can-dashboard"

_dbc = None
_dbc_path = ""
_widgets = []
_values = {}
_history = {}
_paused = False
_next_wid = 1
_running = True


def _on_frame(frame):
    if not _running or _paused:
        return
    for entry in _widgets:
        msg = entry["msg"]
        if frame.id != msg.can_id:
            continue
        v = dbcparse.signal_phys(entry["sig"], frame.data)
        if v is not None:
            _values[entry["wid"]] = (v, True)
            hist = _history.setdefault(entry["wid"], deque(maxlen=64))
            hist.append((time.time(), v))


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
                painter.drawLine(int(path_pts[i - 1][0]), int(path_pts[i - 1][1]),
                                 int(path_pts[i][0]), int(path_pts[i][1]))
        over = warn is not None and value >= warn
        painter.setPen(QColor("#c62828") if over else QColor("#1565c0"))
        painter.drawText(QRectF(rect.left(), rect.bottom() - 22, rect.width(), 18),
                         Qt.AlignmentFlag.AlignHCenter, "%.2f %s" % (value, unit))


KIND_NAMES = ["Gauge", "Bar", "Digital", "LED", "Sparkline"]
PAINTERS = [_GaugeWidget, _BarWidget, _DigitalWidget, _LedWidget, _SparklineWidget]


class _Cell:
    def __init__(self, msg, sig, kind, vmin, vmax, warn):
        global _next_wid
        self.wid = _next_wid
        _next_wid += 1
        self.msg = msg
        self.sig = sig
        self.kind = kind
        self.vmin = vmin
        self.vmax = vmax
        self.warn = warn
        self.value = 0.0
        self.has_data = False
        _values[self.wid] = (0.0, False)

    def update_value(self):
        v, has = _values.get(self.wid, (0.0, False))
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
                _history.get(self.wid))
        else:
            PAINTERS[self.kind].paint(
                painter, rect, self.sig.name, self.value,
                self.vmin, self.vmax, self.sig.unit or "", self.warn)
        painter.setPen(QPen(QColor("#b0bec5"), 1))
        painter.drawRect(rect.adjusted(2, 2, -2, -2))


class _CanvasWidget(QWidget):
    def __init__(self):
        super().__init__()
        self.setMinimumSize(660, 420)
        self.setStyleSheet("background:#ffffff;")
        self._empty = plugin_shell.empty_state_label(
            "Load a DBC and add widgets (Add widget). Layouts can be saved with Ctrl+S.")

    def paintEvent(self, event):
        painter = QPainter(self)
        try:
            cells = [e["cell"] for e in _widgets]
            if not cells:
                painter.setPen(QColor("#90a4ae"))
                painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                                 "Load a DBC, then Add widget to bind signals.")
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


def activate(context):
    global _dbc, _dbc_path, _paused, _running, _next_wid
    _dbc = None
    _dbc_path = ""
    _paused = False
    _running = True
    _next_wid = 1
    del _widgets[:]
    _values.clear()
    _history.clear()

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QScrollArea, QMessageBox, QListWidget, QComboBox, QDialog,
            QDialogButtonBox, QFormLayout, QDoubleSpinBox,
        )
    except ImportError:
        sin.output.append("can-dashboard needs PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN Dashboard")
    win.resize(1040, 580)
    plugin_shell.attach_status_bar(win, "No DBC loaded")

    central = QWidget()
    win.setCentralWidget(central)
    outer = QHBoxLayout(central)

    side = QWidget()
    side.setMaximumWidth(260)
    sv = QVBoxLayout(side)
    dbc_btn = QPushButton("Load DBC…")
    add_btn = QPushButton("Add widget…")
    del_btn = QPushButton("Remove selected")
    pause_btn = QPushButton("Pause")
    save_btn = QPushButton("Save layout")
    load_btn = QPushButton("Load layout")
    export_btn = QPushButton("Export snapshot CSV")
    for b in (dbc_btn, add_btn, del_btn, pause_btn, save_btn, load_btn, export_btn):
        sv.addWidget(b)
    sv.addWidget(plugin_shell.help_label(
        "Ctrl+S save layout · CSV exports current values · LED uses warn as threshold"))
    sv.addWidget(QLabel("Bound widgets:"))
    wlist = QListWidget()
    sv.addWidget(wlist, 1)
    outer.addWidget(side)

    canvas = _CanvasWidget()
    scroll = QScrollArea()
    scroll.setWidget(canvas)
    scroll.setWidgetResizable(True)
    outer.addWidget(scroll, 1)

    def _refresh_list():
        wlist.clear()
        for e in _widgets:
            wlist.addItem("%s · %s" % (e["sig"].name, KIND_NAMES[e["cell"].kind]))

    def _repaint():
        for e in _widgets:
            e["cell"].update_value()
        canvas.update()

    def _layout_data():
        items = []
        for e in _widgets:
            c = e["cell"]
            items.append({
                "can_id": e["msg"].can_id,
                "signal": e["sig"].name,
                "kind": c.kind,
                "vmin": c.vmin,
                "vmax": c.vmax,
                "warn": c.warn,
            })
        return {"dbc_path": _dbc_path, "widgets": items}

    def _apply_layout(data):
        global _dbc, _dbc_path
        if not data:
            return
        path = data.get("dbc_path") or ""
        if path:
            try:
                _dbc = dbcparse.parse_file(path)
                _dbc_path = path
            except OSError as ex:
                QMessageBox.warning(win, "Layout", "Cannot load DBC: %s" % ex)
                return
        del _widgets[:]
        _values.clear()
        _history.clear()
        if not _dbc:
            return
        for item in data.get("widgets", []):
            msg = _dbc.messages.get(int(item["can_id"]))
            if not msg:
                continue
            sig = msg.signal(item["signal"])
            if not sig:
                continue
            cell = _Cell(msg, sig, int(item.get("kind", 0)),
                         float(item.get("vmin", sig.minimum)),
                         float(item.get("vmax", max(sig.maximum, sig.minimum + 1))),
                         item.get("warn"))
            _widgets.append({"cell": cell, "msg": msg, "sig": sig})
        _refresh_list()
        plugin_shell.set_status(win, "Layout restored (%d widgets)" % len(_widgets))

    def _on_load_dbc():
        global _dbc, _dbc_path
        path = dbc_picker.pick_dbc(win, "Load DBC")
        if not path:
            return
        _dbc = dbcparse.parse_file(path)
        _dbc_path = path
        plugin_shell.set_status(
            win,
            "DBC: %d messages / %d signals" % (
                len(_dbc.messages),
                sum(len(m.signals) for m in _dbc.messages.values())),
        )

    def _on_add():
        if not _dbc or not _dbc.messages:
            QMessageBox.information(win, "Dashboard", "Load a DBC first")
            return
        dlg = QDialog(win)
        dlg.setWindowTitle("Add widget")
        dlg.resize(380, 280)
        form = QFormLayout(dlg)
        msg_combo = QComboBox()
        for m in _dbc.messages.values():
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
            m = _dbc.messages[msg_combo.itemData(idx)]
            for s in m.signals:
                sig_combo.addItem(s.name, s.name)

        def _on_sig(_idx):
            m = _dbc.messages[msg_combo.currentData()]
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
        btns = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                QDialogButtonBox.StandardButton.Cancel)
        btns.accepted.connect(dlg.accept)
        btns.rejected.connect(dlg.reject)
        form.addRow(btns)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        m = _dbc.messages[msg_combo.currentData()]
        s = m.signal(sig_combo.currentData())
        if not s:
            return
        warn = warn_spin.value() if warn_spin.value() < vmax_spin.value() else None
        cell = _Cell(m, s, kind_combo.currentIndex(),
                     vmin_spin.value(), vmax_spin.value(), warn)
        _widgets.append({"cell": cell, "msg": m, "sig": s})
        _refresh_list()

    def _on_del():
        row = wlist.currentRow()
        if 0 <= row < len(_widgets):
            wid = _widgets[row]["cell"].wid
            _values.pop(wid, None)
            _history.pop(wid, None)
            del _widgets[row]
            _refresh_list()
            _repaint()

    def _on_pause():
        global _paused
        _paused = not _paused
        pause_btn.setText("Resume" if _paused else "Pause")
        plugin_shell.set_status(win, "Paused" if _paused else "Live")

    def _on_save():
        path = state_store.save_state(PLUGIN_ID, _layout_data(), "layout.json")
        plugin_shell.set_status(win, "Saved %s" % path, 4000)

    def _on_load():
        data = state_store.load_state(PLUGIN_ID, "layout.json")
        if not data:
            QMessageBox.information(win, "Dashboard", "No saved layout")
            return
        _apply_layout(data)

    def _on_export():
        rows = []
        for e in _widgets:
            c = e["cell"]
            c.update_value()
            rows.append([
                "0x%X" % e["msg"].can_id, e["msg"].name, e["sig"].name,
                "%.6g" % c.value if c.has_data else "", KIND_NAMES[c.kind],
            ])
        path = plugin_shell.export_csv(
            win, ["can_id", "message", "signal", "value", "widget"], rows,
            "dashboard_snapshot.csv")
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    context.on_frame(_on_frame)
    context.register_command(
        "canDashboard.open", plugin_shell.bind_raise(win), "Visualization: Dashboard")

    dbc_btn.clicked.connect(_on_load_dbc)
    add_btn.clicked.connect(_on_add)
    del_btn.clicked.connect(_on_del)
    pause_btn.clicked.connect(_on_pause)
    save_btn.clicked.connect(_on_save)
    load_btn.clicked.connect(_on_load)
    export_btn.clicked.connect(_on_export)
    plugin_shell.bind_shortcut(win, "Ctrl+S", _on_save)
    plugin_shell.bind_shortcut(win, "Ctrl+E", _on_export)

    saved = state_store.load_state(PLUGIN_ID, "layout.json")
    if saved:
        _apply_layout(saved)

    timer = QTimer()
    timer.timeout.connect(_repaint)
    timer.start(100)

    win.show()
    sin.output.append("can-dashboard ready (DBC bind, layout persist, read-only)")


def deactivate():
    global _running, _paused
    _running = False
    _paused = False
    sin.output.append("can-dashboard deactivated")
