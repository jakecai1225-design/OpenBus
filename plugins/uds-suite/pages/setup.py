# -*- coding: utf-8 -*-
"""Setup workspace — bus IDs, session, timing.

Opened from activity-bar bottom (gear) or Diagnose Quick bar Bus IDs.
Primary workflow stays on Diagnose; this page is for configuration.
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
from pages import _ui
from widgets.step_spin import StepSpin


def _field(label: str, widget: QWidget) -> QWidget:
    """Inline label + control on one baseline."""
    wrap = QWidget()
    wrap.setObjectName("SuiteField")
    row = QHBoxLayout(wrap)
    row.setContentsMargins(0, 0, 0, 0)
    row.setSpacing(8)
    lab = QLabel(label)
    lab.setObjectName("SuiteFieldLabel")
    lab.setFixedWidth(40)
    lab.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
    row.addWidget(lab)
    row.addWidget(widget, 0, Qt.AlignmentFlag.AlignVCenter)
    return wrap


def _section(title: str, tip: str = "") -> tuple[QWidget, QVBoxLayout]:
    card = QWidget()
    card.setObjectName("SuiteSettingsSection")
    outer = QVBoxLayout(card)
    outer.setContentsMargins(0, 0, 0, 0)
    outer.setSpacing(10)

    head = QHBoxLayout()
    head.setContentsMargins(0, 0, 0, 0)
    head.setSpacing(10)
    t = QLabel(title)
    t.setObjectName("SuiteSectionTitle")
    if tip:
        t.setToolTip(tip)
        card.setToolTip(tip)
    head.addWidget(t)
    head.addStretch(1)
    outer.addLayout(head)

    body = QVBoxLayout()
    body.setContentsMargins(0, 0, 0, 0)
    body.setSpacing(8)
    outer.addLayout(body)
    return card, body


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    root.setObjectName("SuiteSettingsPage")
    outer = QVBoxLayout(root)
    outer.setContentsMargins(20, 16, 20, 16)
    outer.setSpacing(0)

    # Constrain content width — avoids stretched, sparse controls
    content = QWidget()
    content.setObjectName("SuiteSettingsContent")
    content.setMaximumWidth(720)
    layout = QVBoxLayout(content)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    # ---- Connection ----
    conn, cl = _section("Connection", "IDs shared by every page that sends")
    crow = QHBoxLayout()
    crow.setContentsMargins(0, 0, 0, 0)
    crow.setSpacing(16)
    tx = StepSpin(session.tx_id, minimum=1, maximum=0x7FF, hex_mode=True, width=88)
    rx = StepSpin(session.rx_id, minimum=1, maximum=0x7FF, hex_mode=True, width=88)
    func = StepSpin(session.func_id, minimum=1, maximum=0x7FF, hex_mode=True, width=88)
    crow.addWidget(_field("TX", tx))
    crow.addWidget(_field("RX", rx))
    crow.addWidget(_field("Func", func))
    crow.addStretch(1)
    cl.addLayout(crow)

    crow2 = QHBoxLayout()
    crow2.setContentsMargins(0, 0, 0, 0)
    crow2.setSpacing(12)
    func_check = QCheckBox("Functional addressing")
    func_check.setChecked(session.functional)
    func_check.setToolTip("Send on Func ID instead of TX")
    crow2.addWidget(func_check)
    crow2.addStretch(1)
    apply_btn = _ui.primary_btn("Apply", "Apply connection IDs and addressing", "apply")
    apply_btn.setMinimumWidth(88)
    crow2.addWidget(apply_btn)
    cl.addLayout(crow2)
    layout.addWidget(conn)

    # ---- Session (segmented) ----
    sess, sl = _section("Session", "Diagnostic session + TesterPresent keep-alive")
    seg = QWidget()
    seg.setObjectName("SuiteSegment")
    seg_l = QHBoxLayout(seg)
    seg_l.setContentsMargins(0, 0, 0, 0)
    seg_l.setSpacing(0)
    group = QButtonGroup(seg)
    group.setExclusive(True)
    session_btns = {}
    for i, (code, label) in enumerate(((0x01, "Default"), (0x03, "Extended"), (0x02, "Prog"))):
        b = QPushButton(label)
        b.setObjectName("SegmentBtn")
        b.setCheckable(True)
        b.setFixedHeight(_ui.CTRL_H)
        b.setMinimumWidth(84)
        b.setCursor(Qt.CursorShape.PointingHandCursor)
        b.setToolTip("Session 0x%02X" % code)
        if i == 0:
            b.setProperty("segment", "first")
        elif i == 2:
            b.setProperty("segment", "last")
        else:
            b.setProperty("segment", "mid")
        b.style().unpolish(b)
        b.style().polish(b)
        group.addButton(b)
        session_btns[code] = b
        b.clicked.connect(lambda _=False, s=code: session.go_session(s))
        seg_l.addWidget(b)
    seg_l.addStretch(1)
    sl.addWidget(seg)

    foot = QHBoxLayout()
    foot.setContentsMargins(0, 0, 0, 0)
    foot.setSpacing(12)
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
    timing, tl = _section("Timing", "P2 / P2* timeouts, TP keep-alive period")
    trow = QHBoxLayout()
    trow.setContentsMargins(0, 0, 0, 0)
    trow.setSpacing(16)
    p2 = StepSpin(session.client.p2_ms, minimum=50, maximum=10000, suffix=" ms", width=96)
    p2s = StepSpin(session.client.p2star_ms, minimum=50, maximum=60000, suffix=" ms", width=96)
    tp = StepSpin(getattr(session, "tp_interval_ms", 2000), minimum=200, maximum=10000,
                  suffix=" ms", width=96)
    p2.setToolTip("P2 server timeout")
    p2s.setToolTip("P2* after NRC 0x78")
    tp.setToolTip("TesterPresent interval")
    trow.addWidget(_field("P2", p2))
    trow.addWidget(_field("P2*", p2s))
    trow.addWidget(_field("TP", tp))
    trow.addStretch(1)
    tl.addLayout(trow)
    layout.addWidget(timing)

    # ---- Identify ----
    ident, il = _section("Identify", "F186 F187 F18A F18C F190 F191 F195 F197")
    read_btn = _ui.ghost_btn("Read ID", "Read identification DIDs", "refresh")
    read_btn.setMinimumWidth(96)
    read_btn.clicked.connect(session.read_identity)
    il.addWidget(read_btn, 0, Qt.AlignmentFlag.AlignLeft)
    layout.addWidget(ident)

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
        # Highlight matching segment when known
        for code, btn in session_btns.items():
            btn.setChecked(False)

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
    _ui.polish_work_surface(root)
    return root
