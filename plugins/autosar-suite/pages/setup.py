# -*- coding: utf-8 -*-
"""Setup — shared ids. One readable column, hints on the fields."""

from __future__ import annotations

from PyQt6.QtWidgets import QFormLayout, QWidget

from widgets import combo, spin, spin_hex, wrap_width


def build(_parent, session, _log):
    form = QFormLayout()
    form.setSpacing(10)
    nm = spin_hex(0, 0x7FF, session.nm_base, "CanNm base id. Node id is added to this.")
    e2e_id = spin_hex(0, 0x1FFFFFFF, session.e2e_can_id, "CAN id checked by E2E Profile 1")
    data_id = spin_hex(0, 0xFFFF, session.e2e_data_id, "E2E DataID")
    mode = combo(["BOTH", "LOW", "ALT"], "Which DataID bytes enter the CRC")
    mode.setCurrentText(session.e2e_mode if session.e2e_mode in ("BOTH", "LOW", "ALT") else "BOTH")
    sec = spin_hex(0, 0x1FFFFFFF, session.secoc_can_id, "CAN id checked by SecOC")

    form.addRow("NM base", nm)
    form.addRow("E2E CAN id", e2e_id)
    form.addRow("E2E DataID", data_id)
    form.addRow("E2E DataID mode", mode)
    form.addRow("SecOC CAN id", sec)

    nm.valueChanged.connect(lambda v: setattr(session, "nm_base", v))
    e2e_id.valueChanged.connect(lambda v: setattr(session, "e2e_can_id", v))
    data_id.valueChanged.connect(lambda v: setattr(session, "e2e_data_id", v))
    mode.currentTextChanged.connect(lambda t: setattr(session, "e2e_mode", t))
    sec.valueChanged.connect(lambda v: setattr(session, "secoc_can_id", v))

    host = QWidget()
    host.setLayout(form)
    return wrap_width(host)
