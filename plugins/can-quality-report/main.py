# -*- coding: utf-8 -*-
"""can-quality-report — Bus health report (load / period / coverage).

1s load bins, Top Talker, period mean/jitter, optional DBC coverage,
HTML + CSV export. Subscribe-only.
"""

from __future__ import annotations

import html as html_mod
import os
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
    QHeaderView, QTabWidget, QTextEdit, QDoubleSpinBox,
)

import sin
from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "can-quality-report"

_window = 1.0
_bins = []
_ids = {}
_dbc = None
_dbc_path = ""
_first_ts = None
_last_ts = None
_total_frames = 0
_running = True
_ref_kbps = 500.0
_win = None


def _on_frame(frame):
    global _first_ts, _last_ts, _total_frames
    if not _running:
        return
    now = time.time()
    if _first_ts is None:
        _first_ts = now
    _last_ts = now
    _total_frames += 1
    bin_start = now - (now % _window)
    if not _bins or _bins[-1][0] != bin_start:
        _bins.append([bin_start, 0, 0])
        if len(_bins) > 3600:
            del _bins[:1800]
    _bins[-1][1] += 1
    _bins[-1][2] += len(frame.data) + 8
    st = _ids.get(frame.id)
    if st is None:
        _ids[frame.id] = {
            "count": 1, "bytes": len(frame.data),
            "periods": [], "last": now, "ext": frame.extended,
        }
    else:
        dt = (now - st["last"]) * 1000.0
        if 0 < dt < 10000:
            st["periods"].append(dt)
            if len(st["periods"]) > 64:
                del st["periods"][:32]
        st["last"] = now
        st["count"] += 1
        st["bytes"] += len(frame.data)


def _period_stats(st):
    if not st["periods"]:
        return None, None
    pavg = sum(st["periods"]) / len(st["periods"])
    var = sum((p - pavg) ** 2 for p in st["periods"]) / len(st["periods"])
    return pavg, var ** 0.5


