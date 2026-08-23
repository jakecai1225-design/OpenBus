# -*- coding: utf-8 -*-
"""can-quality-report 插件 — 总线体检报告（CANalyzer 统计报告风格）
功能：
- 负载统计：按时间窗（1s）帧数/字节数 → 总线负载率曲线
- Top Talker：帧数/字节排名
- 周期稳定性：逐 ID 周期均值/抖动 σ
- DBC 覆盖率：总线上有 DBC 定义的 ID 占比
- 空闲率：无帧时间占比
- 一键生成 HTML 报告 / CSV 导出
- 纯订阅只读
依赖: pip install PyQt6
"""

import time

from PyQt6.QtCore import QTimer

import sin
import dbcparse

_window = 1.0           # 统计窗口 1s
_bins = []              # [(t_start, frames, bytes)]
_ids = {}               # id -> {count, bytes, periods: [ms], last}
_dbc = None
_first_ts = None
_last_ts = None
_total_frames = 0
_idle_windows = 0
_running = True


def _on_frame(frame):
    global _first_ts, _last_ts, _total_frames
    if not _running:
        return
    now = time.time()
    if _first_ts is None:
        _first_ts = now
    _last_ts = now
    _total_frames += 1
    # 窗口分桶
    bin_start = now - (now % _window)
    if not _bins or _bins[-1][0] != bin_start:
        _bins.append([bin_start, 0, 0])
        if len(_bins) > 3600:
            del _bins[:1800]
    _bins[-1][1] += 1
    _bins[-1][2] += len(frame.data) + 8   # 帧开销估算
    # ID 统计
    st = _ids.get(frame.id)
    if st is None:
        _ids[frame.id] = {"count": 1, "bytes": len(frame.data),
                          "periods": [], "last": now, "ext": frame.extended}
    else:
        dt = (now - st["last"]) * 1000.0
        if 0 < dt < 10000:
            st["periods"].append(dt)
            if len(st["periods"]) > 64:
                del st["periods"][:32]
        st["last"] = now
        st["count"] += 1
        st["bytes"] += len(frame.data)
    if _bins[-1][1] == 0:
        _idle_windows += 1


