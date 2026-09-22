# -*- coding: utf-8 -*-
"""Analysis — COM coverage stats."""

from __future__ import annotations

from PyQt6.QtWidgets import QFormLayout, QLabel, QTextEdit, QVBoxLayout, QWidget

from _shared import arxmlparse, suite_chrome, vscode_theme
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    run_btn = _ui.primary_btn("Refresh", "Recompute coverage", "refresh")
    summary = _ui.quiet_label("")
    crow.addWidget(run_btn)
    crow.addWidget(summary)
    crow.addStretch(1)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl)
    host = QWidget()
    host.setMaximumWidth(420)
    form = QFormLayout(host)
    vscode_theme.tune_form(form)
    lbl_pdu = QLabel("—")
    lbl_sig = QLabel("—")
    lbl_bits = QLabel("—")
    lbl_zero = QLabel("—")
    for label, w in (
        ("PDUs", lbl_pdu),
        ("Signals", lbl_sig),
        ("Mapped bits (intel est.)", lbl_bits),
        ("PDUs with CAN ID 0", lbl_zero),
    ):
        form.addRow(label, w)
    bl.addWidget(host)
    report = QTextEdit()
    report.setReadOnly(True)
    report.setMaximumWidth(640)
    bl.addWidget(report, 1)
    layout.addWidget(body, 1)

    def _run():
        ipdus = document.model.ipdus
        n_sig = sum(len(p.signals) for p in ipdus)
        bits = 0
        zero = 0
        lines = []
        for p in ipdus:
            if p.can_id == 0:
                zero += 1
            used = 0
            for s in p.signals:
                if arxmlparse.normalize_endian(s.endian) == arxmlparse.ENDIAN_INTEL:
                    used = max(used, s.start_bit + s.length)
                    bits += s.length
            lines.append(
                "%s  id=0x%X  dlc=%d  signals=%d  span≈%d bit" % (
                    p.name, p.can_id, p.dlc, len(p.signals), used))
        lbl_pdu.setText(str(len(ipdus)))
        lbl_sig.setText(str(n_sig))
        lbl_bits.setText(str(bits))
        lbl_zero.setText(str(zero))
        summary.setText("%d PDU · %d signals" % (len(ipdus), n_sig))
        report.setPlainText("\n".join(lines) or "(empty)")

    run_btn.clicked.connect(_run)
    document.on_changed(_run)
    _run()
    return root
