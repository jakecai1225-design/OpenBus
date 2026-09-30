# -*- coding: utf-8 -*-
"""Export — EDS / DCF / HTML / CSV / XDD lite."""

from __future__ import annotations

import csv
import html
import os

from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QVBoxLayout,
    QWidget,
)

from _shared import edsparse, suite_chrome
from pages import _ui


def _open_path(path: str) -> None:
    if not path or not os.path.isfile(path):
        return
    try:
        os.startfile(path)
    except Exception:
        try:
            import subprocess
            subprocess.Popen(["xdg-open", path])  # noqa: S603
        except Exception:
            pass


def _export_html(doc: edsparse.EdsDocument) -> str:
    rows = []
    for e in doc.entries:
        rows.append(
            "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
            "<td>%s</td><td>%s</td></tr>" % (
                html.escape(e.display_index()),
                html.escape(e.name),
                html.escape(edsparse.object_type_label(e.object_type)),
                html.escape(edsparse.data_type_label(e.data_type)),
                html.escape(e.access_type),
                html.escape(e.effective_value()),
            ))
    title = html.escape(
        doc.device_info.get("ProductName") or doc.path or "EDS")
    return (
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>%s</title>"
        "<style>body{font-family:Segoe UI,sans-serif;margin:24px;}"
        "table{border-collapse:collapse;width:100%%;}"
        "th,td{border:1px solid #ddd;padding:6px 8px;font-size:13px;}"
        "th{background:#f5f5f5;text-align:left;}</style></head><body>"
        "<h1>%s</h1>"
        "<p>Vendor: %s · Objects: %d</p>"
        "<table><thead><tr>"
        "<th>Index</th><th>Name</th><th>Object</th><th>Data</th>"
        "<th>Access</th><th>Value</th></tr></thead><tbody>%s</tbody>"
        "</table></body></html>"
    ) % (
        title, title,
        html.escape(doc.device_info.get("VendorName", "")),
        len(doc.entries),
        "\n".join(rows),
    )