def activate(context):
    global _dbc
    _dbc = None

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QTabWidget, QTextEdit, QProgressBar
        )
    except ImportError:
        sin.output.append("总线体检插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("总线体检报告")
    win.resize(980, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    dbc_btn = QPushButton("加载 DBC（覆盖率）…")
    report_btn = QPushButton("生成 HTML 报告")
    csv_btn = QPushButton("导出 CSV")
    reset_btn = QPushButton("重新统计")
    top.addWidget(dbc_btn)
    top.addStretch(1)
    top.addWidget(report_btn)
    top.addWidget(csv_btn)
    top.addWidget(reset_btn)
    layout.addLayout(top)

    summary = QLabel("订阅统计中（1s 窗口）...")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    load_tab = QWidget()
    lv = QVBoxLayout(load_tab)
    load_view = QTextEdit()
    load_view.setReadOnly(True)
    lv.addWidget(load_view, 1)
    tabs.addTab(load_tab, "负载概览")

    talker_tab = QWidget()
    tv = QVBoxLayout(talker_tab)
    talker_tree = QTreeWidget()
    talker_tree.setHeaderLabels(["排名", "ID", "帧数", "占比%", "字节数", "周期ms", "抖动σms", "DBC"])
    talker_tree.setRootIsDecorated(False)
    talker_tree.setAlternatingRowColors(True)
    th = talker_tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tv.addWidget(talker_tree, 1)
    tabs.addTab(talker_tab, "Top Talker")

    def _refresh():
        elapsed = (_last_ts - _first_ts) if (_first_ts and _last_ts) else 0
        if not _bins:
            return
        # 负载率：帧/秒与字节率（假设 500kbps 总线）
        total_frames = sum(b[1] for b in _bins)
        total_bytes = sum(b[2] for b in _bins)
        avg_fps = total_frames / max(0.001, elapsed)
        avg_kbps = total_bytes * 8 / 1000.0 / max(0.001, elapsed)
        load_pct = min(100.0, avg_kbps / 500.0)   # 500kbps 参考
        peak = max((b[1] for b in _bins), default=0)
        idle = sum(1 for b in _bins if b[1] == 0)
        idle_pct = 100.0 * idle / max(1, len(_bins))
        # DBC 覆盖率
        if _dbc:
            covered = sum(1 for cid in _ids if cid in _dbc.messages)
            cov_pct = 100.0 * covered / max(1, len(_ids))
            cov_text = "DBC 覆盖率 %.1f%%（%d/%d ID 有定义）" % (cov_pct, covered, len(_ids))
        else:
            cov_text = "DBC 未加载"
        summary.setText("观察 %.0fs · 总帧 %d · 平均 %.0f 帧/s · %.1f kbps · "
                        "负载≈%.1f%% · 峰值 %d 帧/s · 空闲 %.1f%%"
                        % (elapsed, _total_frames, avg_fps, avg_kbps,
                           load_pct, peak, idle_pct))
        # 负载概览 HTML
        bars = []
        peak_b = max((b[1] for b in _bins), default=1) or 1
        for b in _bins[-60:]:
            h = int(60 * b[1] / peak_b)
            bars.append("<div style='display:inline-block;width:8px;height:%dpx;"
                        "background:#1565c0;margin:0 1px;vertical-align:bottom' "
                        "title='%d 帧'></div>" % (max(1, h), b[1]))
        load_view.setHtml(
            "<h3>负载概览（最近 60 秒，每秒帧数）</h3>"
            "<div style='height:70px'>%s</div>"
            "<p>%s<br>平均 %.0f 帧/s · %.1f kbps · 空闲率 %.1f%%</p>"
            % ("".join(bars), cov_text, avg_fps, avg_kbps, idle_pct))
        # Top Talker
        talker_tree.clear()
        ranked = sorted(_ids.items(), key=lambda kv: -kv[1]["count"])[:50]
        for rank, (cid, st) in enumerate(ranked, 1):
            pct = 100.0 * st["count"] / max(1, _total_frames)
            if st["periods"]:
                pavg = sum(st["periods"]) / len(st["periods"])
                var = sum((p - pavg) ** 2 for p in st["periods"]) / len(st["periods"])
                jitter = var ** 0.5
                pavg_s, jitter_s = "%.1f" % pavg, "%.1f" % jitter
            else:
                pavg_s = jitter_s = "-"
            dbc_name = _dbc.messages[cid].name if (_dbc and cid in _dbc.messages) else "-"
            talker_tree.addTopLevelItem(QTreeWidgetItem([
                str(rank), "0x%X" % cid, str(st["count"]), "%.2f" % pct,
                str(st["bytes"]), pavg_s, jitter_s, dbc_name]))

    def _on_load_dbc():
        global _dbc
        path, _ = QFileDialog.getOpenFileName(win, "加载 DBC", "", "DBC (*.dbc)")
        if not path:
            return
        _dbc = dbcparse.parse_file(path)
        QMessageBox.information(win, "加载成功", "%d 报文定义" % len(_dbc.messages))

    def _on_report():
        elapsed = (_last_ts - _first_ts) if (_first_ts and _last_ts) else 0
        if _total_frames < 10:
            QMessageBox.information(win, "提示", "数据不足（至少 10 帧）")
            return
        path, _ = QFileDialog.getSaveFileName(win, "生成报告", "bus_report.html",
                                              "HTML (*.html)")
        if not path:
            return
        try:
            total_frames = sum(b[1] for b in _bins)
            total_bytes = sum(b[2] for b in _bins)
            avg_fps = total_frames / max(0.001, elapsed)
            avg_kbps = total_bytes * 8 / 1000.0 / max(0.001, elapsed)
            idle = sum(1 for b in _bins if b[1] == 0)
            peak = max((b[1] for b in _bins), default=0)
            html = ["<html><head><meta charset='utf-8'><title>总线体检报告</title></head><body>",
                    "<h1>总线体检报告</h1>",
                    "<p>生成: %s · 观察时长 %.0fs · 总帧 %d · ID 数 %d</p><hr>" % (
                        time.strftime("%Y-%m-%d %H:%M:%S"), elapsed, _total_frames, len(_ids)),
                    "<h2>核心指标</h2><ul>",
                    "<li>平均负载: %.1f 帧/s（%.1f kbps，500kbps 参考 %.1f%%）</li>" % (
                        avg_fps, avg_kbps, min(100.0, avg_kbps / 5.0)),
                    "<li>峰值: %d 帧/s</li>" % peak,
                    "<li>空闲率: %.1f%%</li>" % (100.0 * idle / max(1, len(_bins))),
                    "<li>Top Talker: %s</li>" % (
                        "0x%X（%d 帧）" % max(_ids.items(), key=lambda kv: kv[1]["count"])
                        if _ids else "-"),
                    "</ul>"]
            if _dbc:
                covered = sum(1 for cid in _ids if cid in _dbc.messages)
                html.append("<p>DBC 覆盖率: %.1f%%（%d/%d）</p>" % (
                    100.0 * covered / max(1, len(_ids)), covered, len(_ids)))
            html.append("<h2>Top Talker（前 20）</h2><table border='1' cellpadding='4'>"
                        "<tr><th>ID</th><th>帧数</th><th>占比</th><th>周期ms</th><th>抖动σ</th></tr>")
            for cid, st in sorted(_ids.items(), key=lambda kv: -kv[1]["count"])[:20]:
                if st["periods"]:
                    pavg = sum(st["periods"]) / len(st["periods"])
                    var = sum((p - pavg) ** 2 for p in st["periods"]) / len(st["periods"])
                    jitter = var ** 0.5
                    p_s, j_s = "%.1f" % pavg, "%.1f" % jitter
                else:
                    p_s = j_s = "-"
                html.append("<tr><td>0x%X</td><td>%d</td><td>%.2f%%</td><td>%s</td><td>%s</td></tr>"
                            % (cid, st["count"], 100.0 * st["count"] / _total_frames, p_s, j_s))
            html.append("</table></body></html>")
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(html))
            QMessageBox.information(win, "报告已生成", path)
        except OSError as e:
            QMessageBox.warning(win, "生成失败", str(e))

    def _on_csv():
        if not _ids:
            QMessageBox.information(win, "提示", "无统计数据")
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出统计", "bus_stats.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("ID,帧数,占比%,字节数,周期均值ms,抖动ms\n")
                for cid, st in sorted(_ids.items(), key=lambda kv: -kv[1]["count"]):
                    if st["periods"]:
                        pavg = sum(st["periods"]) / len(st["periods"])
                        var = sum((p - pavg) ** 2 for p in st["periods"]) / len(st["periods"])
                        f.write("0x%X,%d,%.2f,%d,%.1f,%.1f\n" % (
                            cid, st["count"], 100.0 * st["count"] / _total_frames,
                            st["bytes"], pavg, var ** 0.5))
                    else:
                        f.write("0x%X,%d,%.2f,%d,-,-\n" % (
                            cid, st["count"], 100.0 * st["count"] / _total_frames, st["bytes"]))
            QMessageBox.information(win, "导出成功", path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_reset():
        globals()["_bins"].clear()
        globals()["_ids"].clear()
        globals()["_first_ts"] = None
        globals()["_last_ts"] = None
        globals()["_total_frames"] = 0

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("canQualityReport.open", _on_open_cmd, "统计: 总线体检")

    dbc_btn.clicked.connect(_on_load_dbc)
    report_btn.clicked.connect(_on_report)
    csv_btn.clicked.connect(_on_csv)
    reset_btn.clicked.connect(_on_reset)

    timer = QTimer()
    timer.timeout.connect(_refresh)
    timer.start(1000)

    win.show()
    sin.output.append("总线体检插件已加载（负载/TopTalker/抖动/覆盖率，只读安全）")


def deactivate():
    global _running
    _running = False
    sin.output.append("总线体检插件已停用")
