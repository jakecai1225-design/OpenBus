# -*- coding: utf-8 -*-
"""dbc-diff — compare two DBC files (messages, signals, BA_, VAL_, comments).

Offline review tool with CSV and HTML side-by-side export.
"""

from __future__ import annotations

import html
import os
import sys
from typing import Any, Optional

_PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
_PLUGINS_ROOT = os.path.dirname(_PLUGIN_DIR)
if _PLUGINS_ROOT not in sys.path:
    sys.path.insert(0, _PLUGINS_ROOT)

from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "dbc-diff"

_SIG_STRUCT = (
    "start_bit", "bit_length", "little_endian", "is_signed",
    "factor", "offset", "minimum", "maximum", "unit",
)

_win = None


def _fmt(v: Any) -> str:
    if isinstance(v, float) and v == int(v):
        return str(int(v))
    return str(v)


def _val_table_str(vt: dict) -> str:
    if not vt:
        return ""
    return "; ".join("%s=%s" % (k, v) for k, v in sorted(vt.items()))


def _attr_diff(a: dict, b: dict, prefix: str = "BA_", skip: Optional[set] = None) -> list[str]:
    diffs = []
    skip = skip or set()
    keys = (set(a or {}) | set(b or {})) - skip
    for k in sorted(keys):
        va, vb = (a or {}).get(k), (b or {}).get(k)
        if va != vb:
            diffs.append("%s%s: %s -> %s" % (
                prefix, k,
                _fmt(va) if va is not None else "(none)",
                _fmt(vb) if vb is not None else "(none)"))
    return diffs


def _sig_changes(sa, sb) -> list[str]:
    diffs = []
    for attr in _SIG_STRUCT:
        va, vb = getattr(sa, attr), getattr(sb, attr)
        if va != vb:
            diffs.append("%s: %s -> %s" % (attr, _fmt(va), _fmt(vb)))
    if (sa.comment or "") != (sb.comment or ""):
        diffs.append("comment: %r -> %r" % (sa.comment or "", sb.comment or ""))
    if dict(sa.value_table or {}) != dict(sb.value_table or {}):
        diffs.append(
            "VAL_: %s -> %s" % (_val_table_str(sa.value_table), _val_table_str(sb.value_table)))
    diffs.extend(_attr_diff(getattr(sa, "attributes", {}), getattr(sb, "attributes", {})))
    return diffs


def _msg_level_diffs(ma, mb) -> list[str]:
    diffs = []
    if ma.name != mb.name:
        diffs.append("name: %s -> %s" % (ma.name, mb.name))
    if ma.dlc != mb.dlc:
        diffs.append("DLC: %d -> %d" % (ma.dlc, mb.dlc))
    if ma.sender != mb.sender:
        diffs.append("sender: %s -> %s" % (ma.sender, mb.sender))
    if (ma.comment or "") != (mb.comment or ""):
        diffs.append("comment: %r -> %r" % (ma.comment or "", mb.comment or ""))
    attrs_a = dict(getattr(ma, "attributes", {}) or {})
    attrs_b = dict(getattr(mb, "attributes", {}) or {})
    if "GenMsgCycleTime" not in attrs_a:
        attrs_a["GenMsgCycleTime"] = str(ma.cycle_time)
    if "GenMsgCycleTime" not in attrs_b:
        attrs_b["GenMsgCycleTime"] = str(mb.cycle_time)
    diffs.extend(_attr_diff(attrs_a, attrs_b))
    return diffs


