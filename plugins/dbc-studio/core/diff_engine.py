# -*- coding: utf-8 -*-
"""DBC compare / diff engine (ported from dbc-diff)."""

from __future__ import annotations

import html
from typing import Any, Optional

_SIG_STRUCT = (
    "start_bit", "bit_length", "little_endian", "is_signed",
    "factor", "offset", "minimum", "maximum", "unit",
)


def _fmt(v: Any) -> str:
    if isinstance(v, float) and v == int(v):
        return str(int(v))
    return str(v)


def _val_table_str(vt: dict) -> str:
    if not vt:
        return ""
    return "; ".join("%s=%s" % (k, v) for k, v in sorted(vt.items()))


def _attr_diff(a: dict, b: dict, prefix: str = "BA_", skip: Optional[set] = None) -> list:
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


def _sig_changes(sa, sb) -> list:
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


def _msg_level_diffs(ma, mb) -> list:
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

    rows: (kind, id, name, detail, children)
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


def flatten_rows(rows) -> list:
    out = []
    for kind, cid, name, detail, children in rows:
        out.append(["message", kind, cid, name, detail])
        for ckind, cname, cdetail in children:
            out.append(["signal", ckind, cid, cname, cdetail])
    return out


def export_html(path: str, path_a: str, path_b: str, rows, stats: dict) -> None:
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
        "<p class='meta'>Messages +%d / -%d / ~%d | Signals +%d / -%d / ~%d</p>" % (
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
