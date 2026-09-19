# -*- coding: utf-8 -*-
"""Signal bit map — same bit walk as _shared.dbcparse encode/decode."""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRect
from PyQt6.QtGui import QColor, QFont, QPainter, QPen
from PyQt6.QtWidgets import QWidget

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
    """Return (byte, visual_col) with visual_col 0 = MSB (bit 7) of the byte."""
    out = []
    length = int(getattr(sig, "bit_length", 0) or 0)
    if length <= 0 or nbytes <= 0:
        return out
    if getattr(sig, "little_endian", True):
        start = int(sig.start_bit)
        for i in range(length):
            bit = start + i
            byte = bit >> 3
            vis = bit & 7
            if 0 <= byte < nbytes:
                out.append((byte, vis))
    else:
        byte = int(sig.start_bit) >> 3
        bit = int(sig.start_bit) & 7
        for _ in range(length):
            if 0 <= byte < nbytes:
                out.append((byte, bit))
            bit += 1
            if bit == 8:
                bit = 0
                byte += 1
    return out


class _Draft:
    def __init__(self, name, start_bit, bit_length, little_endian):
        self.name = name
        self.start_bit = start_bit
        self.bit_length = bit_length
        self.little_endian = little_endian


class BitLayout(QWidget):
    """8-column bit grid for the selected message. Highlight one signal."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._rows = 8
        self._cells = {}
        self._legend = ""
        self.setMinimumHeight(72)
        self.setMinimumWidth(280)

    def clear(self):
        self._rows = 8
        self._cells = {}
        self._legend = ""
        self.update()

    def set_message(self, msg, highlight: str | None = None, draft=None):
        nbytes = max(1, min(int(getattr(msg, "dlc", 8) or 8), 64))
        cells = {}
        names = []
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
            if selected or not highlight:
                names.append(sig.name)
        self._rows = nbytes
        self._cells = cells
        if highlight:
            self._legend = highlight
        elif names:
            self._legend = "%d signals" % len(getattr(msg, "signals", []) or [])
        else:
            self._legend = "no signals"
        self.setMinimumHeight(28 + nbytes * 16 + 18)
        self.update()

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        p.fillRect(self.rect(), QColor("#FFFFFF"))
        font = QFont("Consolas", 8)
        p.setFont(font)
        left = 36
        top = 16
        cell = 16
        gap = 1
        p.setPen(QColor("#6E6E6E"))
        for col in range(8):
            label = str(7 - col)
            x = left + col * (cell + gap)
            p.drawText(QRect(x, 0, cell, 14), Qt.AlignmentFlag.AlignCenter, label)
        if not self._cells and not self._legend:
            p.setPen(QColor("#6E6E6E"))
            p.drawText(self.rect().adjusted(8, 8, -8, -8),
                       Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop,
                       "Bit layout appears when a message is selected.")
            p.end()
            return
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
                    p.setPen(QPen(QColor("#E5E5E5")))
                    p.drawRect(rect)
                else:
                    color, selected, _name = info
                    p.fillRect(rect, QColor(color))
                    if selected:
                        p.setPen(QPen(QColor("#1E1E1E"), 2))
                    else:
                        p.setPen(QPen(QColor("#FFFFFF")))
                    p.drawRect(rect.adjusted(0, 0, -1, -1))
        p.setPen(QColor("#333333"))
        p.drawText(QRect(left, top + self._rows * (cell + gap) + 2, 220, 16),
                   Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                   self._legend)
        p.end()