def activate(context):
    global _dbc, _dbc_path, _running, _ref_kbps, _win
    _dbc = None
    _dbc_path = ""
    _running = True
    _bins.clear()
    _ids.clear()
    globals()["_first_ts"] = None
    globals()["_last_ts"] = None
    globals()["_total_frames"] = 0

    win = sin.ui.create_window("CAN Quality Report")
    win.resize(1000, 660)
    plugin_shell.attach_status_bar(win, "Collecting…")
    _win = win

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    dbc_btn = QPushButton("Load DBC…")
    report_btn = QPushButton("Generate HTML…")
    csv_btn = QPushButton("Export CSV")
    reset_btn = QPushButton("Reset stats")
    top.addWidget(dbc_btn)
    top.addWidget(QLabel("Ref baud:"))
    baud_spin = QDoubleSpinBox()
    baud_spin.setRange(10, 8000)
    baud_spin.setValue(500)
    baud_spin.setSuffix(" kbps")
    top.addWidget(baud_spin)
    top.addStretch(1)
    top.addWidget(report_btn)
    top.addWidget(csv_btn)
    top.addWidget(reset_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Subscribe-only bus health: 1s load bins, Top Talker, period jitter, "
        "optional DBC coverage. Export HTML report or CSV."))

    summary = QLabel("Collecting (1s windows)…")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    load_tab = QWidget()
    lv = QVBoxLayout(load_tab)
    load_view = QTextEdit()
    load_view.setReadOnly(True)
    lv.addWidget(load_view, 1)
    tabs.addTab(load_tab, "Load overview")

    talker_tab = QWidget()
    tv = QVBoxLayout(talker_tab)
    talker_tree = QTreeWidget()
    talker_tree.setHeaderLabels([
        "Rank", "ID", "Frames", "Share %", "Bytes",
        "Period ms", "Jitter σ ms", "DBC"])
    talker_tree.setRootIsDecorated(False)
    talker_tree.setAlternatingRowColors(True)
    talker_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tv.addWidget(talker_tree, 1)
    tabs.addTab(talker_tab, "Top Talker")

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "dbc_path": _dbc_path,
            "ref_kbps": baud_spin.value(),
        })

    def _metrics():
        elapsed = (_last_ts - _first_ts) if (_first_ts and _last_ts) else 0
        total_frames = sum(b[1] for b in _bins)
        total_bytes = sum(b[2] for b in _bins)
        avg_fps = total_frames / max(0.001, elapsed)
        avg_kbps = total_bytes * 8 / 1000.0 / max(0.001, elapsed)
        ref = baud_spin.value() or 500.0
        load_pct = min(100.0, avg_kbps / ref * 100.0) if ref else 0.0
        peak = max((b[1] for b in _bins), default=0)
        idle = sum(1 for b in _bins if b[1] == 0)
        idle_pct = 100.0 * idle / max(1, len(_bins))
        return {
            "elapsed": elapsed, "avg_fps": avg_fps, "avg_kbps": avg_kbps,
            "load_pct": load_pct, "peak": peak, "idle_pct": idle_pct,
            "total_frames": total_frames, "ref": ref,
        }

    def _refresh():
        if not _bins:
            return
        m = _metrics()
        if _dbc:
            covered = sum(1 for cid in _ids if cid in _dbc.messages)
            cov_text = "DBC coverage %.1f%% (%d/%d IDs defined)" % (
                100.0 * covered / max(1, len(_ids)), covered, len(_ids))
        else:
            cov_text = "DBC not loaded"
        summary.setText(
            "Observed %.0fs · frames %d · avg %.0f fps · %.1f kbps · "
            "load≈%.1f%% · peak %d fps · idle %.1f%%"
            % (m["elapsed"], _total_frames, m["avg_fps"], m["avg_kbps"],
               m["load_pct"], m["peak"], m["idle_pct"]))
        peak_b = max((b[1] for b in _bins), default=1) or 1
        bars = []
        for b in _bins[-60:]:
            h = int(60 * b[1] / peak_b)
            bars.append(
                "<div style='display:inline-block;width:8px;height:%dpx;"
                "background:#1565c0;margin:0 1px;vertical-align:bottom' "
                "title='%d frames'></div>" % (max(1, h), b[1]))
        load_view.setHtml(
            "<h3>Load overview (last 60s, frames/s)</h3>"
            "<div style='height:70px'>%s</div>"
            "<p>%s<br>avg %.0f fps · %.1f kbps · idle %.1f%% "
            "(ref %.0f kbps)</p>"
            % ("".join(bars), cov_text, m["avg_fps"], m["avg_kbps"],
               m["idle_pct"], m["ref"]))
        talker_tree.clear()
        ranked = sorted(_ids.items(), key=lambda kv: -kv[1]["count"])[:50]
        for rank, (cid, st) in enumerate(ranked, 1):
            pct = 100.0 * st["count"] / max(1, _total_frames)
            pavg, jitter = _period_stats(st)
            pavg_s = "%.1f" % pavg if pavg is not None else "-"
            jitter_s = "%.1f" % jitter if jitter is not None else "-"
            dbc_name = (
                _dbc.messages[cid].name if (_dbc and cid in _dbc.messages) else "-")
            talker_tree.addTopLevelItem(QTreeWidgetItem([
                str(rank), "0x%X" % cid, str(st["count"]), "%.2f" % pct,
                str(st["bytes"]), pavg_s, jitter_s, dbc_name]))

    def _on_load_dbc():
        global _dbc, _dbc_path
        path = dbc_picker.pick_dbc(win, "Select DBC for coverage")
        if not path:
            return
        _dbc = dbcparse.parse_file(path)
        _dbc_path = path
        _persist()
        plugin_shell.set_status(
            win, "DBC loaded (%d messages)" % len(_dbc.messages), 3000)

    def _on_report():
        if _total_frames < 10:
            QMessageBox.information(win, "Report", "Need at least 10 frames")
            return
        path, _ = QFileDialog.getSaveFileName(
            win, "Generate HTML report", "bus_report.html", "HTML (*.html)")
        if not path:
            return
        try:
            m = _metrics()
            parts = [
                "<!DOCTYPE html><html><head><meta charset='utf-8'>",
                "<title>CAN Quality Report</title>",
                "<style>",
                "body{font-family:Segoe UI,system-ui,sans-serif;margin:24px;}",
                "table{border-collapse:collapse;width:100%;font-size:12px;}",
                "th,td{border:1px solid #cfd8dc;padding:4px 6px;}",
                "th{background:#eceff1;text-align:left;}",
                "</style></head><body>",
                "<h1>CAN Quality Report</h1>",
                "<p>Generated %s · observed %.0fs · frames %d · IDs %d</p><hr>"
                % (time.strftime("%Y-%m-%d %H:%M:%S"), m["elapsed"],
                   _total_frames, len(_ids)),
                "<h2>Key metrics</h2><ul>",
                "<li>Average load: %.1f fps (%.1f kbps, %.1f%% of %.0f kbps)</li>"
                % (m["avg_fps"], m["avg_kbps"], m["load_pct"], m["ref"]),
                "<li>Peak: %d fps</li>" % m["peak"],
                "<li>Idle rate: %.1f%%</li>" % m["idle_pct"],
            ]
            if _ids:
                top_cid, top_st = max(_ids.items(), key=lambda kv: kv[1]["count"])
                parts.append(
                    "<li>Top Talker: 0x%X (%d frames)</li>"
                    % (top_cid, top_st["count"]))
            parts.append("</ul>")
            if _dbc:
                covered = sum(1 for cid in _ids if cid in _dbc.messages)
                parts.append(
                    "<p>DBC coverage: %.1f%% (%d/%d) · %s</p>"
                    % (100.0 * covered / max(1, len(_ids)), covered, len(_ids),
                       html_mod.escape(os.path.basename(_dbc_path or ""))))
            parts.append(
                "<h2>Top Talker (20)</h2>"
                "<table><tr><th>ID</th><th>Frames</th><th>Share</th>"
                "<th>Period ms</th><th>Jitter σ</th><th>DBC</th></tr>")
            for cid, st in sorted(_ids.items(), key=lambda kv: -kv[1]["count"])[:20]:
                pavg, jitter = _period_stats(st)
                p_s = "%.1f" % pavg if pavg is not None else "-"
                j_s = "%.1f" % jitter if jitter is not None else "-"
                name = (
                    _dbc.messages[cid].name
                    if (_dbc and cid in _dbc.messages) else "")
                parts.append(
                    "<tr><td>0x%X</td><td>%d</td><td>%.2f%%</td>"
                    "<td>%s</td><td>%s</td><td>%s</td></tr>"
                    % (cid, st["count"],
                       100.0 * st["count"] / max(1, _total_frames),
                       p_s, j_s, html_mod.escape(name)))
            parts.append("</table></body></html>")
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(parts))
            _persist()
            plugin_shell.set_status(win, "Report written %s" % path, 5000)
        except OSError as e:
            QMessageBox.warning(win, "Report failed", str(e))

    def _on_csv():
        if not _ids:
            QMessageBox.information(win, "Export", "No stats yet")
            return
        rows = []
        for cid, st in sorted(_ids.items(), key=lambda kv: -kv[1]["count"]):
            pavg, jitter = _period_stats(st)
            rows.append([
                "0x%X" % cid, st["count"],
                "%.2f" % (100.0 * st["count"] / max(1, _total_frames)),
                st["bytes"],
                "%.1f" % pavg if pavg is not None else "-",
                "%.1f" % jitter if jitter is not None else "-",
            ])
        path = plugin_shell.export_csv(
            win,
            ["ID", "Frames", "Share%", "Bytes", "Period_ms", "Jitter_ms"],
            rows,
            "bus_stats.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    def _on_reset():
        globals()["_bins"].clear()
        globals()["_ids"].clear()
        globals()["_first_ts"] = None
        globals()["_last_ts"] = None
        globals()["_total_frames"] = 0
        talker_tree.clear()
        load_view.clear()
        summary.setText("Collecting (1s windows)…")
        plugin_shell.set_status(win, "Stats reset", 2000)

    dbc_btn.clicked.connect(_on_load_dbc)
    report_btn.clicked.connect(_on_report)
    csv_btn.clicked.connect(_on_csv)
    reset_btn.clicked.connect(_on_reset)
    baud_spin.valueChanged.connect(lambda _v: _persist())
    plugin_shell.bind_shortcut(win, "Ctrl+E", _on_csv)

    context.on_frame(_on_frame)
    context.register_command(
        "canQualityReport.open", plugin_shell.bind_raise(win),
        "Tools: Quality Report")

    saved = state_store.load_state(PLUGIN_ID, default={}) or {}
    if saved.get("ref_kbps"):
        baud_spin.setValue(float(saved["ref_kbps"]))
    p = saved.get("dbc_path") or ""
    if p and os.path.isfile(p):
        try:
            _dbc = dbcparse.parse_file(p)
            _dbc_path = p
        except OSError:
            pass

    timer = QTimer(win)
    timer.timeout.connect(_refresh)
    timer.start(1000)

    win.show()
    sin.output.append("can-quality-report loaded (load / period / HTML+CSV)")


def deactivate():
    global _running, _win
    _running = False
    _win = None
    try:
        sin.output.append("can-quality-report deactivated")
    except Exception:
        pass
