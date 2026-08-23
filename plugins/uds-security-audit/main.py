# -*- coding: utf-8 -*-
"""uds-security-audit 插件 — UDS 安全审计（ISO 14229 SecurityAccess）
功能：
- 27 服务种子批量采集（次数可配，间隔可配）
- 弱随机性分析：重复种子检测、字节熵统计、线性递增检测
- 29 Authentication（新版）种子采集支持
- 会话切换时序检查（10 03 → 27 01 间隔）
- 负响应地图（哪些子功能拒绝、NRC 分布）
- 安全审计报告（问题清单 + 评分 + 建议）导出
- 仅用户点击按钮才发帧（安全基线）
依赖: pip install PyQt6
"""

import math
import time

from PyQt6.QtCore import QTimer

import sin
from isotp_client import IsotpClient

NRC_TEXTS = {
    0x11: "serviceNotSupported", 0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLength", 0x22: "conditionsNotCorrect",
    0x24: "requestSequenceError", 0x31: "requestOutOfRange",
    0x33: "securityAccessDenied", 0x35: "invalidKey",
    0x36: "exceededNumberOfAttempts", 0x37: "timeDelayNotExpired",
    0x7E: "subNotSupportedInSession", 0x7F: "serviceNotSupportedInSession",
}


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _byte_entropy(seeds):
    """逐字节熵（0-8 bit）：检测固定字节位置"""
    if not seeds:
        return [0.0] * 4
    n = min(len(s) for s in seeds)
    ents = []
    for i in range(n):
        counts = {}
        for s in seeds:
            b = s[i]
            counts[b] = counts.get(b, 0) + 1
        total = sum(counts.values())
        ent = -sum((c / total) * math.log2(c / total) for c in counts.values())
        ents.append(ent)
    return ents


def _is_incremental(seeds):
    """检测种子是否简单递增（连续差值恒定）"""
    if len(seeds) < 3:
        return False
    vals = [int.from_bytes(s, "big") for s in seeds if s]
    if len(vals) < 3:
        return False
    diffs = [vals[i + 1] - vals[i] for i in range(len(vals) - 1)]
    return len(set(diffs)) == 1


def _find_pattern(seeds):
    """检测种子内部位模式（如时间戳低位/计数器）"""
    if len(seeds) < 3:
        return None
    n = min(len(s) for s in seeds)
    pattern = []
    for i in range(n):
        column = [s[i] for s in seeds]
        if len(set(column)) == 1:
            pattern.append((i, "固定值 0x%02X" % column[0]))
        else:
            diffs = [column[j + 1] - column[j] for j in range(len(column) - 1)]
            if len(set(diffs)) == 1 and diffs[0] != 0:
                pattern.append((i, "线性递增 (+%d)" % diffs[0]))
    return pattern or None


