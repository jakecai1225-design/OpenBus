# -*- coding: utf-8 -*-
"""Signal matrix export (ported from dbc-exporter)."""

from __future__ import annotations

import html as html_mod
import os
import time


def _val_table_str(vt: dict) -> str:
    if not vt:
        return ""
    return "; ".join("%d=%s" % (k, v) for k, v in sorted(vt.items()))


def build_matrix_rows(db, opts: dict):
    headers = [
        "message", "id", "dlc", "sender", "cycle_ms", "signal",
        "start_bit", "length", "byte_order", "signed", "factor", "offset",
    ]
    if opts.get("minmax"):
        headers += ["min", "max"]
    headers.append("unit")
    if opts.get("nodes"):
        headers.append("receivers")
    if opts.get("values"):
        headers.append("value_table")
    if opts.get("comment"):
        headers.append("comment")

    rows = []
    for cid, m in db.messages.items():
        for s in m.signals:
            row = [
                m.name, "0x%X" % cid, m.dlc, m.sender, m.cycle_time, s.name,
                s.start_bit, s.bit_length,
                "Intel" if s.little_endian else "Motorola",
                "signed" if s.is_signed else "unsigned",
                s.factor, s.offset,
            ]
            if opts.get("minmax"):
                row += [s.minimum, s.maximum]
            row.append(s.unit)
            if opts.get("nodes"):
                row.append(" ".join(s.receivers))
            if opts.get("values"):
                row.append(_val_table_str(s.value_table))
            if opts.get("comment"):
                row.append(s.comment or "")
            rows.append(row)
    return headers, rows


def build_json(db, opts: dict) -> dict:
    data = {"version": db.version, "nodes": list(db.nodes), "messages": []}
    for cid, m in db.messages.items():
        msg = {
            "id": cid,
            "name": m.name,
            "dlc": m.dlc,
            "sender": m.sender,
            "cycle_time": m.cycle_time,
            "signals": [],
        }
        if opts.get("comment"):
            msg["comment"] = m.comment or ""
        for s in m.signals:
            sig = {
                "name": s.name,
                "start_bit": s.start_bit,
                "bit_length": s.bit_length,
                "byte_order": "intel" if s.little_endian else "motorola",
                "signed": s.is_signed,
                "factor": s.factor,
                "offset": s.offset,
                "unit": s.unit,
            }
            if opts.get("minmax"):
                sig["min"] = s.minimum
                sig["max"] = s.maximum
            if opts.get("nodes"):
                sig["receivers"] = list(s.receivers)
            if opts.get("values") and s.value_table:
                sig["values"] = {str(k): v for k, v in s.value_table.items()}
            if opts.get("comment") and s.comment:
                sig["comment"] = s.comment
            msg["signals"].append(sig)
        data["messages"].append(msg)
    return data


def write_html(path: str, db, opts: dict) -> None:
    parts = [
        "<!DOCTYPE html><html><head><meta charset='utf-8'>",
        "<title>DBC Signal Matrix</title>",
        "<style>",
        "body{font-family:Segoe UI,system-ui,sans-serif;margin:24px;color:#263238;}",
        "table{border-collapse:collapse;width:100%;font-size:12px;margin-bottom:20px;}",
        "th,td{border:1px solid #cfd8dc;padding:4px 6px;}",
        "th{background:#eceff1;text-align:left;}",
        "tr:nth-child(even){background:#fafafa;}",
        "h1{font-size:20px;} h3{font-size:14px;margin-top:18px;}",
        ".meta{color:#607d8b;}",
        "</style></head><body>",
        "<h1>Signal Matrix</h1>",
        "<p class='meta'>Generated %s · %d messages · %s</p>"
        % (
            time.strftime("%Y-%m-%d %H:%M:%S"),
            len(db.messages),
            html_mod.escape(os.path.basename(db.path or "")),
        ),
    ]
    for cid, m in db.messages.items():
        parts.append(
            "<h3>%s (0x%X, DLC %d, %s)</h3>"
            % (html_mod.escape(m.name), cid, m.dlc, html_mod.escape(m.sender or ""))
        )
        if opts.get("comment") and m.comment:
            parts.append("<p>%s</p>" % html_mod.escape(m.comment))
        parts.append("<table><thead><tr>")
        cols = ["Signal", "Start", "Len", "Order", "Factor", "Offset", "Unit"]
        if opts.get("minmax"):
            cols += ["Min", "Max"]
        if opts.get("values"):
            cols.append("Values")
        if opts.get("comment"):
            cols.append("Comment")
        for col in cols:
            parts.append("<th>%s</th>" % col)
        parts.append("</tr></thead><tbody>")
        for s in m.signals:
            parts.append("<tr>")
            cells = [
                s.name,
                str(s.start_bit),
                str(s.bit_length),
                "Intel" if s.little_endian else "Motorola",
                str(s.factor),
                str(s.offset),
                s.unit or "",
            ]
            if opts.get("minmax"):
                cells += [str(s.minimum), str(s.maximum)]
            if opts.get("values"):
                cells.append(_val_table_str(s.value_table))
            if opts.get("comment"):
                cells.append(s.comment or "")
            for cell in cells:
                parts.append("<td>%s</td>" % html_mod.escape(str(cell)))
            parts.append("</tr>")
        parts.append("</tbody></table>")
    parts.append("</body></html>")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))
