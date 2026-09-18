# -*- coding: utf-8 -*-
"""dbc-exporter — DBC signal matrix to CSV / JSON / HTML.

Offline tool with workspace DBC picker and persisted last format/path.
"""

from __future__ import annotations

import csv
import html as html_mod
import json
import os
import sys
import time

_PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
_PLUGINS_ROOT = os.path.dirname(_PLUGIN_DIR)
if _PLUGINS_ROOT not in sys.path:
    sys.path.insert(0, _PLUGINS_ROOT)

from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "dbc-exporter"

_win = None


def _val_table_str(vt: dict) -> str:
    if not vt:
        return ""
    return "; ".join("%d=%s" % (k, v) for k, v in sorted(vt.items()))


def build_matrix_rows(db, opts: dict) -> tuple[list[str], list[list]]:
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


def activate(context):
    global _win
    import sin

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QCheckBox, QComboBox,
        )
    except ImportError:
        sin.output.append("dbc-exporter requires PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("DBC Exporter")
    win.resize(980, 640)
    plugin_shell.attach_status_bar(win, "Ready — pick a DBC")
    _win = win

    store = {"db": None, "path": "", "last_format": "csv", "last_export": ""}

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_btn = QPushButton("Load DBC…")
    fmt_label = QLabel("Format:")
    fmt_combo = QComboBox()
    fmt_combo.addItems(["CSV", "JSON", "HTML"])
    export_btn = QPushButton("Export…")
    top.addWidget(load_btn)
    top.addWidget(fmt_label)
    top.addWidget(fmt_combo)
    top.addWidget(export_btn)
    top.addStretch(1)
    layout.addLayout(top)

    opts_row = QHBoxLayout()
    opt_comment = QCheckBox("Comments")
    opt_comment.setChecked(True)
    opt_values = QCheckBox("Value tables")
    opt_values.setChecked(True)
    opt_nodes = QCheckBox("Receivers")
    opt_nodes.setChecked(True)
    opt_minmax = QCheckBox("Min/Max")
    opt_minmax.setChecked(True)
    for w in (opt_comment, opt_values, opt_nodes, opt_minmax):
        opts_row.addWidget(w)
    opts_row.addStretch(1)
    layout.addLayout(opts_row)

    layout.addWidget(plugin_shell.help_label(
        "Export a full signal matrix (CSV / JSON / HTML). "
        "Workspace DBC preferred. Last format and path are restored next session."))

    label = QLabel("No DBC loaded")
    label.setStyleSheet("color:#78909c;")
    layout.addWidget(label)

    empty = plugin_shell.empty_state_label(
        "Load a DBC to preview messages.\nChoose format and Export.")
    layout.addWidget(empty)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Message", "ID", "DLC", "Sender", "Cycle", "Signals"])
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tree.hide()
    layout.addWidget(tree, 1)

    def _opts() -> dict:
        return {
            "comment": opt_comment.isChecked(),
            "values": opt_values.isChecked(),
            "nodes": opt_nodes.isChecked(),
            "minmax": opt_minmax.isChecked(),
        }

    def _persist(**extra):
        data = {
            "path": store["path"],
            "last_format": store["last_format"],
            "last_export": store["last_export"],
            "opts": _opts(),
            "format_index": fmt_combo.currentIndex(),
        }
        data.update(extra)
        return state_store.save_state(PLUGIN_ID, data)

    def _refresh_tree():
        tree.clear()
        db = store["db"]
        if not db:
            return
        for cid, m in db.messages.items():
            parent = QTreeWidgetItem([
                m.name, "0x%X" % cid, str(m.dlc), m.sender,
                str(m.cycle_time), str(len(m.signals))])
            tree.addTopLevelItem(parent)
            for s in m.signals:
                detail = "%d|%d@%s%s (%g,%g)" % (
                    s.start_bit, s.bit_length,
                    "i" if s.little_endian else "m",
                    "-" if s.is_signed else "+",
                    s.factor, s.offset)
                if opt_comment.isChecked() and s.comment:
                    detail += " // %s" % s.comment
                parent.addChild(QTreeWidgetItem([s.name, "", "", "", "", detail]))

    def _on_load():
        path = dbc_picker.pick_dbc(win, "Select DBC to export")
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "DBC Exporter", "No messages in file")
            return
        store["db"] = db
        store["path"] = path
        n_sig = sum(len(m.signals) for m in db.messages.values())
        label.setText(
            "%s (%d messages, %d signals)"
            % (os.path.basename(path), len(db.messages), n_sig))
        label.setStyleSheet("color:#2e7d32;")
        empty.hide()
        tree.show()
        _refresh_tree()
        _persist()
        plugin_shell.set_status(win, "Loaded %s" % os.path.basename(path), 3000)

    def _default_name(fmt: str) -> str:
        base = os.path.splitext(os.path.basename(store["path"] or "dbc"))[0]
        last = store.get("last_export") or ""
        if last and os.path.dirname(last):
            return os.path.join(os.path.dirname(last), "%s_matrix.%s" % (base, fmt))
        return "%s_matrix.%s" % (base, fmt)

    def _on_export():
        db = store["db"]
        if not db:
            QMessageBox.information(win, "DBC Exporter", "Load a DBC first")
            return
        fmt = fmt_combo.currentText().lower()
        store["last_format"] = fmt
        filters = {
            "csv": "CSV (*.csv)",
            "json": "JSON (*.json)",
            "html": "HTML (*.html)",
        }
        path, _ = QFileDialog.getSaveFileName(
            win, "Export signal matrix", _default_name(fmt), filters[fmt])
        if not path:
            return
        opts = _opts()
        try:
            if fmt == "csv":
                headers, rows = build_matrix_rows(db, opts)
                with open(path, "w", newline="", encoding="utf-8-sig") as f:
                    w = csv.writer(f)
                    w.writerow(headers)
                    for row in rows:
                        w.writerow(row)
            elif fmt == "json":
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(build_json(db, opts), f, ensure_ascii=False, indent=2)
            else:
                write_html(path, db, opts)
            store["last_export"] = path
            _persist()
            plugin_shell.set_status(win, "Exported %s" % path, 5000)
        except OSError as e:
            QMessageBox.warning(win, "DBC Exporter", str(e))

    load_btn.clicked.connect(_on_load)
    export_btn.clicked.connect(_on_export)
    for chk in (opt_comment, opt_values, opt_nodes, opt_minmax):
        chk.stateChanged.connect(_refresh_tree)
    fmt_combo.currentIndexChanged.connect(
        lambda _i: _persist(last_format=fmt_combo.currentText().lower()))
    plugin_shell.bind_shortcut(win, "Ctrl+O", _on_load)
    plugin_shell.bind_shortcut(win, "Ctrl+S", _on_export)

    context.register_command(
        "dbcExporter.open", plugin_shell.bind_raise(win), "Database: DBC Export")

    saved = state_store.load_state(PLUGIN_ID, default={}) or {}
    if isinstance(saved.get("format_index"), int):
        fmt_combo.setCurrentIndex(max(0, min(2, saved["format_index"])))
    store["last_format"] = (saved.get("last_format") or "csv").lower()
    store["last_export"] = saved.get("last_export") or ""
    opts_saved = saved.get("opts") or {}
    if isinstance(opts_saved, dict):
        opt_comment.setChecked(bool(opts_saved.get("comment", True)))
        opt_values.setChecked(bool(opts_saved.get("values", True)))
        opt_nodes.setChecked(bool(opts_saved.get("nodes", True)))
        opt_minmax.setChecked(bool(opts_saved.get("minmax", True)))
    p = saved.get("path") or ""
    if p and os.path.isfile(p):
        try:
            db = dbcparse.parse_file(p)
        except OSError:
            db = None
        if db and db.messages:
            store["db"] = db
            store["path"] = p
            n_sig = sum(len(m.signals) for m in db.messages.values())
            label.setText(
                "%s (%d messages, %d signals)"
                % (os.path.basename(p), len(db.messages), n_sig))
            label.setStyleSheet("color:#2e7d32;")
            empty.hide()
            tree.show()
            _refresh_tree()

    win.show()
    sin.output.append("dbc-exporter loaded (CSV/JSON/HTML signal matrix)")


def deactivate():
    global _win
    _win = None
    try:
        import sin
        sin.output.append("dbc-exporter deactivated")
    except Exception:
        pass