def diff_dbc(db_a, db_b):
    """Return (rows, stats).

    rows: (kind, id, name, detail, children:[(kind, name, detail)])
    kind in added|removed|changed
    """
    rows = []
    stats = {
        "msg_new": 0, "msg_del": 0, "msg_mod": 0,
        "sig_new": 0, "sig_del": 0, "sig_mod": 0,
    }
    ids_a = set(db_a.messages.keys())
    ids_b = set(db_b.messages.keys())
    for cid in sorted(ids_a | ids_b):
        ma = db_a.messages.get(cid)
        mb = db_b.messages.get(cid)
        if ma and not mb:
            rows.append((
                "removed", "0x%X" % cid, ma.name,
                "only in A (%d signals)" % len(ma.signals), []))
            stats["msg_del"] += 1
            stats["sig_del"] += len(ma.signals)
        elif mb and not ma:
            rows.append((
                "added", "0x%X" % cid, mb.name,
                "only in B (%d signals)" % len(mb.signals), []))
            stats["msg_new"] += 1
            stats["sig_new"] += len(mb.signals)
        else:
            children = []
            msg_diffs = _msg_level_diffs(ma, mb)
            sig_a = {s.name: s for s in ma.signals}
            sig_b = {s.name: s for s in mb.signals}
            for name in sorted(set(sig_a) | set(sig_b)):
                sa, sb = sig_a.get(name), sig_b.get(name)
                if sa and not sb:
                    children.append(("removed", name, "only in A"))
                    stats["sig_del"] += 1
                elif sb and not sa:
                    children.append(("added", name, "only in B"))
                    stats["sig_new"] += 1
                else:
                    d = _sig_changes(sa, sb)
                    if d:
                        children.append(("changed", name, "; ".join(d)))
                        stats["sig_mod"] += 1
            if msg_diffs or children:
                rows.append((
                    "changed", "0x%X" % cid, ma.name,
                    "; ".join(msg_diffs) or "signal changes", children))
                stats["msg_mod"] += 1
    return rows, stats


def flatten_rows(rows) -> list[list]:
    out = []
    for kind, cid, name, detail, children in rows:
        out.append(["message", kind, cid, name, detail])
        for ckind, cname, cdetail in children:
            out.append(["signal", ckind, cid, cname, cdetail])
    return out


def export_html(
    path: str,
    path_a: str,
    path_b: str,
    rows,
    stats: dict,
) -> None:
    """Write a side-by-side HTML review report."""
    color = {
        "added": "#e8f5e9",
        "removed": "#ffebee",
        "changed": "#fff8e1",
    }
    badge = {
        "added": "#2e7d32",
        "removed": "#c62828",
        "changed": "#ef6c00",
    }
    parts = [
        "<!DOCTYPE html><html><head><meta charset='utf-8'>",
        "<title>DBC Diff</title>",
        "<style>",
        "body{font-family:Segoe UI,system-ui,sans-serif;margin:24px;color:#263238;}",
        "h1{font-size:20px;} .meta{color:#607d8b;margin-bottom:16px;}",
        "table{border-collapse:collapse;width:100%;font-size:13px;}",
        "th,td{border:1px solid #cfd8dc;padding:6px 8px;vertical-align:top;}",
        "th{background:#eceff1;text-align:left;}",
        ".kind{font-weight:600;}",
        ".child td{background:#fafafa;}",
        ".cols{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:20px;}",
        ".box{background:#f5f5f5;padding:12px;border-radius:4px;}",
        "</style></head><body>",
        "<h1>DBC Diff Report</h1>",
        "<div class='cols'>",
        "<div class='box'><b>A</b><br>%s</div>" % html.escape(path_a),
        "<div class='box'><b>B</b><br>%s</div>" % html.escape(path_b),
        "</div>",
        "<p class='meta'>Messages +%d / −%d / ~%d | Signals +%d / −%d / ~%d</p>" % (
            stats.get("msg_new", 0), stats.get("msg_del", 0), stats.get("msg_mod", 0),
            stats.get("sig_new", 0), stats.get("sig_del", 0), stats.get("sig_mod", 0)),
        "<table><thead><tr>",
        "<th>Status</th><th>ID</th><th>Name</th><th>Detail (A -> B)</th>",
        "</tr></thead><tbody>",
    ]
    if not rows:
        parts.append("<tr><td colspan='4'>No differences</td></tr>")
    for kind, cid, name, detail, children in rows:
        bg = color.get(kind, "#fff")
        parts.append(
            "<tr style='background:%s'><td class='kind' style='color:%s'>%s</td>"
            "<td>%s</td><td>%s</td><td>%s</td></tr>"
            % (bg, badge.get(kind, "#000"), html.escape(kind),
               html.escape(cid), html.escape(name), html.escape(detail)))
        for ckind, cname, cdetail in children:
            parts.append(
                "<tr class='child'><td class='kind' style='color:%s'>%s</td>"
                "<td></td><td>%s</td><td>%s</td></tr>"
                % (badge.get(ckind, "#000"), html.escape(ckind),
                   html.escape(cname), html.escape(cdetail)))
    parts.append("</tbody></table></body></html>")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))


