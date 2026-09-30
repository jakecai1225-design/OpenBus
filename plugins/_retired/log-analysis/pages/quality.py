# -*- coding: utf-8 -*-
"""Quality workspace — bus health report (load / period / coverage)."""

from __future__ import annotations

import html as html_mod
import os
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QDoubleSpinBox, QFileDialog, QHBoxLayout, QHeaderView, QLabel,
    QMessageBox, QPushButton, QTabWidget, QTextEdit, QTreeWidget,
    QTreeWidgetItem, QVBoxLayout, QWidget,
)

from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PAGE_STATE = "quality.json"


def _period_stats(st):
    if not st["periods"]:
        return None, None
    pavg = sum(st["periods"]) / len(st["periods"])
    var = sum((p - pavg) ** 2 for p in st["periods"]) / len(st["periods"])
    return pavg, var ** 0.5


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    state = {
        "window": 1.0,
        "bins": [],
        "ids": {},
        "dbc": None,
        "dbc_path": "",
        "first_ts": None,
        "last_ts": None,
        "total_frames": 0,
    }

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
    talker_tree.header().setSectionResizeMode(
        QHeaderView.ResizeMode.ResizeToContents)
    tv.addWidget(talker_tree, 1)
    tabs.addTab(talker_tab, "Top Talker")

    def _persist():
        state_store.save_state("log-analysis", {
            "dbc_path": state["dbc_path"],
            "ref_kbps": baud_spin.value(),
        }, PAGE_STATE)

    def _on_frame(frame):
        now = time.time()
        if state["first_ts"] is None:
            state["first_ts"] = now
        state["last_ts"] = now
        state["total_frames"] += 1
        bin_start = now - (now % state["window"])
        bins = state["bins"]
        if not bins or bins[-1][0] != bin_start:
            bins.append([bin_start, 0, 0])
            if len(bins) > 3600:
                del bins[:1800]
        bins[-1][1] += 1
        bins[-1][2] += len(frame.data) + 8
        st = state["ids"].get(frame.id)
        if st is None:
            state["ids"][frame.id] = {
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

    def _metrics():
        elapsed = (
            (state["last_ts"] - state["first_ts"])
            if (state["first_ts"] and state["last_ts"]) else 0)
        total_frames = sum(b[1] for b in state["bins"])
        total_bytes = sum(b[2] for b in state["bins"])
        avg_fps = total_frames / max(0.001, elapsed)
        avg_kbps = total_bytes * 8 / 1000.0 / max(0.001, elapsed)
        ref = baud_spin.value() or 500.0
        load_pct = min(100.0, avg_kbps / ref * 100.0) if ref else 0.0
        peak = max((b[1] for b in state["bins"]), default=0)
        idle = sum(1 for b in state["bins"] if b[1] == 0)
        idle_pct = 100.0 * idle / max(1, len(state["bins"]))
        return {
            "elapsed": elapsed, "avg_fps": avg_fps, "avg_kbps": avg_kbps,
            "load_pct": load_pct, "peak": peak, "idle_pct": idle_pct,
            "total_frames": total_frames, "ref": ref,
        }

    def _refresh():
        if not state["bins"]:
            return
        m = _metrics()
        dbc = state["dbc"]
        if dbc:
            covered = sum(1 for cid in state["ids"] if cid in dbc.messages)
            cov_text = "DBC coverage %.1f%% (%d/%d IDs defined)" % (
                100.0 * covered / max(1, len(state["ids"])),
                covered, len(state["ids"]))
        else:
            cov_text = "DBC not loaded"
        summary.setText(
            "Observed %.0fs · frames %d · avg %.0f fps · %.1f kbps · "
            "load≈%.1f%% · peak %d fps · idle %.1f%%"
            % (m["elapsed"], state["total_frames"], m["avg_fps"], m["avg_kbps"],
               m["load_pct"], m["peak"], m["idle_pct"]))
        peak_b = max((b[1] for b in state["bins"]), default=1) or 1
        bars = []
        for b in state["bins"][-60:]:
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
        ranked = sorted(state["ids"].items(), key=lambda kv: -kv[1]["count"])[:50]
        for rank, (cid, st) in enumerate(ranked, 1):
            pct = 100.0 * st["count"] / max(1, state["total_frames"])
            pavg, jitter = _period_stats(st)
            pavg_s = "%.1f" % pavg if pavg is not None else "-"
            jitter_s = "%.1f" % jitter if jitter is not None else "-"
            dbc_name = (
                dbc.messages[cid].name if (dbc and cid in dbc.messages) else "-")
            talker_tree.addTopLevelItem(QTreeWidgetItem([
                str(rank), "0x%X" % cid, str(st["count"]), "%.2f" % pct,
                str(st["bytes"]), pavg_s, jitter_s, dbc_name]))

    def _on_load_dbc():
        path = dbc_picker.pick_dbc(parent, "Select DBC for coverage")
        if not path:
            return
        state["dbc"] = dbcparse.parse_file(path)
        state["dbc_path"] = path
        session.note_dbc_path(path)
        _persist()
        plugin_shell.set_status(
            parent, "DBC loaded (%d messages)" % len(state["dbc"].messages),
            3000)

    def _on_report():
        if state["total_frames"] < 10:
            QMessageBox.information(parent, "Report", "Need at least 10 frames")
            return
        start = session.start_dir()
        path, _ = QFileDialog.getSaveFileName(
            parent, "Generate HTML report",
            os.path.join(start, "bus_report.html") if start else "bus_report.html",
            "HTML (*.html)")
        if not path:
            return
        try:
            m = _metrics()
            dbc = state["dbc"]
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
                   state["total_frames"], len(state["ids"])),
                "<h2>Key metrics</h2><ul>",
                "<li>Average load: %.1f fps (%.1f kbps, %.1f%% of %.0f kbps)</li>"
                % (m["avg_fps"], m["avg_kbps"], m["load_pct"], m["ref"]),
                "<li>Peak: %d fps</li>" % m["peak"],
                "<li>Idle rate: %.1f%%</li>" % m["idle_pct"],
            ]
            if state["ids"]:
                top_cid, top_st = max(
                    state["ids"].items(), key=lambda kv: kv[1]["count"])
                parts.append(
                    "<li>Top Talker: 0x%X (%d frames)</li>"
                    % (top_cid, top_st["count"]))
            parts.append("</ul>")
            if dbc:
                covered = sum(1 for cid in state["ids"] if cid in dbc.messages)
                parts.append(
                    "<p>DBC coverage: %.1f%% (%d/%d) · %s</p>"
                    % (100.0 * covered / max(1, len(state["ids"])),
                       covered, len(state["ids"]),
                       html_mod.escape(os.path.basename(state["dbc_path"] or ""))))
            parts.append(
                "<h2>Top Talker (20)</h2>"
                "<table><tr><th>ID</th><th>Frames</th><th>Share</th>"
                "<th>Period ms</th><th>Jitter σ</th><th>DBC</th></tr>")
            for cid, st in sorted(
                    state["ids"].items(), key=lambda kv: -kv[1]["count"])[:20]:
                pavg, jitter = _period_stats(st)
                p_s = "%.1f" % pavg if pavg is not None else "-"
                j_s = "%.1f" % jitter if jitter is not None else "-"
                name = (
                    dbc.messages[cid].name
                    if (dbc and cid in dbc.messages) else "")
                parts.append(
                    "<tr><td>0x%X</td><td>%d</td><td>%.2f%%</td>"
                    "<td>%s</td><td>%s</td><td>%s</td></tr>"
                    % (cid, st["count"],
                       100.0 * st["count"] / max(1, state["total_frames"]),
                       p_s, j_s, html_mod.escape(name)))
            parts.append("</table></body></html>")
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(parts))
            session.note_export_path(path)
            _persist()
            plugin_shell.set_status(parent, "Report written %s" % path, 5000)
        except OSError as e:
            QMessageBox.warning(parent, "Report failed", str(e))

    def _on_csv():
        if not state["ids"]:
            QMessageBox.information(parent, "Export", "No stats yet")
            return
        rows = []
        for cid, st in sorted(state["ids"].items(), key=lambda kv: -kv[1]["count"]):
            pavg, jitter = _period_stats(st)
            rows.append([
                "0x%X" % cid, st["count"],
                "%.2f" % (100.0 * st["count"] / max(1, state["total_frames"])),
                st["bytes"],
                "%.1f" % pavg if pavg is not None else "-",
                "%.1f" % jitter if jitter is not None else "-",
            ])
        path = plugin_shell.export_csv(
            parent,
            ["ID", "Frames", "Share%", "Bytes", "Period_ms", "Jitter_ms"],
            rows,
            "bus_stats.csv",
        )
        if path:
            session.note_export_path(path)
            plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    def _on_reset():
        state["bins"].clear()
        state["ids"].clear()
        state["first_ts"] = None
        state["last_ts"] = None
        state["total_frames"] = 0
        talker_tree.clear()
        load_view.clear()
        summary.setText("Collecting (1s windows)…")
        plugin_shell.set_status(parent, "Stats reset", 2000)

    dbc_btn.clicked.connect(_on_load_dbc)
    report_btn.clicked.connect(_on_report)
    csv_btn.clicked.connect(_on_csv)
    reset_btn.clicked.connect(_on_reset)
    baud_spin.valueChanged.connect(lambda _v: _persist())

    session.on_bus_frame(_on_frame)

    saved = state_store.load_state("log-analysis", PAGE_STATE) or {}
    if saved.get("ref_kbps"):
        baud_spin.setValue(float(saved["ref_kbps"]))
    p = saved.get("dbc_path") or session.last_dbc_path or ""
    if p and os.path.isfile(p):
        try:
            state["dbc"] = dbcparse.parse_file(p)
            state["dbc_path"] = p
            session.note_dbc_path(p)
        except OSError:
            pass

    timer = QTimer(root)
    timer.timeout.connect(_refresh)
    timer.start(1000)

    return root
