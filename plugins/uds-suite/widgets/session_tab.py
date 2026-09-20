# -*- coding: utf-8 -*-
"""Session tab — bus IDs, diagnostic session, keep-alive, timing.

Whitespace separates sections (no loud dividers). Hints live in tooltips so
content stays primary.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QButtonGroup,
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, vscode_theme, codicons
from widgets.step_spin import StepSpin


def _field(label: str, widget: QWidget, tip: str = "", label_w: int = 36) -> QWidget:
    wrap = QWidget()
    row = QHBoxLayout(wrap)
    row.setContentsMargins(0, 0, 0, 0)
    row.setSpacing(6)
    lab = QLabel(label)
    lab.setObjectName("SuiteFieldLabel")
    lab.setFixedWidth(label_w)
    lab.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
    if tip:
        lab.setToolTip(tip)
        widget.setToolTip(tip)
    row.addWidget(lab)
    row.addWidget(widget, 0, Qt.AlignmentFlag.AlignVCenter)
    return wrap


def _section(title: str, tip: str = "") -> tuple[QWidget, QVBoxLayout]:
    """Title only — tip on hover, not a second text row that adds noise."""
    card = QWidget()
    card.setObjectName("SuiteSettingsSection")
    outer = QVBoxLayout(card)
    outer.setContentsMargins(0, 10, 0, 4)
    outer.setSpacing(8)
    t = QLabel(title)
    t.setObjectName("SuiteSectionTitle")
    if tip:
        t.setToolTip(tip)
    outer.addWidget(t)
    body = QVBoxLayout()
    body.setContentsMargins(0, 0, 0, 0)
    body.setSpacing(8)
    outer.addLayout(body)
    return card, body


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    root.setObjectName("SuiteSettingsPage")
    outer = QVBoxLayout(root)
    outer.setContentsMargins(20, 12, 20, 12)
    outer.setSpacing(0)

    content = QWidget()
    content.setMaximumWidth(680)
    layout = QVBoxLayout(content)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(4)

    # Connection
    conn, cl = _section(
        "Connection",
        "TX = tester request ID, RX = ECU response ID, Func = functional address. "
        "Apply once — used by every Diagnose action.")
    crow = QHBoxLayout()
    crow.setSpacing(14)
    tx = StepSpin(session.tx_id, minimum=1, maximum=0x7FF, hex_mode=True, width=92)
    rx = StepSpin(session.rx_id, minimum=1, maximum=0x7FF, hex_mode=True, width=92)
    func = StepSpin(session.func_id, minimum=1, maximum=0x7FF, hex_mode=True, width=92)
    crow.addWidget(_field("TX", tx, "Tester request CAN ID"))
    crow.addWidget(_field("RX", rx, "ECU response CAN ID"))
    crow.addWidget(_field("Func", func, "Functional / broadcast ID (e.g. 0x7DF)"))
    crow.addStretch(1)
    cl.addLayout(crow)

    crow2 = QHBoxLayout()
    crow2.setSpacing(12)
    func_check = QCheckBox("Functional addressing")
    func_check.setChecked(session.functional)
    func_check.setToolTip("Send requests on Func ID instead of TX")
    crow2.addWidget(func_check)
    crow2.addStretch(1)
    apply_btn = QPushButton("Apply")
    apply_btn.setObjectName("PrimaryButton")
    apply_btn.setFixedHeight(28)
    apply_btn.setMinimumWidth(88)
    apply_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    apply_btn.setToolTip("Write IDs and timing into the shared session")
    codicons.set_button(apply_btn, "apply", primary=True, size=12)
    crow2.addWidget(apply_btn)
    cl.addLayout(crow2)
    layout.addWidget(conn)

    # Session
    sess, sl = _section(
        "Session",
        "Switch diagnostic session before DID / DTC / Flash. "
        "Keep-alive sends TesterPresent 3E 80.")
    seg = QWidget()
    seg.setObjectName("SuiteSegment")
    seg_l = QHBoxLayout(seg)
    seg_l.setContentsMargins(0, 0, 0, 0)
    seg_l.setSpacing(0)
    group = QButtonGroup(seg)
    group.setExclusive(True)
    for i, (code, label) in enumerate(
            ((0x01, "Default"), (0x03, "Extended"), (0x02, "Prog"))):
        b = QPushButton(label)
        b.setObjectName("SegmentBtn")
        b.setCheckable(True)
        b.setFixedHeight(28)
        b.setMinimumWidth(84)
        b.setCursor(Qt.CursorShape.PointingHandCursor)
        b.setToolTip("DiagnosticSessionControl 0x%02X" % code)
        if i == 0:
            b.setProperty("segment", "first")
        elif i == 2:
            b.setProperty("segment", "last")
        else:
            b.setProperty("segment", "mid")
        b.style().unpolish(b)
        b.style().polish(b)
        group.addButton(b)
        b.clicked.connect(lambda _=False, s=code: session.go_session(s))
        seg_l.addWidget(b)
    seg_l.addSpacing(12)
    session_label = QLabel("Session: unknown")
    session_label.setObjectName("SessionBadge")
    session_label.setToolTip("Last successful session reported by the ECU")
    seg_l.addWidget(session_label)
    tp_check = QCheckBox("Keep-alive")
    tp_check.setChecked(session.tester_present)
    tp_check.setToolTip("TesterPresent 3E 80 while you work")
    tp_check.toggled.connect(lambda c: setattr(session, "tester_present", c))
    seg_l.addWidget(tp_check)
    seg_l.addStretch(1)
    sl.addWidget(seg)
    layout.addWidget(sess)

    # Timing
    timing, tl = _section(
        "Timing",
        "P2 / P2* = response timeouts. TP = keep-alive period. Written with Apply.")
    trow = QHBoxLayout()
    trow.setSpacing(14)
    p2 = StepSpin(session.client.p2_ms, minimum=50, maximum=10000, suffix=" ms", width=100)
    p2s = StepSpin(session.client.p2star_ms, minimum=50, maximum=60000, suffix=" ms", width=100)
    tp = StepSpin(getattr(session, "tp_interval_ms", 2000), minimum=200, maximum=10000,
                  suffix=" ms", width=100)
    trow.addWidget(_field("P2", p2, "P2 server timeout"))
    trow.addWidget(_field("P2*", p2s, "P2* after NRC 0x78"))
    trow.addWidget(_field("TP", tp, "TesterPresent interval"))
    trow.addStretch(1)
    tl.addLayout(trow)
    layout.addWidget(timing)
    layout.addStretch(1)

    outer.addWidget(content, 0, Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeft)
    outer.addStretch(1)

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
        for spin, val in ((tx, session.tx_id), (rx, session.rx_id), (func, session.func_id)):
            spin.blockSignals(True)
            spin.setValue(val)
            spin.blockSignals(False)
        func_check.setChecked(session.functional)
        tp_check.setChecked(session.tester_present)

    session.on_ids_changed(_sync)
    return root