def _export_xdd_lite(doc: edsparse.EdsDocument) -> str:
    """Minimal CiA 311-inspired XDD subset (not full schema)."""
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<ISO15745ProfileContainer xmlns="http://www.canopen.org/xml/1.1">',
        "  <ISO15745Profile>",
        "    <ProfileHeader>",
        "      <ProfileIdentification>CiA 301</ProfileIdentification>",
        "      <ProfileName>%s</ProfileName>" % html.escape(
            doc.device_info.get("ProductName") or "Device"),
        "      <ProfileSource>%s</ProfileSource>" % html.escape(
            doc.device_info.get("VendorName") or "sin"),
        "    </ProfileHeader>",
        "    <ProfileBody>",
        "      <DeviceIdentity>",
        "        <vendorName>%s</vendorName>" % html.escape(
            doc.device_info.get("VendorName") or ""),
        "        <productName>%s</productName>" % html.escape(
            doc.device_info.get("ProductName") or ""),
        "      </DeviceIdentity>",
        "      <ObjectDictionary>",
    ]
    for e in doc.entries:
        lines.append(
            '        <Object index="0x%04X" subIndex="%d" name="%s" '
            'objectType="%s" dataType="%s" accessType="%s" '
            'defaultValue="%s"/>' % (
                e.index, e.subindex,
                html.escape(e.name),
                html.escape(e.object_type or ""),
                html.escape(e.data_type or ""),
                html.escape(e.access_type or ""),
                html.escape(e.default_value or ""),
            ))
    lines.extend([
        "      </ObjectDictionary>",
        "    </ProfileBody>",
        "  </ISO15745Profile>",
        "</ISO15745ProfileContainer>",
        "",
    ])
    return "\n".join(lines)


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    fmt = QComboBox()
    fmt.addItems([
        "EDS", "DCF", "HTML", "CSV", "XDD lite",
        "CANopenNode V4 (OD.h/c)", "CanFestival (C/H)",
    ])
    fmt.setFixedHeight(_ui.CTRL_H)
    fmt.setMinimumWidth(200)
    fmt.setToolTip("Export format")
    open_after = QCheckBox("Open after")
    open_after.setChecked(True)
    open_after.setToolTip("Open the file with the system default app")
    export_btn = _ui.primary_btn("Export…", "Write the chosen format to disk", "export")
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
        "EDS / DCF keep full OD. HTML and CSV are for review. "
        "XDD lite is a CiA 311 subset. "
        "CANopenNode V4 and CanFestival write OD C/H for firmware stacks.")
    tip.setMaximumWidth(520)
    tip.setWordWrap(True)
    bl.addWidget(tip)
    bl.addStretch(1)
    layout.addWidget(body, 1)

    def _do_export():
        kind = fmt.currentText()
        # Stack codegen → directory with multiple files
        if kind.startswith("CANopenNode") or kind.startswith("CanFestival"):
            from _shared.canopen_codegen import generate
            directory = QFileDialog.getExistingDirectory(
                shell, "Export C/H directory", "")
            if not directory:
                return
            target = (
                "canopennode_v4" if kind.startswith("CANopenNode")
                else "canfestival")
            node = (document.eds.device_info.get("ProductName")
                    or document.display_name().rsplit(".", 1)[0]
                    or "Node")
            node = "".join(c if c.isalnum() or c == "_" else "_" for c in node)
            if node and node[0].isdigit():
                node = "N" + node
            try:
                files = generate(
                    document.eds, target, node_name=node or "Node",
                    strict=True)
                written = []
                for name, text in files.items():
                    path = os.path.join(directory, name)
                    with open(path, "w", encoding="utf-8", newline="\n") as f:
                        f.write(text)
                    written.append(path)
                log_fn("OK", "Exported %s" % ", ".join(written))
                if open_after.isChecked() and written:
                    _open_path(written[0])
            except ValueError as e:
                log_fn("ERR", "Codegen blocked: %s" % e)
            except OSError as e:
                log_fn("ERR", "Export failed: %s" % e)
            return

        filters = {
            "EDS": ("EDS (*.eds)", ".eds"),
            "DCF": ("DCF (*.dcf)", ".dcf"),
            "HTML": ("HTML (*.html)", ".html"),
            "CSV": ("CSV (*.csv)", ".csv"),
            "XDD lite": ("XDD (*.xdd *.xml)", ".xdd"),
        }
        filt, ext = filters.get(kind, ("All (*)", ".txt"))
        default = (document.display_name().rsplit(".", 1)[0] + ext)
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export", default, filt)
        if not path:
            return
        if not path.lower().endswith(ext):
            path += ext
        try:
            if kind == "EDS":
                text = edsparse.export_document(document.eds, as_dcf=False)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
            elif kind == "DCF":
                text = edsparse.export_document(document.eds, as_dcf=True)
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
            elif kind == "HTML":
                with open(path, "w", encoding="utf-8") as f:
                    f.write(_export_html(document.eds))
            elif kind == "CSV":
                with open(path, "w", encoding="utf-8", newline="") as f:
                    w = csv.writer(f)
                    w.writerow([
                        "Index", "Sub", "Name", "ObjectType", "DataType",
                        "Access", "Default", "ParameterValue", "PDOMapping",
                    ])
                    for e in document.eds.entries:
                        w.writerow([
                            "0x%04X" % e.index, e.subindex, e.name,
                            e.object_type, e.data_type, e.access_type,
                            e.default_value, e.parameter_value, e.pdo_mapping,
                        ])
            else:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(_export_xdd_lite(document.eds))
            log_fn("OK", "Exported %s" % path)
            if open_after.isChecked():
                _open_path(path)
        except OSError as e:
            log_fn("ERR", "Export failed: %s" % e)

    export_btn.clicked.connect(_do_export)
    return root
