# -*- coding: utf-8 -*-
"""Setup workspace — bus IDs, session, timing (keeps Diagnose free of chrome)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QCheckBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, vscode_theme, codicons
from widgets.step_spin import StepSpin


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(16)

    # ---- Connection ----
    conn, cl = vscode_theme.block(
        "Connection",
        "Tester and ECU CAN IDs. Apply writes them into every page that sends.",
    )
    grid = QGridLayout()
    grid.setHorizontalSpacing(10)
    grid.setVerticalSpacing(8)
    tx = StepSpin(session.tx_id, minimum=1, maximum=0x7FF, hex_mode=True, width=110)
    rx = StepSpin(session.rx_id, minimum=1, maximum=0x7FF, hex_mode=True, width=110)
    func = StepSpin(session.func_id, minimum=1, maximum=0x7FF, hex_mode=True, width=110)
    for col, (name, spin) in enumerate((("TX", tx), ("RX", rx), ("Func", func))):
        lab = QLabel(name)
        lab.setObjectName("BusStripLabel")
        grid.addWidget(lab, 0, col)
        grid.addWidget(spin, 1, col)
    func_check = QCheckBox("Functional addressing")
    func_check.setChecked(session.functional)
    func_check.setToolTip("Send on Func ID instead of TX")
    grid.addWidget(func_check, 1, 3)
    apply_btn = QPushButton("Apply")
    apply_btn.setObjectName("PrimaryButton")
    apply_btn.setFixedSize(96, 28)
    codicons.set_button(apply_btn, "apply", primary=True)
    grid.addWidget(apply_btn, 1, 4)
    grid.setColumnStretch(5, 1)
    cl.addLayout(grid)
    layout.addWidget(conn)

    # ---- Session ----
    sess, sl = vscode_theme.block(
        "Session",
        "Switch diagnostic session. Keep-alive sends TesterPresent 3E 80.",
    )
    brow = QHBoxLayout()
    brow.setSpacing(8)
    for code, label in ((0x01, "Default"), (0x03, "Extended"), (0x02, "Prog")):
        b = QPushButton(label)
        b.setObjectName("SecondaryButton")
        b.setFixedHeight(28)
        b.setCursor(Qt.CursorShape.PointingHandCursor)
        b.setToolTip("Session 0x%02X" % code)
        b.clicked.connect(lambda _=False, s=code: session.go_session(s))
        brow.addWidget(b)
    brow.addStretch(1)
    sl.addLayout(brow)
    foot = QHBoxLayout()
    session_label = QLabel("Session: unknown")
    session_label.setObjectName("SessionBadge")
    foot.addWidget(session_label)
    tp_check = QCheckBox("Keep-alive")
    tp_check.setChecked(session.tester_present)
    tp_check.setToolTip("TesterPresent every TP ms. Functional during flash.")
    tp_check.toggled.connect(lambda c: setattr(session, "tester_present", c))
    foot.addWidget(tp_check)
    foot.addStretch(1)
    sl.addLayout(foot)
    layout.addWidget(sess)

    # ---- Timing ----
    timing, tl = vscode_theme.block(
        "Timing",
        "P2 / P2* are response timeouts. TP is the keep-alive period. Apply writes them.",
    )
    trow = QHBoxLayout()
    trow.setSpacing(10)
    p2 = StepSpin(session.client.p2_ms, minimum=50, maximum=10000, suffix=" ms", width=100)
    p2s = StepSpin(session.client.p2star_ms, minimum=50, maximum=60000, suffix=" ms", width=100)
    tp = StepSpin(getattr(session, "tp_interval_ms", 2000), minimum=200, maximum=10000,
                  suffix=" ms", width=100)
    p2.setToolTip("P2 server timeout")
    p2s.setToolTip("P2* after NRC 0x78")
    tp.setToolTip("TesterPresent interval")
    for name, spin in (("P2", p2), ("P2*", p2s), ("TP", tp)):
        lab = QLabel(name)
        lab.setObjectName("BusStripLabel")
        trow.addWidget(lab)
        trow.addWidget(spin)
    trow.addStretch(1)
    tl.addLayout(trow)
    layout.addWidget(timing)

    # ---- Identify ----
    ident, il = vscode_theme.block(
        "Identify",
        "Read the usual identification DIDs (F186, F187, F18A, F18C, F190, F191, F195, F197).",
    )
    read_btn = QPushButton("Read ID")
    read_btn.setObjectName("SecondaryButton")
    read_btn.setFixedSize(100, 28)
    codicons.set_button(read_btn, "info")
    read_btn.clicked.connect(session.read_identity)
    il.addWidget(read_btn, 0, Qt.AlignmentFlag.AlignLeft)
    layout.addWidget(ident)
    layout.addStretch(1)

    def _on_apply():
        session.functional = func_check.isChecked()
        session.apply_ids(tx.value(), rx.value(), func.value())
        if hasattr(session, "set_timing"):
            session.set_timing(p2.value(), p2s.value(), tp.value())
        plugin_shell.set_status(parent, "Connection applied", 2000)

    apply_btn.clicked.connect(_on_apply)

    def _on_session(name: str):
        session_label.setText("Session: %s" % name)
        p2.setValue(min(10000, max(50, session.client.p2_ms)))
        p2s.setValue(min(60000, max(50, session.client.p2star_ms)))

    session.on_session_changed(_on_session)

    def _sync():
        tx.blockSignals(True)
        rx.blockSignals(True)
        func.blockSignals(True)
        tx.setValue(session.tx_id)
        rx.setValue(session.rx_id)
        func.setValue(session.func_id)
        tx.blockSignals(False)
        rx.blockSignals(False)
        func.blockSignals(False)
        func_check.setChecked(session.functional)
        tp_check.setChecked(session.tester_present)

    session.on_ids_changed(_sync)
    return root
