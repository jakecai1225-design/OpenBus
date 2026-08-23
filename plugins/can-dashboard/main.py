# -*- coding: utf-8 -*-
"""can-dashboard 插件 — 实时仪表盘（CANoe Instrumentation / PCAN-Plot 风格）
功能：
- 加载 DBC，将信号绑定到仪表部件：圆形仪表（QPainter 自绘）/ 柱状 / 数字
- 每部件可配量程（默认取信号 min/max）与告警阈值（越限变红）
- 布局增删部件、暂停/继续刷新
- 纯订阅只读（不发送任何帧）
依赖: pip install PyQt6
"""

import math

from PyQt6.QtCore import Qt, QTimer, QRectF
from PyQt6.QtGui import QColor, QPen, QFont, QPainter
from PyQt6.QtWidgets import QWidget

import sin
import dbcparse

_dbc = None
_widgets = []          # [ {widget, msg, sig, warn} ]
_values = {}           # widget id -> (value, has_data)
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


class _GaugeWidget:
    """圆形仪表（QPainter 自绘）"""

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
        # 背景弧
        painter.setPen(QPen(QColor("#90a4ae"), 10, Qt.PenStyle.SolidLine,
                            Qt.PenCapStyle.RoundCap))
        painter.drawArc(QRectF(cx - r, cy - r, 2 * r, 2 * r), start_angle, span)
        # 值弧（越限红色）
        over = warn is not None and value >= warn
        color = QColor("#c62828") if over else QColor("#1565c0")
        painter.setPen(QPen(color, 10, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawArc(QRectF(cx - r, cy - r, 2 * r, 2 * r), start_angle,
                        int(span * ratio))
        # 指针
        angle = math.radians(210 - 240 * ratio)
        nx = cx + r * 0.72 * math.cos(angle)
        ny = cy - r * 0.72 * math.sin(angle)
        painter.setPen(QPen(QColor("#37474f"), 3))
        painter.drawLine(int(cx), int(cy), int(nx), int(ny))
        painter.setBrush(QColor("#37474f"))
        painter.drawEllipse(QRectF(cx - 5, cy - 5, 10, 10))
        # 文本
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
        painter.drawText(QRectF(cx - r, cy + r * 0.72, 2 * r, 16),
                         Qt.AlignmentFlag.AlignHCenter, "%.0f" % vmax)
        painter.drawText(QRectF(cx - r, cy - r * 0.30, 2 * r, 16),
                         Qt.AlignmentFlag.AlignHCenter, "%.0f" % vmin)


class _BarWidget:
    """柱状仪表"""

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
        painter.setPen(QColor("#90a4ae"))
        f2 = QFont()
        f2.setPointSize(7)
        painter.setFont(f2)
        painter.drawText(QRectF(x - 46, y0 - 2, 40, 14),
                         Qt.AlignmentFlag.AlignRight, "%.0f" % vmax)
        painter.drawText(QRectF(x - 46, y0 + h - 12, 40, 14),
                         Qt.AlignmentFlag.AlignRight, "%.0f" % vmin)


class _DigitalWidget:
    """数字仪表"""

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


class _Cell:
    """单个仪表部件（承载绘图回调 + 配置）"""

    PAINTERS = [_GaugeWidget, _BarWidget, _DigitalWidget]

    def __init__(self, canvas, msg, sig, kind, vmin, vmax, warn):
        global _next_wid
        self.wid = _next_wid
        _next_wid += 1
        self.canvas = canvas
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
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, "等待数据…")
            painter.setPen(QColor("#cfd8dc"))
            painter.drawRect(rect.adjusted(2, 2, -2, -2))
            return
        _Cell.PAINTERS[self.kind].paint(
            painter, rect, self.sig.name, self.value,
            self.vmin, self.vmax, self.sig.unit or "", self.warn)
        painter.setPen(QPen(QColor("#b0bec5"), 1))
        painter.drawRect(rect.adjusted(2, 2, -2, -2))


class _CanvasWidget(QWidget):
    """仪表画布：自绘所有部件（QWidget 子类，重写 paintEvent）"""

    def __init__(self):
        super().__init__()
        self.setMinimumSize(660, 420)
        self.setStyleSheet("background:#ffffff;")

    def paintEvent(self, event):
        painter = QPainter(self)
        try:
            self._paint_cells(painter)
        finally:
            painter.end()

    def _paint_cells(self, painter):
        cols = 3
        cells = [e["cell"] for e in _widgets]
        if not cells:
            painter.setPen(QColor("#90a4ae"))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                             "加载 DBC 后添加仪表部件（左上角「添加部件」）")
            return
        rows = (len(cells) + cols - 1) // cols
        w = self.width() / cols
        h = max(150.0, self.height() / rows)
        for i, cell in enumerate(cells):
            r = QRectF((i % cols) * w + 4, (i // cols) * h + 4,
                       w - 8, h - 8)
            cell.paint(painter, r)


def activate(context):
    global _dbc, _paused, _running
    _dbc = None
    _paused = False
    _running = True
    del _widgets[:]

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QScrollArea, QFileDialog, QMessageBox, QListWidget,
            QComboBox, QDialog, QDialogButtonBox, QFormLayout,
            QDoubleSpinBox, QInputDialog
        )
    except ImportError:
        sin.output.append("仪表盘插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("实时仪表盘")
    win.resize(1000, 560)

    central = QWidget()
    win.setCentralWidget(central)
    outer = QHBoxLayout(central)

    # 左侧控制面板
    side = QWidget()
    side.setMaximumWidth(240)
    sv = QVBoxLayout(side)
    dbc_btn = QPushButton("加载 DBC…")
    add_btn = QPushButton("添加部件…")
    del_btn = QPushButton("删除选中部件")
    pause_btn = QPushButton("暂停刷新")
    sv.addWidget(dbc_btn)
    sv.addWidget(add_btn)
    sv.addWidget(del_btn)
    sv.addWidget(pause_btn)
    sv.addWidget(QLabel("已绑定部件:"))
    wlist = QListWidget()
    sv.addWidget(wlist, 1)
    status = QLabel("未加载 DBC")
    status.setWordWrap(True)
    sv.addWidget(status)
    outer.addWidget(side)

    # 右侧仪表画布
    canvas = _CanvasWidget()
    scroll = QScrollArea()
    scroll.setWidget(canvas)
    scroll.setWidgetResizable(True)
    outer.addWidget(scroll, 1)

    def _refresh_list():
        wlist.clear()
        kinds = ["仪表", "柱状", "数字"]
        for e in _widgets:
            wlist.addItem("%s · %s" % (e["sig"].name, kinds[e["cell"].kind]))

    def _repaint():
        for e in _widgets:
            e["cell"].update_value()
        canvas.update()

    def _on_load_dbc():
        global _dbc
        path, _ = QFileDialog.getOpenFileName(win, "加载 DBC", "", "DBC (*.dbc)")
        if not path:
            return
        _dbc = dbcparse.parse_file(path)
        status.setText("DBC: %d 报文 / %d 信号（点击「添加部件」绑定）" % (
            len(_dbc.messages),
            sum(len(m.signals) for m in _dbc.messages.values())))
        if _dbc.warnings:
            sin.output.append("DBC 解析警告 %d 条（仪表盘）" % len(_dbc.warnings))

    def _on_add():
        if not _dbc or not _dbc.messages:
            QMessageBox.information(win, "提示", "请先加载 DBC")
            return
        dlg = QDialog(win)
        dlg.setWindowTitle("添加仪表部件")
        dlg.resize(360, 260)
        form = QFormLayout(dlg)
        msg_combo = QComboBox()
        for m in _dbc.messages.values():
            msg_combo.addItem("0x%X %s" % (m.can_id, m.name), m.can_id)
        sig_combo = QComboBox()
        kind_combo = QComboBox()
        kind_combo.addItems(["圆形仪表", "柱状", "数字"])
        vmin_spin = QDoubleSpinBox()
        vmax_spin = QDoubleSpinBox()
        warn_spin = QDoubleSpinBox()
        vmin_spin.setRange(-1e9, 1e9)
        vmax_spin.setRange(-1e9, 1e9)
        warn_spin.setRange(-1e9, 1e9)
        for sp in (vmin_spin, vmax_spin, warn_spin):
            sp.setDecimals(3)

        def _on_msg(idx):
            sig_combo.clear()
            m = _dbc.messages[msg_combo.itemData(idx)]
            for s in m.signals:
                sig_combo.addItem(s.name, s.name)

        def _on_sig(idx):
            m = _dbc.messages[msg_combo.currentData()]
            s = m.signal(sig_combo.itemData(idx))
            if s:
                vmin_spin.setValue(s.minimum)
                vmax_spin.setValue(max(s.maximum, s.minimum + 1))
                warn_spin.setValue(s.maximum)

        msg_combo.currentIndexChanged.connect(_on_msg)
        sig_combo.currentIndexChanged.connect(_on_sig)
        _on_msg(0)
        if sig_combo.count():
            _on_sig(0)
        form.addRow("报文:", msg_combo)
        form.addRow("信号:", sig_combo)
        form.addRow("部件类型:", kind_combo)
        form.addRow("量程下限:", vmin_spin)
        form.addRow("量程上限:", vmax_spin)
        form.addRow("告警阈值:", warn_spin)
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
        cell = _Cell(canvas, m, s, kind_combo.currentIndex(),
                     vmin_spin.value(), vmax_spin.value(),
                     warn_spin.value() if warn_spin.value() < vmax_spin.value() else None)
        _widgets.append({"cell": cell, "msg": m, "sig": s})
        _refresh_list()

    def _on_del():
        row = wlist.currentRow()
        if 0 <= row < len(_widgets):
            _values.pop(_widgets[row]["cell"].wid, None)
            del _widgets[row]
            _refresh_list()
            _repaint()

    def _on_pause():
        global _paused
        _paused = not _paused
        pause_btn.setText("继续刷新" if _paused else "暂停刷新")

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("canDashboard.open", _on_open_cmd, "可视化: 仪表盘")

    dbc_btn.clicked.connect(_on_load_dbc)
    add_btn.clicked.connect(_on_add)
    del_btn.clicked.connect(_on_del)
    pause_btn.clicked.connect(_on_pause)

    timer = QTimer()
    timer.timeout.connect(_repaint)
    timer.start(100)

    win.show()
    sin.output.append("仪表盘插件已加载（DBC 信号绑定，只读安全）")


def deactivate():
    global _running, _paused
    _running = False
    _paused = False
    sin.output.append("仪表盘插件已停用")
