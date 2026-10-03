# -*- coding: utf-8 -*-
"""Setup — cycle time used by the DC page."""

from __future__ import annotations

from PyQt6.QtWidgets import QFormLayout, QWidget

from _shared import vscode_theme
from widgets import spin, wrap_width


def build(_parent, session, _log):
    form = QFormLayout()
    form.setSpacing(10)
    cycle = spin(1000, 100_000_000, session.cycle_ns, "DC cycle time, nanoseconds")
    frame = spin(14, 1500, session.frame_bytes, "Assumed frame size for shift time")
    form.addRow(vscode_theme.field_label("Cycle ns"), cycle)
    form.addRow(vscode_theme.field_label("Frame bytes"), frame)
    vscode_theme.polish_form_labels(form)
    cycle.valueChanged.connect(lambda v: setattr(session, "cycle_ns", v))
    frame.valueChanged.connect(lambda v: setattr(session, "frame_bytes", v))
    host = QWidget()
    host.setLayout(form)
    return wrap_width(host)
