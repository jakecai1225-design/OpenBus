# -*- coding: utf-8 -*-
"""Export — ARXML / DBC / HTML / CSV / project intermediates (no codegen)."""

from __future__ import annotations

import csv
import html
import os

from PyQt6.QtWidgets import QCheckBox, QComboBox, QFileDialog, QVBoxLayout, QWidget

from _shared import arxmlparse, suite_chrome
from pages import _ui


def _open_path(path: str) -> None:
    if not path or not os.path.isfile(path):
        return
    try:
        os.startfile(path)
    except OSError as exc:
        import sys
        print("open failed: %s (%s)" % (path, exc), file=sys.stderr)


def _html(model: arxmlparse.ArxmlModel) -> str:
    rows = []
    for pdu in model.ipdus:
        for sig in pdu.signals:
            rows.append(
                "<tr><td>%s</td><td>0x%X</td><td>%s</td><td>%d</td>"
                "<td>%d</td><td>%s</td></tr>" % (
                    html.escape(pdu.name), pdu.can_id,
                    html.escape(sig.name), sig.start_bit, sig.length,
                    html.escape(sig.endian)))
    return (
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>ARXML report</title>"
        "<style>body{font-family:Segoe UI,sans-serif;margin:24px;}"
        "table{border-collapse:collapse;width:100%%;}"
        "th,td{border:1px solid #ddd;padding:6px;font-size:13px;}"
        "th{background:#f5f5f5;text-align:left;}</style></head><body>"
        "<h1>AUTOSAR Studio report</h1>"
        "<p>Package %s · %d PDUs — configuration only, no codegen.</p>"
        "<table><thead><tr><th>PDU</th><th>CAN ID</th><th>Signal</th>"
        "<th>Start</th><th>Len</th><th>Endian</th></tr></thead>"
        "<tbody>%s</tbody></table></body></html>"
    ) % (html.escape(model.package), len(model.ipdus), "\n".join(rows))


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    fmt = QComboBox()
    fmt.addItems([
        "ARXML", "DBC", "HTML", "CSV", "ECUC-lite",
        "BSW module ARXML", "Project intermediates",
        "Handoff report (HTML)"])
    fmt.setFixedHeight(_ui.CTRL_H)
    fmt.setToolTip("Export format — never generates BSW/RTE source")
    open_after = QCheckBox("Open after")
    open_after.setChecked(True)
    export_btn = _ui.ghost_btn("Export…", "Write file", "export")
    crow.addWidget(fmt)
    crow.addWidget(open_after)
    crow.addStretch(1)
    crow.addWidget(export_btn)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl, top=20)
    tip = _ui.quiet_label(
        "ARXML / ECUC-lite / BSW module ARXML are the handoff to DaVinci / "
        "tresos / ISOLAR. Handoff report includes an Open-in-DaVinci checklist. "
        "No stack codegen.")
    tip.setWordWrap(True)
    tip.setMaximumWidth(560)
    bl.addWidget(tip)
    bl.addStretch(1)
    layout.addWidget(body, 1)

    def _do():
        kind = fmt.currentText()
        if kind == "Handoff report (HTML)":
            default = "handoff_davinci.html"
            if document.has_project() and document.manifest:
                default = os.path.join(
                    document.manifest.root, "out", "handoff_davinci.html")
            path, _ = QFileDialog.getSaveFileName(
                shell, "Handoff report", default, "HTML (*.html)")
            if not path:
                return
            if not path.lower().endswith(".html"):
                path += ".html"
            try:
                os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(document.handoff_html_report())
                log_fn("OK", "Handoff report %s" % path)
                if open_after.isChecked():
                    _open_path(path)
            except OSError as e:
                log_fn("ERR", str(e))
            return

        if kind == "Project intermediates":
            if not document.has_project():
                log_fn("WARN", "Open or create a project first")
                return
            try:
                paths = document.write_intermediates()
                document.write_out_reports()
                n = len(paths.get("bsw") or {})
                log_fn("OK", "Wrote %s + %d BSW ARXMLs + out/" % (
                    paths.get("com", ""), n))
                if hasattr(shell, "_notify_host"):
                    shell._notify_host(paths.get("com", ""))
            except (OSError, ValueError) as e:
                log_fn("ERR", str(e))
            return

        if kind == "BSW module ARXML":
            name = document.active_bsw
            if not name or name not in document.bsw:
                from _shared import arxml_bsw
                names = arxml_bsw.module_names()
                from PyQt6.QtWidgets import QInputDialog
                name, ok = QInputDialog.getItem(
                    shell, "Export BSW module",
                    "Module:", names, 0, False)
                if not ok or not name:
                    return
                if name not in document.bsw:
                    ecu = (
                        document.manifest.ecu_name
                        if document.manifest else "Ecu")
                    document.bsw[name] = arxml_bsw.stub_module(
                        name, ecu_name=ecu)
            default = "%s.arxml" % name
            if document.has_project() and document.manifest:
                default = document.manifest.abs_module(name) or default
            path, _ = QFileDialog.getSaveFileName(
                shell, "Export %s" % name, default, "ARXML (*.arxml)")
            if not path:
                return
            if not path.lower().endswith(".arxml"):
                path += ".arxml"
            try:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(arxmlparse.serialize_ecuc_lite(
                        document.bsw[name]))
                log_fn("OK", "Exported %s" % path)
                if open_after.isChecked():
                    _open_path(path)
            except OSError as e:
                log_fn("ERR", str(e))
            return

        filters = {
            "ARXML": ("ARXML (*.arxml)", ".arxml"),
            "DBC": ("DBC (*.dbc)", ".dbc"),
            "HTML": ("HTML (*.html)", ".html"),
            "CSV": ("CSV (*.csv)", ".csv"),
            "ECUC-lite": ("ARXML (*.arxml)", ".arxml"),
        }
        filt, ext = filters[kind]
        default = document.display_name().rsplit(".", 1)[0] + ext
        if kind == "ECUC-lite":
            default = "ecuc_com.arxml"
        path, _ = QFileDialog.getSaveFileName(shell, "Export", default, filt)
        if not path:
            return
        if not path.lower().endswith(ext):
            path += ext
        try:
            if kind == "ARXML":
                text = arxmlparse.serialize_model(document.model)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
            elif kind == "ECUC-lite":
                if not document.ecuc.modules:
                    document.derive_ecuc()
                with open(path, "w", encoding="utf-8") as f:
                    f.write(arxmlparse.serialize_ecuc_lite(document.ecuc))
            elif kind == "DBC":
                with open(path, "w", encoding="utf-8") as f:
                    f.write(arxmlparse.export_dbc(document.model.ipdus))
            elif kind == "HTML":
                with open(path, "w", encoding="utf-8") as f:
                    f.write(_html(document.model))
            else:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    w = csv.writer(f)
                    w.writerow([
                        "PDU", "CAN_ID", "DLC", "Signal", "Start", "Length",
                        "Endian", "Factor", "Offset", "Unit"])
                    for pdu in document.model.ipdus:
                        for sig in pdu.signals:
                            w.writerow([
                                pdu.name, "0x%X" % pdu.can_id, pdu.dlc,
                                sig.name, sig.start_bit, sig.length,
                                sig.endian, sig.factor, sig.offset, sig.unit])
            log_fn("OK", "Exported %s" % path)
            if open_after.isChecked():
                _open_path(path)
        except OSError as e:
            log_fn("ERR", str(e))

    export_btn.clicked.connect(_do)
    return root
