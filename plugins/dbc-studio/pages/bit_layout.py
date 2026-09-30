# -*- coding: utf-8 -*-
"""CANdb++ layout window: columns 7..0, bit 0 is the LSB of byte 0.

Click a colored cell to select that signal. With a signal selected, click or
drag cells to set start bit and length (live preview; Apply writes the DBC).
"""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRect
from PyQt6.QtGui import QColor, QFont, QPainter, QPen
from PyQt6.QtWidgets import QWidget

from _shared.dbcparse import signal_bit_numbers

_PALETTE = (
    "#007ACC",
    "#16825D",
    "#C5860A",
    "#A1260D",
    "#68217A",
    "#0E639C",
    "#B5200A",
    "#1B7C83",
)

_ACCENT = QColor("#005FB8")
_ACCENT_FILL = QColor(0, 95, 184, 90)


def cell_to_bit(row: int, col: int) -> int:
    """Layout cell → Vector bit number. Column 0 is bit 7 of that byte."""
    return int(row) * 8 + (7 - int(col))


def bit_to_cell(bit: int) -> tuple[int, int]:
    bit = int(bit)
    return bit >> 3, 7 - (bit & 7)


def range_from_drag(origin, current, little_endian: bool, nbytes: int):
    """(start_bit, length) from two layout cells, using Intel or Motorola walk."""
    if origin is None or current is None or nbytes <= 0:
        return None
    max_bit = nbytes * 8 - 1
    b0 = max(0, min(max_bit, cell_to_bit(*origin)))
    b1 = max(0, min(max_bit, cell_to_bit(*current)))
    if little_endian:
        start = min(b0, b1)
        length = min(64, abs(b1 - b0) + 1)
        return start, max(1, length)
    for start, end in ((b0, b1), (b1, b0)):
        bits = signal_bit_numbers(start, 64, False)
        if end not in bits:
            continue
        length = bits.index(end) + 1
        if any(b < 0 or b > max_bit for b in bits[:length]):
            continue
        return start, max(1, length)
    return b0, 1


def cells_for_signal(sig, nbytes: int) -> list[tuple[int, int]]:
    """(byte, column) with column 0 on the left = bit 7 of that byte."""
    out = []
    length = int(getattr(sig, "bit_length", 0) or 0)
    if length <= 0 or nbytes <= 0:
        return out
    little = bool(getattr(sig, "little_endian", True))
    for bit in signal_bit_numbers(getattr(sig, "start_bit", 0), length, little):
        byte, col = bit_to_cell(bit)
        if 0 <= byte < nbytes and 0 <= col < 8:
            out.append((byte, col))
    return out


class _Draft:
    def __init__(self, name, start_bit, bit_length, little_endian):
        self.name = name
        self.start_bit = start_bit
        self.bit_length = bit_length
        self.little_endian = little_endian