def activate(context):
    global _win
    import sin

    try:
        from PyQt6.QtGui import QColor
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox, QHeaderView,
        )
    except ImportError:
        sin.output.append("dbc-diff requires PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("DBC Diff")
    win.resize(1000, 640)
    plugin_shell.attach_status_bar(win, "Ready — pick DBC A and B")
    _win = win

    store = {
        "a": None, "b": None,
        "path_a": "", "path_b": "",
        "rows": [], "stats": {},
    }

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_a_btn = QPushButton("Load A…")
    load_b_btn = QPushButton("Load B…")
    diff_btn = QPushButton("Compare")
    csv_btn = QPushButton("Export CSV")
    html_btn = QPushButton("Export HTML")
    for b in (load_a_btn, load_b_btn, diff_btn):
        top.addWidget(b)
    top.addStretch(1)
    top.addWidget(csv_btn)
    top.addWidget(html_btn)
    layout.addLayout(top)
    layout.addWidget(plugin_shell.help_label(
        "Diffs messages/signals plus BA_ attributes, VAL_ tables, and comments. "
        "Prefer workspace DBC pickers for A/B. HTML export is side-by-side for review."))

    label_a = QLabel("A: (not loaded)")
    label_b = QLabel("B: (not loaded)")
    label_a.setStyleSheet("color:#78909c;")
    label_b.setStyleSheet("color:#78909c;")
    layout.addWidget(label_a)
    layout.addWidget(label_b)

    empty = plugin_shell.empty_state_label(
        "Load two DBC files, then click Compare.\n"
        "Findings cover structural fields, BA_, VAL_, and comments.")
    layout.addWidget(empty)

    summary = QLabel("")
    summary.setStyleSheet("font-weight:bold;")
    summary.hide()
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Status", "ID", "Name", "Detail"])
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tree.hide()
    layout.addWidget(tree, 1)

    def _load(side: str):
        title = "Select DBC %s" % side
        path = dbc_picker.pick_dbc(win, title)
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "DBC Diff", "No messages: %s" % path)
            return
        name = os.path.basename(path)
        if side == "A":
            store["a"] = db
            store["path_a"] = path
            label_a.setText("A: %s (%d messages)" % (name, len(db.messages)))
            label_a.setStyleSheet("color:#2e7d32;")
        else:
            store["b"] = db
            store["path_b"] = path
            label_b.setText("B: %s (%d messages)" % (name, len(db.messages)))
            label_b.setStyleSheet("color:#2e7d32;")
        plugin_shell.set_status(win, "Loaded %s as %s" % (name, side), 3000)
        state_store.save_state(PLUGIN_ID, {
            "path_a": store["path_a"],
            "path_b": store["path_b"],
        })

    def _on_diff():
        if not store["a"] or not store["b"]:
            QMessageBox.information(win, "DBC Diff", "Load DBC A and B first")
            return
        rows, stats = diff_dbc(store["a"], store["b"])
        store["rows"] = rows
        store["stats"] = stats
        tree.clear()
        colors = {
            "added": QColor("#2e7d32"),
            "removed": QColor("#c62828"),
            "changed": QColor("#ef6c00"),
        }
        for kind, cid, name, detail, children in rows:
            item = QTreeWidgetItem([kind, cid, name, detail])
            item.setForeground(0, colors.get(kind, QColor("#000")))
            tree.addTopLevelItem(item)
            for ckind, cname, cdetail in children:
                child = QTreeWidgetItem([ckind, "", cname, cdetail])
                child.setForeground(0, colors.get(ckind, QColor("#000")))
                item.addChild(child)
        empty.hide()
        tree.show()
        summary.show()
        if not rows:
            summary.setText("Identical (messages / signals / BA_ / VAL_ / comments)")
        else:
            s = stats
            summary.setText(
                "Diff: msgs +%d/−%d/~%d | sigs +%d/−%d/~%d"
                % (s["msg_new"], s["msg_del"], s["msg_mod"],
                   s["sig_new"], s["sig_del"], s["sig_mod"]))
        plugin_shell.set_status(win, "%d message-level change(s)" % len(rows), 4000)

    def _on_csv():
        if not store["rows"]:
            QMessageBox.information(win, "DBC Diff", "Compare first")
            return
        path = plugin_shell.export_csv(
            win,
            ["level", "status", "id", "name", "detail"],
            flatten_rows(store["rows"]),
            "dbc_diff.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    def _on_html():
        if not store["rows"] and not (store["a"] and store["b"]):
            QMessageBox.information(win, "DBC Diff", "Compare first")
            return
        if store["a"] and store["b"] and not store["rows"] and not store["stats"]:
            # Allow HTML even when identical after compare populated empty rows.
            pass
        path, _ = QFileDialog.getSaveFileName(
            win, "Export HTML report", "dbc_diff.html", "HTML (*.html)")
        if not path:
            return
        try:
            export_html(
                path, store["path_a"], store["path_b"],
                store["rows"], store["stats"] or {
                    "msg_new": 0, "msg_del": 0, "msg_mod": 0,
                    "sig_new": 0, "sig_del": 0, "sig_mod": 0,
                })
            plugin_shell.set_status(win, "Exported %s" % path, 4000)
        except OSError as e:
            QMessageBox.warning(win, "DBC Diff", str(e))

    load_a_btn.clicked.connect(lambda: _load("A"))
    load_b_btn.clicked.connect(lambda: _load("B"))
    diff_btn.clicked.connect(_on_diff)
    csv_btn.clicked.connect(_on_csv)
    html_btn.clicked.connect(_on_html)
    plugin_shell.bind_shortcut(win, "Ctrl+Return", _on_diff)

    context.register_command(
        "dbcDiff.open", plugin_shell.bind_raise(win), "Database: DBC Diff")

    # Restore last paths if still readable.
    saved = state_store.load_state(PLUGIN_ID, default={}) or {}
    for side, key in (("A", "path_a"), ("B", "path_b")):
        p = saved.get(key) or ""
        if p and os.path.isfile(p):
            try:
                db = dbcparse.parse_file(p)
            except OSError:
                continue
            if not db.messages:
                continue
            if side == "A":
                store["a"] = db
                store["path_a"] = p
                label_a.setText("A: %s (%d messages)" % (os.path.basename(p), len(db.messages)))
                label_a.setStyleSheet("color:#2e7d32;")
            else:
                store["b"] = db
                store["path_b"] = p
                label_b.setText("B: %s (%d messages)" % (os.path.basename(p), len(db.messages)))
                label_b.setStyleSheet("color:#2e7d32;")

    win.show()
    sin.output.append("dbc-diff loaded (messages/signals/BA_/VAL_/comments + HTML)")


def deactivate():
    global _win
    _win = None
    try:
        import sin
        sin.output.append("dbc-diff deactivated")
    except Exception:
        pass
