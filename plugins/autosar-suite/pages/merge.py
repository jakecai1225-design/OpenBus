# -*- coding: utf-8 -*-
"""Merge — take PDUs/signals that exist only in B."""

from __future__ import annotations

import os

from PyQt6.QtWidgets import QFileDialog, QLabel, QVBoxLayout, QWidget

from _shared import arxmlparse, suite_chrome
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    open_b = _ui.ghost_btn("Open B…", "Source ARXML to merge from", "browse")
    merge_btn = _ui.ghost_btn(
        "Merge added from B", "Copy PDUs/signals only in B into A", "add")
    crow.addWidget(open_b)
    crow.addStretch(1)
    crow.addWidget(merge_btn)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl)
    status = _ui.quiet_label("B: (none) — merge never overwrites existing names.")
    bl.addWidget(status)
    tip = QLabel(
        "Policy: add-only. Existing PDU / signal SHORT-NAMEs in A are kept. "
        "Use Compare first to review.")
    tip.setWordWrap(True)
    tip.setStyleSheet("color:#78909C;font-size:12px;")
    tip.setMaximumWidth(520)
    bl.addWidget(tip)
    bl.addStretch(1)
    layout.addWidget(body, 1)

    peer = {"model": None, "path": ""}

    def _open():
        path, _ = QFileDialog.getOpenFileName(
            shell, "Open ARXML B", "", "ARXML (*.arxml *.xml);;All (*)")
        if not path:
            return
        try:
            peer["model"] = arxmlparse.parse_arxml_model(path)
            peer["path"] = path
            status.setText("B: %s (%d PDUs)" % (
                os.path.basename(path), len(peer["model"].ipdus)))
        except OSError as e:
            log_fn("ERR", str(e))

    def _merge():
        if peer["model"] is None:
            log_fn("WARN", "Open B first")
            return
        model = document.clone_model()
        by_name = {p.name: p for p in model.ipdus}
        added_pdu = added_sig = 0
        for bp in peer["model"].ipdus:
            if bp.name not in by_name:
                model.ipdus.append(bp)
                by_name[bp.name] = bp
                added_pdu += 1
            else:
                ap = by_name[bp.name]
                have = {s.name for s in ap.signals}
                for s in bp.signals:
                    if s.name not in have:
                        ap.signals.append(s)
                        added_sig += 1
        document.apply_model(model)
        log_fn("OK", "Merged +%d PDU · +%d signal" % (added_pdu, added_sig))
        shell.goto_page("editor")

    open_b.clicked.connect(_open)
    merge_btn.clicked.connect(_merge)
    return root
