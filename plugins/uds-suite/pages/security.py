# -*- coding: utf-8 -*-
"""Security workspace — observational SecurityAccess audit (no key brute-force)."""

from __future__ import annotations

import math
import time

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QTabWidget,
    QTextEdit,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store

PLUGIN_ID = "uds-suite"

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
    if not seeds:
        return []
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
    if len(seeds) < 3:
        return False
    vals = [int.from_bytes(s, "big") for s in seeds if s]
    if len(vals) < 3:
        return False
    diffs = [vals[i + 1] - vals[i] for i in range(len(vals) - 1)]
    return len(set(diffs)) == 1


def _find_pattern(seeds):
    if len(seeds) < 3:
        return None
    n = min(len(s) for s in seeds)
    pattern = []
    for i in range(n):
        column = [s[i] for s in seeds]
        if len(set(column)) == 1:
            pattern.append((i, "fixed 0x%02X" % column[0]))
        else:
            diffs = [column[j + 1] - column[j] for j in range(len(column) - 1)]
            if len(set(diffs)) == 1 and diffs[0] != 0:
                pattern.append((i, "linear +%d" % diffs[0]))
    return pattern or None


class _SeedCollector:
    """Observational: request seed (27 01) only — never send key (27 02)."""

    def __init__(self, session, count, interval_ms, on_seed, on_nrc, on_done,
                 enter_session=True):
        self.session = session
        self.count = count
        self.interval_ms = interval_ms
        self.on_seed = on_seed
        self.on_nrc = on_nrc
        self.on_done = on_done
        self.enter_session = enter_session
        self.seeds = []
        self.errors = []
        self.session_ms = None
        self._alive = True
        self._timer = QTimer()
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._on_timeout)
        self._pending = False
        self._phase = "session" if enter_session else "seed"
        self._session_t0 = 0.0

    def start(self):
        self._alive = True
        if self.enter_session:
            self._phase = "session"
            self._session_t0 = time.time()
            self._pending = True
            self.session.request(
                bytes([0x10, 0x03]), on_done=self._on_session_done,
                tag="Audit 10 03")
            self._timer.start(2000)
        else:
            self._phase = "seed"
            self._collect_once()

    def stop(self):
        self._alive = False
        self._timer.stop()
        try:
            self.session.client.cancel()
        except Exception:
            pass

    def _on_session_done(self, ok, resp, note):
        if not self._alive or not self._pending:
            return
        self._pending = False
        self._timer.stop()
        if self.session_ms is None:
            self.session_ms = (time.time() - self._session_t0) * 1000.0
        QTimer.singleShot(50, self._collect_once)

    def _collect_once(self):
        if not self._alive:
            return
        if len(self.seeds) + len(self.errors) >= self.count:
            self.on_done(self.seeds, self.errors, self.session_ms)
            return
        self._phase = "seed"
        self._pending = True

        def on_done(ok, resp, note):
            if not self._alive or not self._pending:
                return
            self._pending = False
            self._timer.stop()
            if ok and resp and len(resp) >= 2 and resp[0] == 0x67 and resp[1] == 0x01 and len(resp) > 2:
                self.seeds.append(resp[2:])
                self.on_seed(resp[2:])
            elif resp and len(resp) >= 3 and resp[0] == 0x7F:
                nrc = resp[2]
                self.errors.append(nrc)
                self.on_nrc(nrc)
            else:
                self.errors.append(0 if not ok else 0)
                self.on_nrc(-1 if not ok else 0)
            QTimer.singleShot(self.interval_ms, self._collect_once)

        self.session.request(
            bytes([0x27, 0x01]), on_done=on_done, tag="Audit 27 01 seed")
        self._timer.start(1500)

    def _on_timeout(self):
        if not self._alive or not self._pending:
            return
        self._pending = False
        if self._phase == "session":
            self.session_ms = -1.0
            try:
                self.session.client.cancel()
            except Exception:
                pass
            QTimer.singleShot(50, self._collect_once)
            return
        self.errors.append(-1)
        self.on_nrc(-1)
        try:
            self.session.client.cancel()
        except Exception:
            pass
        QTimer.singleShot(self.interval_ms, self._collect_once)


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    seeds = []
    nrc_counts = {}
    findings = []
    session_timing = [None]
    collector_ref = [None]

    banner = QLabel(
        "WARNING: Observational audit only. This tool requests seeds (27 01) "
        "and never sends SecurityAccess keys (27 02). Do not use for key "
        "brute-force. Unauthorized testing may be illegal.")
    banner.setWordWrap(True)
    banner.setStyleSheet(
        "background:#fff3e0;color:#e65100;border:1px solid #ef6c00;"
        "padding:8px;font-weight:bold;")
    layout.addWidget(banner)

    cfg = QGroupBox("Collection settings")
    cfg_l = QFormLayout(cfg)
    count_spin = QSpinBox()
    count_spin.setRange(2, 200)
    count_spin.setValue(20)
    interval_spin = QSpinBox()
    interval_spin.setRange(50, 10000)
    interval_spin.setSingleStep(50)
    interval_spin.setValue(300)
    interval_spin.setSuffix(" ms")
    cfg_l.addRow("Seed samples:", count_spin)
    cfg_l.addRow("Interval:", interval_spin)
    cfg_l.addRow(QLabel("Uses shared TX/RX from the connection strip."))
    layout.addWidget(cfg)

    layout.addWidget(plugin_shell.help_label(
        "Flow: optional 10 03 (session timing) → repeated 27 01 seed requests. "
        "Analysis: duplicates, linear increment, per-byte entropy, NRC histogram. "
        "No keys are ever sent."))

    btns = QHBoxLayout()
    collect_btn = QPushButton("Collect seeds (27 01)")
    stop_btn = QPushButton("Stop")
    analyze_btn = QPushButton("Analyze")
    report_html_btn = QPushButton("Export HTML")
    report_csv_btn = QPushButton("Export CSV")
    clear_btn = QPushButton("Clear")
    for w in (collect_btn, stop_btn, analyze_btn):
        btns.addWidget(w)
    btns.addStretch(1)
    for w in (report_html_btn, report_csv_btn, clear_btn):
        btns.addWidget(w)
    layout.addLayout(btns)
    stop_btn.setEnabled(False)

    progress = QProgressBar()
    progress.setRange(0, 100)
    progress.setValue(0)
    layout.addWidget(progress)

    timing_label = QLabel("Session timing (10 03 → first 27 01): -")
    layout.addWidget(timing_label)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    seed_tab = QWidget()
    sv = QVBoxLayout(seed_tab)
    seed_tree = QTreeWidget()
    seed_tree.setHeaderLabels(["#", "Time", "Seed hex", "Delta"])
    seed_tree.setRootIsDecorated(False)
    seed_tree.setAlternatingRowColors(True)
    seed_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(seed_tree, 1)

    nrc_tab = QWidget()
    nv = QVBoxLayout(nrc_tab)
    nrc_tree = QTreeWidget()
    nrc_tree.setHeaderLabels(["NRC / event", "Count", "Meaning"])
    nrc_tree.setRootIsDecorated(False)
    nrc_tree.setAlternatingRowColors(True)
    nv.addWidget(nrc_tree, 1)

    finding_tab = QWidget()
    fv = QVBoxLayout(finding_tab)
    finding_view = QTextEdit()
    finding_view.setReadOnly(True)
    fv.addWidget(finding_view, 1)

    tabs.addTab(seed_tab, "Seeds")
    tabs.addTab(nrc_tab, "NRC histogram")
    tabs.addTab(finding_tab, "Findings")

    def _plog(text):
        log_fn("RX", "-", b"", "[Security] %s" % text)

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "sec_count": count_spin.value(),
            "sec_interval_ms": interval_spin.value(),
        }, "security.json")

    def _refresh_nrc():
        nrc_tree.clear()
        for key, cnt in sorted(nrc_counts.items(), key=lambda x: str(x[0])):
            if isinstance(key, int) and key > 0:
                name = "0x%02X" % key
                meaning = NRC_TEXTS.get(key, "?")
            elif key == -1:
                name, meaning = "timeout", "no response"
            else:
                name, meaning = str(key), "other / unexpected"
            nrc_tree.addTopLevelItem(QTreeWidgetItem([name, str(cnt), meaning]))

    def _on_seed(seed):
        ts = time.time()
        prev = seeds[-1][1] if seeds else None
        seeds.append((ts, seed))
        diff = "-"
        if prev is not None and len(prev) == len(seed):
            d = int.from_bytes(seed, "big") - int.from_bytes(prev, "big")
            diff = "%+d" % d
        item = QTreeWidgetItem([
            str(len(seeds)),
            time.strftime("%H:%M:%S.%f", time.localtime(ts))[:-3],
            _hex(seed), diff,
        ])
        seed_tree.addTopLevelItem(item)
        seed_tree.scrollToBottom()
        done = len(seeds) + sum(nrc_counts.values())
        progress.setValue(min(100, int(100 * done / count_spin.value())))

    def _on_nrc(nrc):
        key = nrc if nrc > 0 else (-1 if nrc == -1 else "other")
        nrc_counts[key] = nrc_counts.get(key, 0) + 1
        _refresh_nrc()
        if nrc > 0:
            _plog("NRC 0x%02X (%s)" % (nrc, NRC_TEXTS.get(nrc, "?")))
        else:
            _plog("No valid seed (%s)" % key)
        done = len(seeds) + sum(nrc_counts.values())
        progress.setValue(min(100, int(100 * done / count_spin.value())))

    def _on_collect_done(collected, errors, session_ms):
        collector_ref[0] = None
        collect_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        session_timing[0] = session_ms
        if session_ms is None:
            timing_label.setText("Session timing: not measured")
        elif session_ms < 0:
            timing_label.setText("Session timing: 10 03 timed out")
        else:
            timing_label.setText(
                "Session timing (10 03 → first 27 01): %.0f ms" % session_ms)
        _plog("Collect done: %d seeds, %d errors" % (len(collected), len(errors)))
        plugin_shell.set_status(
            parent.window(), "Collected %d seeds" % len(collected), 5000)
        if collected:
            _analyze()

    def _on_collect():
        _persist()
        seed_tree.clear()
        seeds.clear()
        nrc_counts.clear()
        findings.clear()
        finding_view.clear()
        _refresh_nrc()
        progress.setValue(0)
        collect_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        _plog("Start seed collect: %d samples, interval %d ms" % (
            count_spin.value(), interval_spin.value()))
        collector_ref[0] = _SeedCollector(
            session, count_spin.value(), interval_spin.value(),
            _on_seed, _on_nrc, _on_collect_done, enter_session=True)
        collector_ref[0].start()

    def _on_stop():
        if collector_ref[0]:
            collector_ref[0].stop()
            collector_ref[0] = None
        collect_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        _plog("Collect stopped by user")

    def _analyze():
        findings.clear()
        if len(seeds) < 2:
            findings.append(("info", "Need at least 2 seeds to analyze"))
        else:
            values = [s for _, s in seeds]
            seen = {}
            for s in values:
                seen[s] = seen.get(s, 0) + 1
            dups = {k: v for k, v in seen.items() if v > 1}
            if dups:
                findings.append((
                    "critical",
                    "Found %d duplicate seed value(s) (max repeats=%d) — "
                    "replay risk" % (len(dups), max(dups.values()))))
            else:
                findings.append(("pass", "No duplicate seeds"))

            if _is_incremental(values):
                findings.append((
                    "critical",
                    "Seeds look linearly incremental (fixed step) — predictable"))
            else:
                findings.append(("pass", "Not a simple linear sequence"))

            ents = _byte_entropy(values)
            low_bytes = [(i, e) for i, e in enumerate(ents) if e < 2.0]
            if low_bytes:
                detail = ", ".join("byte%d(%.1f bit)" % (i, e) for i, e in low_bytes)
                findings.append(("warn", "Low-entropy bytes: %s" % detail))
            else:
                findings.append((
                    "pass",
                    "Per-byte entropy >= 2.0 bit (%s)" % ", ".join(
                        "%.1f" % e for e in ents)))

            pattern = _find_pattern(values)
            if pattern:
                detail = ", ".join("b%d: %s" % (i, p) for i, p in pattern)
                findings.append(("warn", "Byte pattern: %s" % detail))
            else:
                findings.append(("pass", "No obvious fixed/counter byte pattern"))

            if any(len(s) < 4 for s in values):
                findings.append(("warn", "Seed length < 4 bytes — small key space"))

        if 0x37 in nrc_counts:
            findings.append((
                "info", "NRC 0x37 (time delay) seen — anti-bruteforce delay present"))
        if 0x36 in nrc_counts:
            findings.append((
                "info", "NRC 0x36 (attempt limit) seen — lockout present"))
        if nrc_counts.get(-1, 0) > 0:
            findings.append(("warn", "%d request timeout(s)" % nrc_counts[-1]))

        st = session_timing[0]
        if st is not None and st >= 0:
            findings.append(("info", "Session switch latency ~%.0f ms" % st))

        sev_order = {"critical": 0, "warn": 1, "info": 2, "pass": 3}
        sev_color = {
            "critical": "#c62828", "warn": "#ef6c00",
            "info": "#1565c0", "pass": "#2e7d32",
        }
        high = sum(1 for s, _ in findings if s == "critical")
        warn = sum(1 for s, _ in findings if s == "warn")
        score = max(0, 100 - high * 25 - warn * 10)
        html = [
            "<h3>SecurityAccess audit (observational)</h3>",
            "<p>Seeds %d · critical %d · warn %d · score "
            "<b style='font-size:16pt'>%d</b>/100</p><hr>" % (
                len(seeds), high, warn, score),
        ]
        for sev, text in sorted(findings, key=lambda f: sev_order.get(f[0], 9)):
            html.append(
                "<p><b style='color:%s'>[%s]</b> %s</p>" % (
                    sev_color[sev], sev.upper(), text))
        html.append(
            "<p><i>No keys were sent. This report is observational only.</i></p>")
        finding_view.setHtml("\n".join(html))
        _plog("Analyze: critical %d warn %d score %d" % (high, warn, score))
        tabs.setCurrentWidget(finding_tab)

    def _on_report_html():
        path, _ = QFileDialog.getSaveFileName(
            root, "Export HTML report", "uds_security_audit.html",
            "HTML (*.html)")
        if not path:
            return
        try:
            html = [
                "<html><head><meta charset='utf-8'>"
                "<title>UDS SecurityAccess Audit</title></head><body>",
                "<h1>UDS SecurityAccess audit (observational)</h1>",
                "<p><b>No keys were sent. Audit only.</b></p>",
                "<p>Generated: %s</p>" % time.strftime("%Y-%m-%d %H:%M:%S"),
                "<p>TX 0x%X / RX 0x%X · samples %d</p>" % (
                    session.tx_id, session.rx_id, len(seeds)),
            ]
            st = session_timing[0]
            if st is not None and st >= 0:
                html.append("<p>Session timing: %.0f ms</p>" % st)
            html.append("<h2>Seeds (%d)</h2><table border='1' cellpadding='4'>" % len(seeds))
            html.append("<tr><th>#</th><th>Time</th><th>Seed</th></tr>")
            for i, (ts, s) in enumerate(seeds):
                html.append(
                    "<tr><td>%d</td><td>%s</td><td>%s</td></tr>" % (
                        i + 1, time.strftime("%H:%M:%S", time.localtime(ts)),
                        _hex(s)))
            html.append("</table>")
            html.append("<h2>NRC histogram</h2><ul>")
            for key, cnt in sorted(nrc_counts.items(), key=lambda x: str(x[0])):
                if isinstance(key, int) and key > 0:
                    html.append(
                        "<li>0x%02X (%s): %d</li>" % (
                            key, NRC_TEXTS.get(key, "?"), cnt))
                else:
                    html.append("<li>%s: %d</li>" % (key, cnt))
            html.append("</ul><h2>Findings</h2><ul>")
            for sev, text in findings:
                html.append("<li><b>[%s]</b> %s</li>" % (sev, text))
            html.append("</ul></body></html>")
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(html))
            QMessageBox.information(root, "Export OK", "Wrote:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(root, "Export failed", str(e))

    def _on_report_csv():
        rows = []
        for i, (ts, s) in enumerate(seeds):
            rows.append([
                i + 1, time.strftime("%H:%M:%S", time.localtime(ts)),
                _hex(s), "seed", "",
            ])
        for key, cnt in sorted(nrc_counts.items(), key=lambda x: str(x[0])):
            label = ("0x%02X" % key) if isinstance(key, int) and key > 0 else str(key)
            meaning = NRC_TEXTS.get(key, "") if isinstance(key, int) else ""
            rows.append(["", "", label, "nrc", "%d;%s" % (cnt, meaning)])
        for sev, text in findings:
            rows.append(["", "", "", sev, text])
        path = plugin_shell.export_csv(
            root,
            ["#", "Time", "Value", "Kind", "Detail"],
            rows,
            "uds_security_audit.csv",
        )
        if path:
            plugin_shell.set_status(parent.window(), "Exported %s" % path, 4000)

    def _on_clear():
        seeds.clear()
        nrc_counts.clear()
        findings.clear()
        session_timing[0] = None
        seed_tree.clear()
        nrc_tree.clear()
        finding_view.clear()
        progress.setValue(0)
        timing_label.setText("Session timing (10 03 → first 27 01): -")

    collect_btn.clicked.connect(_on_collect)
    stop_btn.clicked.connect(_on_stop)
    analyze_btn.clicked.connect(_analyze)
    report_html_btn.clicked.connect(_on_report_html)
    report_csv_btn.clicked.connect(_on_report_csv)
    clear_btn.clicked.connect(_on_clear)

    saved = state_store.load_state(PLUGIN_ID, "security.json") or {}
    if saved.get("sec_count"):
        count_spin.setValue(int(saved["sec_count"]))
    if saved.get("sec_interval_ms"):
        interval_spin.setValue(int(saved["sec_interval_ms"]))

    return root
