# -*- coding: utf-8 -*-
"""Distributed clocks — cable delay and a shift suggestion. Not a DC master."""

from __future__ import annotations

from PyQt6.QtWidgets import QHBoxLayout, QTableWidgetItem, QVBoxLayout, QWidget

from core.dc import shift_ns, system_delay_ns
from widgets import primary, spin, table


def build(_parent, session, _log):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    cycle = spin(1000, 100_000_000, session.cycle_ns, "Cycle time, nanoseconds")
    frame = spin(14, 1500, session.frame_bytes, "Frame size on the wire, bytes")
    calc = primary("Recalculate", "Sum cable delay and suggest a SYNC0 shift")
    row = QHBoxLayout()
    row.addWidget(cycle)
    row.addWidget(frame)
    row.addWidget(calc)
    row.addStretch(1)
    root.addLayout(row)
    grid = table(["Pos", "Name", "Cable m", "Delay ns", "Shift ns"])
    root.addWidget(grid, 1)

    def run():
        session.cycle_ns = cycle.value()
        session.frame_bytes = frame.value()
        lengths = [s.cable_m for s in session.slaves]
        delays = system_delay_ns(lengths) if lengths else []
        grid.setRowCount(0)
        for slave, delay in zip(session.slaves, delays):
            r = grid.rowCount()
            grid.insertRow(r)
            shift = shift_ns(session.cycle_ns, session.frame_bytes, delay)
            vals = (
                str(slave.position),
                slave.name,
                "%.2f" % slave.cable_m,
                "%.1f" % delay,
                "%.1f" % shift,
            )
            for c, text in enumerate(vals):
                grid.setItem(r, c, QTableWidgetItem(text))

    calc.clicked.connect(run)
    session.on_changed(run)
    run()
    return page
