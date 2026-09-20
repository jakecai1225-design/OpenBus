# -*- coding: utf-8 -*-
"""CANdb++ layout window: columns 7..0, bit 0 is the LSB of byte 0."""

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


def cells_for_signal(sig, nbytes: int) -> list[tuple[int, int]]:
    """(byte, column) with column 0 on the left = bit 7 of that byte."""
    out = []
    length = int(getattr(sig, "bit_length", 0) or 0)
    if length <= 0 or nbytes <= 0:
        return out
    little = bool(getattr(sig, "little_endian", True))
    for bit in signal_bit_numbers(getattr(sig, "start_bit", 0), length, little):
        byte = bit >> 3
        col = 7 - (bit & 7)
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
    """CANdb++ byte/bit matrix. Click a colored cell to select that signal."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._rows = 8
        self._cells = {}
        self._legend = ""
        self.on_pick = None
        self._cell = 22
        self.setMinimumHeight(80)
        self.setMinimumWidth(240)
        self.setMouseTracking(True)

    def clear(self):
        self._rows = 8
        self._cells = {}
        self._legend = ""
        self.update()

    def set_message(self, msg, highlight: str | None = None, draft=None):
        nbytes = max(1, min(int(getattr(msg, "dlc", 8) or 8), 64))
        cells = {}
        for i, sig in enumerate(getattr(msg, "signals", []) or []):
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
        self._legend = highlight or ("%d signals" % len(getattr(msg, "signals", []) or []))
        cell = self._cell
        self.setMinimumHeight(18 + nbytes * (cell + 1) + 22)
        self.setMinimumWidth(40 + 8 * (cell + 1))
        self.update()

    def _hit(self, pos):
        cell = self._cell
        gap = 1
        left = 36
        top = 18
        col = (pos.x() - left) // (cell + gap)
        row = (pos.y() - top) // (cell + gap)
        if 0 <= col < 8 and 0 <= row < self._rows:
            return row, col
        return None

    def mousePressEvent(self, event):
        hit = self._hit(event.position().toPoint() if hasattr(event, "position") else event.pos())
        if hit is None:
            return
        info = self._cells.get(hit)
        if info and self.on_pick:
            self.on_pick(info[2])

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        p.fillRect(self.rect(), QColor("#FFFFFF"))
        font = QFont("Consolas", 8)
        p.setFont(font)
        left = 36
        top = 18
        cell = self._cell
        gap = 1
        p.setPen(QColor("#6E6E6E"))
        for col in range(8):
            x = left + col * (cell + gap)
            p.drawText(QRect(x, 0, cell, 16), Qt.AlignmentFlag.AlignCenter, str(7 - col))
        for row in range(self._rows):
            y = top + row * (cell + gap)
            p.setPen(QColor("#6E6E6E"))
            p.drawText(QRect(0, y, left - 4, cell),
                       Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                       "%d" % row)
            for col in range(8):
                x = left + col * (cell + gap)
                info = self._cells.get((row, col))
                rect = QRect(x, y, cell, cell)
                if info is None:
                    p.fillRect(rect, QColor("#F3F3F3"))
                    p.setPen(QPen(QColor("#D0D0D0")))
                    p.drawRect(rect)
                else:
                    color, selected, _name = info
                    p.fillRect(rect, QColor(color))
                    p.setPen(QPen(QColor("#1E1E1E") if selected else QColor("#FFFFFF"),
                                  2 if selected else 1))
                    p.drawRect(rect.adjusted(0, 0, -1, -1))
        p.setPen(QColor("#333333"))
        p.drawText(QRect(left, top + self._rows * (cell + gap) + 4, 280, 16),
                   Qt.AlignmentFlag.AlignLeft, self._legend or "Layout")
        p.end()
