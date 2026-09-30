# -*- coding: utf-8 -*-
"""Trace workspace — filtered view of the shared offline corpus."""

from __future__ import annotations

from PyQt6.QtCore import Qt, QRect
from PyQt6.QtGui import QColor, QPainter
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import vscode_theme
from core.report import rate_bins, summarize


class _RateStrip(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._bins = []
        self.setMinimumHeight(48)
        self.setMaximumHeight(64)

    def set_bins(self, bins):
        self._bins = list(bins or [])
        self.update()

    def paintEvent(self, _event):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#FFFFFF"))
        if not self._bins:
            p.setPen(QColor("#6E6E6E"))
            p.drawText(self.rect().adjusted(8, 0, -8, 0),
                       Qt.AlignmentFlag.AlignVCenter, "No samples")
            p.end()
            return
        peak = max(self._bins) or 1
        n = len(self._bins)
        gap = 2
        w = max(2, (self.width() - 8 - gap * (n - 1)) // n)
        h = self.height() - 8
        for i, count in enumerate(self._bins):
            bh = int(h * count / peak)
            x = 4 + i * (w + gap)
            p.fillRect(QRect(x, 4 + h - bh, w, bh), QColor("#007ACC"))
        p.end()


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "Trace",
        "Shows frames loaded on Open. Filter is a hex ID fragment, for example 100.")
    row = QHBoxLayout()
    filt = QLineEdit()
    filt.setPlaceholderText("ID filter")
    summary = QLabel("No corpus")
    row.addWidget(QLabel("Filter"))
    row.addWidget(filt, 1)
    row.addWidget(summary)
    body.addLayout(row)
    layout.addWidget(card)

    strip = _RateStrip()
    layout.addWidget(strip)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Time s", "CH", "Dir", "ID", "DLC", "Data"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(5, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    def _match(cid, text):
        text = text.strip().lower().replace("0x", "")
        if not text:
            return True
        return text in ("%x" % cid)

    def _refresh():
        frames = session.frames
        info = summarize(frames)
        if not frames:
            summary.setText("Load logs on Open")
        else:
            summary.setText("%d frames · %d IDs · %.1f /s" % (
                info["count"], info["ids"], info["fps"]))
        strip.set_bins(rate_bins(frames))
        text = filt.text()
        shown = [fr for fr in frames if _match(fr[2], text)]
        tail = shown[-600:]
        tree.clear()
        for ts, ch, cid, direction, dlc, data in tail:
            tree.addTopLevelItem(QTreeWidgetItem([
                "%.6f" % ts, str(ch), direction, "0x%X" % cid, str(dlc),
                " ".join("%02X" % b for b in data),
            ]))
        tree.scrollToBottom()

    filt.textChanged.connect(lambda _t: _refresh())
    session.on_corpus_changed(_refresh)
    _refresh()
    return root
