# -*- coding: utf-8 -*-
"""Analysis — OD coverage stats and coarse PDO payload estimate."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QFormLayout,
    QLabel,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from _shared import edsparse, suite_chrome, vscode_theme
from pages import _ui


def _bitlen(entry) -> int:
    s = (entry.parameter_value or entry.default_value or "").strip().lower()
    s = s.replace("0x", "")
    if not s:
        return 0
    try:
        raw = int(s, 16)
    except ValueError:
        return 0
    return raw & 0xFF


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    run_btn = _ui.primary_btn("Refresh", "Recompute coverage and PDO load", "refresh")
    summary = _ui.count_label()
    crow.addWidget(run_btn)
    crow.addWidget(summary)
    crow.addStretch(1)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl, top=12)
    bl.setSpacing(10)

    stats_host = QWidget()
    stats_host.setMaximumWidth(420)
    form = QFormLayout(stats_host)
    vscode_theme.tune_form(form)
    lbl_objects = QLabel("—")
    lbl_indexes = QLabel("—")
    lbl_comm = QLabel("—")
    lbl_mfg = QLabel("—")
    lbl_dev = QLabel("—")
    lbl_pdo = QLabel("—")
    lbl_dcf = QLabel("—")
    for label, w in (
        ("OD entries", lbl_objects),
        ("Unique indexes", lbl_indexes),
        ("Communication", lbl_comm),
        ("Manufacturer", lbl_mfg),
        ("Device profile", lbl_dev),
        ("Mapped PDO bits", lbl_pdo),
        ("DCF", lbl_dcf),
    ):
        form.addRow(label, w)
    bl.addWidget(stats_host)

    report = QTextEdit()
    report.setReadOnly(True)
    report.setObjectName("SuiteReport")
    report.setPlaceholderText("Details")
    report.setMaximumWidth(640)
    bl.addWidget(report, 1)
    layout.addWidget(body, 1)

    def _run():
        eds = document.eds
        entries = eds.entries
        indexes = {e.index for e in entries}
        comm = sum(1 for i in indexes if 0x1000 <= i <= 0x1FFF)
        mfg = sum(1 for i in indexes if 0x2000 <= i <= 0x5FFF)
        dev = sum(1 for i in indexes if 0x6000 <= i <= 0x9FFF)
        pdo_bits = 0
        pdo_lines = []
        for e in entries:
            if not (0x1600 <= e.index <= 0x17FF or 0x1A00 <= e.index <= 0x1BFF):
                continue
            if e.subindex == 0:
                continue
            bits = _bitlen(e)
            pdo_bits += bits
            if bits:
                pdo_lines.append(
                    "  %s → %d bit(s)" % (e.display_index(), bits))
        missing = [
            "0x%04X" % req for req in edsparse.MANDATORY_INDEXES
            if req not in indexes]
        dc = eds.device_commissioning
        dcf_txt = "yes" if eds.is_dcf else "no"
        if dc:
            dcf_txt += " · NodeID=%s Baud=%s" % (
                dc.get("NodeID") or dc.get("NodeId") or "?",
                dc.get("BaudRate") or "?",
            )

        lbl_objects.setText(str(len(entries)))
        lbl_indexes.setText(str(len(indexes)))
        lbl_comm.setText(str(comm))
        lbl_mfg.setText(str(mfg))
        lbl_dev.setText(str(dev))
        lbl_pdo.setText("%d bit ≈ %.1f B" % (
            pdo_bits, pdo_bits / 8.0 if pdo_bits else 0.0))
        lbl_dcf.setText(dcf_txt)
        summary.setText("%d objects · %d indexes" % (
            len(entries), len(indexes)))

        lines = [
            "Product: %s" % (eds.device_info.get("ProductName") or "(unnamed)"),
            "Vendor: %s" % (eds.device_info.get("VendorName") or "(unnamed)"),
            "",
            "Mandatory missing: %s" % (
                ", ".join(missing) if missing else "none"),
            "",
            "PDO mapping payload:",
        ]
        lines.extend(pdo_lines or ["  (none)"])
        report.setPlainText("\n".join(lines))

    run_btn.clicked.connect(_run)
    document.on_changed(_run)
    _run()
    return root