class BitLayout(QWidget):
    """Byte/bit matrix. Click a color to select; drag bits to edit placement."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._rows = 8
        self._cells = {}
        self._legend = ""
        self._highlight = None
        self._little = True
        self.on_pick = None
        self.on_range = None
        self._cell = 24
        self._press = None
        self._hover = None
        self._dragging = False
        self.setMinimumHeight(80)
        self.setMinimumWidth(240)
        self.setMouseTracking(True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setToolTip(
            "Click a colored cell to select that signal. "
            "With a signal selected, click or drag bits to set start and length.")

    def clear(self):
        self._rows = 8
        self._cells = {}
        self._legend = ""
        self._highlight = None
        self._press = None
        self._hover = None
        self._dragging = False
        self.update()

    def set_message(self, msg, highlight: str | None = None, draft=None):
        nbytes = max(1, min(int(getattr(msg, "dlc", 8) or 8), 64))
        cells = {}
        signals = getattr(msg, "signals", []) or []
        if draft is not None:
            self._little = bool(draft.little_endian)
        else:
            hs = next((s for s in signals if highlight and s.name == highlight), None)
            self._little = True if hs is None else bool(hs.little_endian)
        self._highlight = highlight
        focus_mux = None
        focus_mux_val = None
        for sig in signals:
            if highlight and sig.name == highlight:
                if sig.mux_type == "multiplexor":
                    focus_mux = "multiplexor"
                elif sig.mux_type == "multiplexed":
                    focus_mux = "multiplexed"
                    focus_mux_val = sig.mux_value
                break
        for i, sig in enumerate(signals):
            if focus_mux == "multiplexed" and sig.mux_type == "multiplexed":
                if sig.mux_value != focus_mux_val and sig.name != highlight:
                    continue
            src = sig
            if draft is not None and sig.name == highlight:
                src = draft
            color = _PALETTE[i % len(_PALETTE)]
            selected = bool(highlight) and sig.name == highlight
            for pos in cells_for_signal(src, nbytes):
                prev = cells.get(pos)
                if prev is None or selected:
                    cells[pos] = (color, selected, sig.name)
        self._rows = nbytes
        self._cells = cells
        if highlight:
            hs = next((s for s in signals if s.name == highlight), None)
            if hs and hs.mux_type == "multiplexor":
                self._legend = "%s  [M multiplexor]" % highlight
            elif hs and hs.mux_type == "multiplexed":
                self._legend = "%s  [m%d]" % (highlight, int(hs.mux_value or 0))
            else:
                self._legend = highlight
        else:
            n_mux = sum(1 for s in signals if s.mux_type)
            self._legend = "%d signals%s" % (
                len(signals),
                (" · %d mux" % n_mux) if n_mux else "")
        cell = self._cell
        self.setMinimumHeight(20 + nbytes * (cell + 1) + 24)
        self.setMinimumWidth(40 + 8 * (cell + 1))
        self.update()

    def _hit(self, pos):
        cell = self._cell
        gap = 1
        left = 40
        top = 20
        col = (pos.x() - left) // (cell + gap)
        row = (pos.y() - top) // (cell + gap)
        if 0 <= col < 8 and 0 <= row < self._rows:
            return int(row), int(col)
        return None

    def _draft_cells(self):
        if not self._press or not self._highlight:
            return set()
        current = self._hover or self._press
        rng = range_from_drag(self._press, current, self._little, self._rows)
        if rng is None:
            return set()
        start, length = rng
        out = set()
        for bit in signal_bit_numbers(start, length, self._little):
            row, col = bit_to_cell(bit)
            if 0 <= row < self._rows and 0 <= col < 8:
                out.add((row, col))
        return out

    def _finish_gesture(self, hit):
        origin = self._press
        self._press = None
        self._hover = None
        dragging = self._dragging
        self._dragging = False
        if origin is None:
            return
        cell = hit or origin
        info = self._cells.get(origin)
        other = info[2] if info else None
        if not dragging and other and other != self._highlight:
            if callable(self.on_pick):
                self.on_pick(other)
            self.update()
            return
        if not self._highlight:
            if other and callable(self.on_pick):
                self.on_pick(other)
            self.update()
            return
        rng = range_from_drag(origin, cell, self._little, self._rows)
        if rng and callable(self.on_range):
            self.on_range(rng[0], rng[1])
        self.update()

    def mousePressEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton:
            return
        pos = event.position().toPoint() if hasattr(event, "position") else event.pos()
        hit = self._hit(pos)
        if hit is None:
            return
        self._press = hit
        self._hover = hit
        self._dragging = False
        self.update()

    def mouseMoveEvent(self, event):
        pos = event.position().toPoint() if hasattr(event, "position") else event.pos()
        hit = self._hit(pos)
        if hit is not None:
            bit = cell_to_bit(*hit)
            self.setToolTip("bit %d  ·  byte %d  ·  bit %d of byte" % (
                bit, hit[0], 7 - hit[1]))
        if self._press is None:
            return
        if hit is not None and hit != self._press:
            self._dragging = True
            self._hover = hit
            self.update()
        elif hit is not None:
            self._hover = hit

    def mouseReleaseEvent(self, event):
        if event.button() != Qt.MouseButton.LeftButton:
            return
        pos = event.position().toPoint() if hasattr(event, "position") else event.pos()
        self._finish_gesture(self._hit(pos))

    def leaveEvent(self, _event):
        if self._press is None:
            self._hover = None

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        p.fillRect(self.rect(), QColor("#FFFFFF"))
        font = QFont("Consolas", 10)
        p.setFont(font)
        left = 40
        top = 20
        cell = self._cell
        gap = 1
        draft = self._draft_cells()
        p.setPen(QColor("#6E6E6E"))
        for col in range(8):
            x = left + col * (cell + gap)
            p.drawText(QRect(x, 0, cell, 18), Qt.AlignmentFlag.AlignCenter, str(7 - col))
        for row in range(self._rows):
            y = top + row * (cell + gap)
            p.setPen(QColor("#6E6E6E"))
            p.drawText(QRect(0, y, left - 6, cell),
                       Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                       "%d" % row)
            for col in range(8):
                x = left + col * (cell + gap)
                info = self._cells.get((row, col))
                rect = QRect(x, y, cell, cell)
                in_draft = (row, col) in draft
                if info is None:
                    p.fillRect(rect, _ACCENT_FILL if in_draft else QColor("#F3F3F3"))
                    p.setPen(QPen(_ACCENT if in_draft else QColor("#D0D0D0"),
                                  2 if in_draft else 1))
                    p.drawRect(rect.adjusted(0, 0, -1, -1))
                else:
                    color, selected, _name = info
                    p.fillRect(rect, QColor(color))
                    if in_draft:
                        p.fillRect(rect, _ACCENT_FILL)
                    p.setPen(QPen(
                        _ACCENT if in_draft else (
                            QColor("#1E1E1E") if selected else QColor("#FFFFFF")),
                        2 if (selected or in_draft) else 1))
                    p.drawRect(rect.adjusted(0, 0, -1, -1))
        p.setPen(QColor("#333333"))
        legend = self._legend or "Layout"
        if self._press and self._highlight:
            current = self._hover or self._press
            rng = range_from_drag(self._press, current, self._little, self._rows)
            if rng:
                legend = "%s  ·  start %d  len %d" % (
                    self._highlight, rng[0], rng[1])
        p.drawText(QRect(left, top + self._rows * (cell + gap) + 4, 280, 16),
                   Qt.AlignmentFlag.AlignLeft, legend)
        p.end()