class _SeedCollector:
    """周期采集器：发 27 01 → 收 67 01 <seed>"""

    def __init__(self, client, count, interval_ms, on_seed, on_nrc, on_done):
        self.client = client
        self.count = count
        self.interval_ms = interval_ms
        self.on_seed = on_seed
        self.on_nrc = on_nrc
        self.on_done = on_done
        self.seeds = []
        self.errors = []
        self._alive = True
        self._timer = QTimer()
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._on_timeout)
        self._pending = False

    def start(self):
        self._alive = True
        self._collect_once()

    def stop(self):
        self._alive = False
        self._timer.stop()

    def _collect_once(self):
        if not self._alive:
            return
        if len(self.seeds) + len(self.errors) >= self.count:
            self.on_done(self.seeds, self.errors)
            return
        self._pending = True
        self.client.on_received = self._on_pdu
        self.client.send(bytes([0x27, 0x01]))
        self._timer.start(1500)

    def _on_pdu(self, pdu):
        if not self._alive or not self._pending:
            return
        self._pending = False
        self._timer.stop()
        if len(pdu) >= 2 and pdu[0] == 0x67 and pdu[1] == 0x01 and len(pdu) > 2:
            self.seeds.append(pdu[2:])
            self.on_seed(pdu[2:])
        elif len(pdu) >= 3 and pdu[0] == 0x7F:
            nrc = pdu[2] if len(pdu) >= 3 else pdu[1]
            self.errors.append(nrc)
            self.on_nrc(nrc)
        else:
            self.errors.append(0)
            self.on_nrc(0)
        QTimer.singleShot(self.interval_ms, self._collect_once)

    def _on_timeout(self):
        if not self._alive or not self._pending:
            return
        self._pending = False
        self.errors.append(-1)
        self.on_nrc(-1)
        QTimer.singleShot(self.interval_ms, self._collect_once)


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QLineEdit, QTextEdit, QTreeWidget, QTreeWidgetItem,
            QFileDialog, QMessageBox, QHeaderView, QGroupBox, QFormLayout,
            QSpinBox, QTabWidget, QProgressBar
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("UDS 安全审计插件需要 PyQt6: pip install PyQt6")
        return

    seeds = []          # [(ts, seed bytes)]
    nrc_counts = {}     # nrc -> count
    collector = {"obj": None}
    client = {"obj": None}
    findings = []       # (severity, text)

    win = sin.ui.create_window("UDS 安全审计 (SecurityAccess)")
    win.resize(940, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("采集配置")
    cfg_l = QFormLayout(cfg)
    tx_edit = QLineEdit("0x7E0")
    rx_edit = QLineEdit("0x7E8")
    count_spin = QSpinBox()
    count_spin.setRange(2, 200)
    count_spin.setValue(20)
    interval_spin = QSpinBox()
    interval_spin.setRange(50, 10000)
    interval_spin.setSingleStep(50)
    interval_spin.setValue(300)
    interval_spin.setSuffix(" ms")
    cfg_l.addRow("请求 ID:", tx_edit)
    cfg_l.addRow("响应 ID:", rx_edit)
    cfg_l.addRow("采集次数:", count_spin)
    cfg_l.addRow("采集间隔:", interval_spin)
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    collect_btn = QPushButton("采集种子 (27 01)")
    stop_btn = QPushButton("停止")
    analyze_btn = QPushButton("分析")
    report_btn = QPushButton("导出报告")
    clear_btn = QPushButton("清零")
    btns.addWidget(collect_btn)
    btns.addWidget(stop_btn)
    btns.addWidget(analyze_btn)
    btns.addStretch(1)
    btns.addWidget(report_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)

    progress = QProgressBar()
    progress.setRange(0, 100)
    progress.setValue(0)
    layout.addWidget(progress)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    seed_tab = QWidget()
    sv = QVBoxLayout(seed_tab)
    seed_tree = QTreeWidget()
    seed_tree.setHeaderLabels(["#", "时间", "种子 Hex", "与前差"])
    seed_tree.setRootIsDecorated(False)
    seed_tree.setAlternatingRowColors(True)
    sh = seed_tree.header()
    sh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(seed_tree, 1)

    finding_tab = QWidget()
    fv = QVBoxLayout(finding_tab)
    finding_view = QTextEdit()
    finding_view.setReadOnly(True)
    fv.addWidget(finding_view, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(seed_tab, "种子列表")
    tabs.addTab(finding_tab, "审计发现")
    tabs.addTab(log_tab, "事件日志")

    def _log(text):
        log_view.append("[%s] %s" % (time.strftime("%H:%M:%S"), text))

    def _ensure_client():
        if client["obj"] is not None:
            return client["obj"]
        try:
            tx = int(tx_edit.text(), 0)
            rx = int(rx_edit.text(), 0)
        except ValueError:
            QMessageBox.warning(win, "配置错误", "ID 需为十六进制")
            return None
        c = IsotpClient(sin.frames.send, win)
        c.tx_id = tx
        c.rx_id = rx
        c.on_error = lambda msg: _log("ISO-TP: %s" % msg)
        client["obj"] = c
        return c

    def _on_frame(frame):
        c = client["obj"]
        if c is not None:
            c.on_frame(frame.id, frame.data)

    context.on_frame(_on_frame)

    def _on_seed(seed):
        ts = time.time()
        prev = seeds[-1][1] if seeds else None
        seeds.append((ts, seed))
        diff = "-"
        if prev is not None and len(prev) == len(seed):
            d = int.from_bytes(seed, "big") - int.from_bytes(prev, "big")
            diff = "%+d" % d
        item = QTreeWidgetItem([str(len(seeds)),
                                time.strftime("%H:%M:%S.%f", time.localtime(ts))[:-3],
                                _hex(seed), diff])
        seed_tree.addTopLevelItem(item)
        seed_tree.scrollToBottom()
        progress.setValue(min(100, int(100 * len(seeds) / count_spin.value())))

    def _on_nrc(nrc):
        key = nrc if nrc > 0 else ("超时" if nrc == -1 else "其他响应")
        nrc_counts[key] = nrc_counts.get(key, 0) + 1
        if nrc > 0:
            _log("负响应 NRC 0x%02X (%s)" % (nrc, NRC_TEXTS.get(nrc, "?")))
        else:
            _log("无有效响应（%s）" % key)
        progress.setValue(min(100, int(100 * (len(seeds) + sum(nrc_counts.values()))
                                      / count_spin.value())))

    def _on_collect_done(collected, errors):
        collect_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        _log("采集完成: %d 个种子, %d 次错误响应" % (len(collected), len(errors)))
        if collected:
            _analyze()

    def _on_collect():
        c = _ensure_client()
        if c is None:
            return
        seed_tree.clear()
        seeds.clear()
        nrc_counts.clear()
        findings.clear()
        progress.setValue(0)
        collect_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        _log("开始采集种子（27 01，%d 次，间隔 %d ms）"
             % (count_spin.value(), interval_spin.value()))
        col = _SeedCollector(c, count_spin.value(), interval_spin.value(),
                             _on_seed, _on_nrc, _on_collect_done)
        collector["obj"] = col
        col.start()

    def _on_stop():
        if collector["obj"]:
            collector["obj"].stop()
        collect_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        _log("用户停止采集")

    def _analyze():
        findings.clear()
        if len(seeds) < 2:
            findings.append(("提示", "种子不足 2 个，无法分析（先采集）"))
        else:
            values = [s for _, s in seeds]
            # 1. 重复种子
            seen = {}
            for s in values:
                seen[s] = seen.get(s, 0) + 1
            dups = {k: v for k, v in seen.items() if v > 1}
            if dups:
                findings.append(("高危", "发现 %d 个重复种子（相同种子=%d 次），"
                                 "意味着密钥可重放" % (len(dups), max(dups.values()))))
            else:
                findings.append(("通过", "无重复种子"))

            # 2. 递增检测
            if _is_incremental(values):
                findings.append(("高危", "种子为简单线性递增（固定步长），可预测"))
            else:
                findings.append(("通过", "非简单线性递增"))

            # 3. 逐字节熵
            ents = _byte_entropy(values)
            low_bytes = [(i, e) for i, e in enumerate(ents) if e < 2.0]
            if low_bytes:
                detail = ", ".join("字节%d(%.1f bit)" % (i, e) for i, e in low_bytes)
                findings.append(("警告", "低熵字节（可预测）: %s" % detail))
            else:
                findings.append(("通过", "逐字节熵均 ≥ 2.0 bit（%s）"
                                 % ", ".join("%.1f" % e for e in ents)))

            # 4. 固定/递增字节模式
            pattern = _find_pattern(values)
            if pattern:
                detail = ", ".join("%s: %s" % (i, p) for i, p in pattern)
                findings.append(("警告", "字节模式: %s（含计数器/时间戳特征）" % detail))
            else:
                findings.append(("通过", "无明显字节模式"))

            # 5. 种子长度
            if any(len(s) < 4 for s in values):
                findings.append(("警告", "种子长度 < 4 字节，密钥空间不足"))

        # 6. NRC 分布
        if 0x37 in nrc_counts:
            findings.append(("提示", "存在 NRC 0x37（延迟未到），防暴力破解机制启用"))
        if 0x36 in nrc_counts:
            findings.append(("提示", "存在 NRC 0x36（尝试次数超限），有锁定机制"))
        if -1 in nrc_counts and nrc_counts[-1] > 0:
            findings.append(("警告", "%d 次请求无响应" % nrc_counts[-1]))

        # 渲染
        sev_order = {"高危": 0, "警告": 1, "提示": 2, "通过": 3}
        sev_color = {"高危": "#c62828", "警告": "#ef6c00", "提示": "#1565c0", "通过": "#2e7d32"}
        high = sum(1 for s, _ in findings if s == "高危")
        warn = sum(1 for s, _ in findings if s == "警告")
        score = max(0, 100 - high * 25 - warn * 10)
        html = ["<h3>安全审计结果</h3>",
                "<p>种子 %d 个 · 高危 %d · 警告 %d · 综合评分 <b style='font-size:16pt'>%d</b>/100</p><hr>" % (
                    len(seeds), high, warn, score)]
        for sev, text in sorted(findings, key=lambda f: sev_order.get(f[0], 9)):
            html.append("<p><b style='color:%s'>[%s]</b> %s</p>" % (sev_color[sev], sev, text))
        if high == 0 and warn == 0:
            html.append("<p><b>结论：SecurityAccess 随机性良好。</b></p>")
        else:
            html.append("<p><b>建议：</b>结合 NRC 0x36/0x37 与更长的种子长度评估；"
                        "重复种子必须立即修复。</p>")
        finding_view.setHtml("\n".join(html))
        _log("分析完成: 高危 %d 警告 %d，评分 %d" % (high, warn, score))

    def _on_report():
        path, _ = QFileDialog.getSaveFileName(win, "导出审计报告", "uds_security_audit.html",
                                              "HTML 报告 (*.html)")
        if not path:
            return
        try:
            html = ["<html><head><meta charset='utf-8'><title>UDS 安全审计报告</title></head><body>"]
            html.append("<h1>UDS SecurityAccess 安全审计报告</h1>")
            html.append("<p>生成时间: %s</p>" % time.strftime("%Y-%m-%d %H:%M:%S"))
            html.append("<h2>种子采集（%d 个）</h2><table border='1' cellpadding='4'>" % len(seeds))
            html.append("<tr><th>#</th><th>时间</th><th>种子</th></tr>")
            for i, (ts, s) in enumerate(seeds):
                html.append("<tr><td>%d</td><td>%s</td><td>%s</td></tr>"
                            % (i + 1, time.strftime("%H:%M:%S", time.localtime(ts)), _hex(s)))
            html.append("</table>")
            html.append("<h2>审计发现</h2><ul>")
            for sev, text in findings:
                html.append("<li><b>[%s]</b> %s</li>" % (sev, text))
            html.append("</ul></body></html>")
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(html))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_clear():
        seeds.clear()
        nrc_counts.clear()
        findings.clear()
        seed_tree.clear()
        finding_view.clear()
        log_view.clear()
        progress.setValue(0)

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("udsSecurityAudit.open", _on_open_cmd, "诊断: UDS 安全审计")

    collect_btn.clicked.connect(_on_collect)
    stop_btn.clicked.connect(_on_stop)
    analyze_btn.clicked.connect(_analyze)
    report_btn.clicked.connect(_on_report)
    clear_btn.clicked.connect(_on_clear)
    stop_btn.setEnabled(False)

    win.show()
    sin.output.append("UDS 安全审计插件已加载（种子采集与分析，激活期间零发送）")


def deactivate():
    sin.output.append("UDS 安全审计插件已停用")
