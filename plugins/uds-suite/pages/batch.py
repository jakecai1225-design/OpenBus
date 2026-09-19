# -*- coding: utf-8 -*-
"""Batch workspace — CSV/JSON sequence runner on SharedSession stack."""

from __future__ import annotations

import csv
import json
import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store

PLUGIN_ID = "uds-suite"

NRC_TEXTS = {
    0x10: "generalReject", 0x11: "serviceNotSupported",
    0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLengthOrInvalidFormat",
    0x14: "responseTooLong", 0x21: "busyRepeatRequest",
    0x22: "conditionsNotCorrect", 0x24: "requestSequenceError",
    0x31: "requestOutOfRange", 0x33: "securityAccessDenied",
    0x35: "invalidKey", 0x36: "exceededNumberOfAttempts",
    0x37: "requiredTimeDelayNotExpired",
    0x70: "uploadDownloadNotAccepted", 0x71: "transferDataSuspended",
    0x72: "generalProgrammingFailure", 0x73: "wrongBlockSequenceCounter",
    0x78: "requestCorrectlyReceived-ResponsePending",
    0x7E: "subFunctionNotSupportedInActiveSession",
    0x7F: "serviceNotSupportedInActiveSession",
}

DEFAULT_ROWS = [
    {"name": "Extended session", "req": "1003", "expect": "50",
     "timeout": "2000", "delay": "300"},
    {"name": "Read VIN", "req": "22F190", "expect": "62F190",
     "timeout": "2000", "delay": "300"},
    {"name": "Read DTC", "req": "19FF04", "expect": "59",
     "timeout": "2000", "delay": "300"},
]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _parse_hex(text):
    text = (text or "").strip().replace(" ", "").replace("0x", "").replace("0X", "")
    if not text:
        return b""
    try:
        if len(text) % 2:
            text = "0" + text
        return bytes.fromhex(text)
    except ValueError:
        return None


class _BatchRunner:
    """Sequential runner using SharedSession.request / client."""

    def __init__(self, session, rows, on_step, on_done):
        self.session = session
        self.rows = rows
        self.on_step = on_step
        self.on_done = on_done
        self.idx = 0
        self.results = []
        self._alive = True
        self._timer = QTimer()
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._on_timeout)
        self._pending_expect = b""
        self._started = 0.0
        self._pendings = 0
        self._timeout_ms = 2000

    def start(self):
        self.idx = 0
        self.results = []
        self._alive = True
        self._run_step()

    def stop(self):
        self._alive = False
        self._timer.stop()
        try:
            self.session.client.cancel()
        except Exception:
            pass

    def _run_step(self):
        if not self._alive or self.idx >= len(self.rows):
            self.on_done(self.results)
            return
        row = self.rows[self.idx]
        req = _parse_hex(row.get("req", ""))
        if req is None or not req:
            self.results.append((row, "FAIL", "Invalid request hex"))
            self._finish_step(row)
            return
        self._pending_expect = _parse_hex(row.get("expect", "")) or b""
        try:
            timeout_ms = int(float(row.get("timeout", "2000") or 2000))
        except ValueError:
            timeout_ms = 2000
        self._timeout_ms = max(200, timeout_ms)
        self._started = time.time()
        self._pendings = 0

        def on_done(ok, resp, note):
            if not self._alive:
                return
            self._timer.stop()
            elapsed = (time.time() - self._started) * 1000.0
            if not ok or resp is None:
                verdict = "TIMEOUT" if resp is None and "timeout" in (note or "").lower() else "FAIL"
                detail = note or "No response"
                if resp and len(resp) >= 3 and resp[0] == 0x7F:
                    nrc = resp[2]
                    detail = "NRC %02X (%s)" % (nrc, NRC_TEXTS.get(nrc, "?"))
                    verdict = "FAIL"
                self.results.append((row, verdict, detail))
            else:
                if self._pending_expect and resp[:len(self._pending_expect)] != self._pending_expect:
                    detail = _hex(resp)
                    if resp[0] == 0x7F and len(resp) >= 3:
                        nrc = resp[2]
                        detail = "NRC %02X (%s)" % (nrc, NRC_TEXTS.get(nrc, "?"))
                    self.results.append((row, "FAIL", detail))
                else:
                    self.results.append((row, "PASS", _hex(resp)))
            self.on_step(self.idx, self.results[-1], elapsed)
            self._finish_step(row)

        # Hook pending 0x78 via client path — UdsClient already extends P2*
        old_p2 = self.session.client.p2_ms
        self.session.client.p2_ms = self._timeout_ms
        self.session.client.p2star_ms = max(self._timeout_ms, 5000)

        def restore_and(cb):
            def wrapped(ok, resp, note):
                self.session.client.p2_ms = old_p2
                cb(ok, resp, note)
            return wrapped

        self.session.request(
            req, on_done=restore_and(on_done),
            expect_response=True, tag=row.get("name") or "batch")
        self._timer.start(self._timeout_ms + 500)

    def _on_timeout(self):
        if not self._alive:
            return
        # UdsClient may still fire; if still busy, cancel
        if self.session.client.busy:
            self.session.client.cancel()
        row = self.rows[self.idx]
        if len(self.results) <= self.idx:
            self.results.append(
                (row, "TIMEOUT", "No response (%d ms)" % self._timeout_ms))
            self.on_step(self.idx, self.results[-1], self._timeout_ms)
            self._finish_step(row)

    def _finish_step(self, row):
        try:
            delay_ms = int(float(row.get("delay", "0") or 0))
        except (ValueError, AttributeError):
            delay_ms = 0
        delay_ms = max(0, min(delay_ms, 10000))
        self.idx += 1
        QTimer.singleShot(delay_ms, self._run_step)


def _load_rows_from_path(path):
    lower = path.lower()
    if lower.endswith(".json"):
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict) and "steps" in data:
            data = data["steps"]
        if not isinstance(data, list):
            raise ValueError("JSON must be a list of steps or {steps: [...]}")
        rows = []
        for item in data:
            rows.append({
                "name": str(item.get("name", item.get("tag", ""))),
                "req": str(item.get("req", item.get("request", item.get("pdu_hex", "")))),
                "expect": str(item.get("expect", item.get("expected", ""))),
                "timeout": str(item.get("timeout", "2000")),
                "delay": str(item.get("delay", "300")),
            })
        return rows
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        rows = []
        for row in reader:
            rows.append({
                "name": row.get("name", row.get("tag", "")),
                "req": row.get("req", row.get("request", row.get("pdu_hex", ""))),
                "expect": row.get("expect", row.get("expected", "")),
                "timeout": row.get("timeout", "2000"),
                "delay": row.get("delay", "300"),
            })
        return rows


def build(parent, session, log_fn) -> QWidget:
    from _shared import vscode_theme, codicons

    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(14)

    rows = [dict(r) for r in DEFAULT_ROWS]
    last_batch_path = [""]
    runner_ref = [None]

    seq_card, seq_body = vscode_theme.block(
        "Sequence",
        "CSV columns: name, req, expect, timeout, delay. Run uses the shared TX / RX.",
    )

    btns = QHBoxLayout()
    btns.setSpacing(8)
    import_btn = QPushButton("Import")
    add_btn = QPushButton("Add")
    edit_btn = QPushButton("Edit")
    del_btn = QPushButton("Delete")
    run_btn = QPushButton("Run")
    run_btn.setObjectName("PrimaryButton")
    stop_btn = QPushButton("Stop")
    export_btn = QPushButton("Export")
    for w, ic, primary in (
        (import_btn, "import", False),
        (add_btn, "add", False),
        (edit_btn, "edit", False),
        (del_btn, "delete", False),
        (run_btn, "start", True),
        (stop_btn, "stop", False),
        (export_btn, "export", False),
    ):
        w.setFixedHeight(28)
        codicons.set_button(w, ic, primary=primary)
    for w in (import_btn, add_btn, edit_btn, del_btn):
        btns.addWidget(w)
    btns.addStretch(1)
    for w in (run_btn, stop_btn, export_btn):
        btns.addWidget(w)
    seq_body.addLayout(btns)
    layout.addWidget(seq_card)
    stop_btn.setEnabled(False)

    tree = QTreeWidget()
    tree.setHeaderLabels([
        "#", "Step", "Request", "Expect", "Timeout ms", "Delay ms",
        "Result", "Response / detail",
    ])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSelectionBehavior(tree.SelectionBehavior.SelectRows)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    result_card, result_body = vscode_theme.block(
        "Steps",
        "One row per request. Result is PASS, FAIL, or NRC.",
    )
    result_body.addWidget(tree, 1)
    summary = QLabel("Idle")
    summary.setObjectName("SuiteHint")
    result_body.addWidget(summary)
    layout.addWidget(result_card, 1)

    def _plog(text):
        log_fn("RX", "-", b"", "[Batch] %s" % text)

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "last_batch_path": last_batch_path[0],
        }, "batch.json")

    def _refresh():
        tree.clear()
        for i, r in enumerate(rows):
            res = r.get("_result", "")
            detail = r.get("_detail", "")
            item = QTreeWidgetItem([
                str(i + 1), r.get("name", ""), r.get("req", ""),
                r.get("expect", ""), r.get("timeout", ""), r.get("delay", ""),
                res, detail,
            ])
            if res == "PASS":
                item.setBackground(6, QColor("#2e7d32"))
                item.setForeground(6, QColor("white"))
            elif res in ("FAIL", "TIMEOUT"):
                item.setBackground(6, QColor("#c62828"))
                item.setForeground(6, QColor("white"))
            tree.addTopLevelItem(item)

    def _on_step(idx, result, elapsed):
        row, verdict, detail = result
        row["_result"] = verdict
        row["_detail"] = detail
        _plog("Step %d [%s] %s → %s (%.0f ms)" % (
            idx + 1, verdict, row.get("name"), detail, elapsed))
        _refresh()

    def _on_done(results):
        runner_ref[0] = None
        n_pass = sum(1 for _, v, _ in results if v == "PASS")
        n_fail = sum(1 for _, v, _ in results if v == "FAIL")
        n_to = sum(1 for _, v, _ in results if v == "TIMEOUT")
        total = len(results)
        rate = (100.0 * n_pass / total) if total else 0.0
        summary.setText(
            "Done: %d/%d pass (%.1f%%) — fail %d, timeout %d" % (
                n_pass, total, rate, n_fail, n_to))
        _plog("Batch finished: pass rate %.1f%%" % rate)
        run_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        plugin_shell.set_status(
            parent.window(),
            "Pass %d/%d (%.1f%%)" % (n_pass, total, rate), 6000)

    def _on_run():
        if not rows:
            QMessageBox.information(root, "Empty", "No steps to run.")
            return
        for r in rows:
            r.pop("_result", None)
            r.pop("_detail", None)
        _refresh()
        _persist()
        runner_ref[0] = _BatchRunner(session, rows, _on_step, _on_done)
        run_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        summary.setText("Running (%d steps)..." % len(rows))
        _plog("Start batch (%d steps, TX 0x%X → RX 0x%X)" % (
            len(rows), session.tx_id, session.rx_id))
        runner_ref[0].start()

    def _on_stop():
        if runner_ref[0]:
            runner_ref[0].stop()
            runner_ref[0] = None
        run_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        summary.setText("Stopped")
        _plog("Batch stopped by user")

    def _on_import():
        path, _ = QFileDialog.getOpenFileName(
            root, "Import batch table",
            last_batch_path[0] or "uds_batch.csv",
            "Batch (*.csv *.json);;CSV (*.csv);;JSON (*.json);;All (*)")
        if not path:
            return
        try:
            new_rows = _load_rows_from_path(path)
            if not new_rows:
                QMessageBox.warning(root, "Import failed", "No data rows found")
                return
            rows.clear()
            rows.extend(new_rows)
            last_batch_path[0] = path
            _persist()
            _refresh()
            QMessageBox.information(
                root, "Import OK", "Loaded %d steps from:\n%s" % (len(rows), path))
        except (OSError, csv.Error, json.JSONDecodeError, ValueError) as e:
            QMessageBox.warning(root, "Import failed", str(e))

    def _edit_row_dialog(values, title, is_new=False):
        dialog = QWidget()
        dialog.setWindowTitle(title)
        dialog.setWindowModality(Qt.WindowModality.ApplicationModal)
        dl = QFormLayout(dialog)
        edits = {}
        for key, label in [
            ("name", "Step name:"), ("req", "Request hex:"),
            ("expect", "Expect prefix:"), ("timeout", "Timeout ms:"),
            ("delay", "Delay ms:"),
        ]:
            e = QLineEdit(str(values.get(key, "")))
            edits[key] = e
            dl.addRow(label, e)
        ok_btn = QPushButton("OK")
        cancel_btn = QPushButton("Cancel")
        bl = QHBoxLayout()
        bl.addStretch(1)
        bl.addWidget(ok_btn)
        bl.addWidget(cancel_btn)
        dl.addRow(bl)

        def _ok():
            values.update({k: e.text().strip() for k, e in edits.items()})
            if _parse_hex(values.get("req", "")) is None:
                QMessageBox.warning(dialog, "Format error", "Invalid request hex")
                return
            dialog.close()
            if is_new:
                rows.append(values)
            _refresh()

        ok_btn.clicked.connect(_ok)
        cancel_btn.clicked.connect(dialog.close)
        dialog.resize(360, 220)
        dialog.show()

    def _on_add():
        _edit_row_dialog(
            {"name": "", "req": "", "expect": "", "timeout": "2000", "delay": "300"},
            "Add row", is_new=True)

    def _on_edit():
        sel = tree.selectedItems()
        if not sel:
            QMessageBox.information(root, "Hint", "Select a row first")
            return
        idx = int(sel[0].text(0)) - 1
        if 0 <= idx < len(rows):
            _edit_row_dialog(rows[idx], "Edit row", is_new=False)

    def _on_del():
        sel = tree.selectedItems()
        if not sel:
            return
        idx = int(sel[0].text(0)) - 1
        if 0 <= idx < len(rows):
            rows.pop(idx)
            _refresh()

    def _on_export():
        export_rows = []
        for i, r in enumerate(rows):
            export_rows.append([
                i + 1, r.get("name", ""), r.get("req", ""), r.get("expect", ""),
                r.get("_result", ""), r.get("_detail", ""),
            ])
        path = plugin_shell.export_csv(
            root,
            ["#", "Step", "Request", "Expect", "Result", "Detail"],
            export_rows,
            "uds_batch_report.csv",
        )
        if path:
            plugin_shell.set_status(parent.window(), "Exported %s" % path, 4000)

    import_btn.clicked.connect(_on_import)
    add_btn.clicked.connect(_on_add)
    edit_btn.clicked.connect(_on_edit)
    del_btn.clicked.connect(_on_del)
    run_btn.clicked.connect(_on_run)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)

    saved = state_store.load_state(PLUGIN_ID, "batch.json") or {}
    if saved.get("last_batch_path"):
        last_batch_path[0] = str(saved["last_batch_path"])
        try:
            loaded = _load_rows_from_path(last_batch_path[0])
            if loaded:
                rows.clear()
                rows.extend(loaded)
                summary.setText("Restored batch: %s" % last_batch_path[0])
        except (OSError, csv.Error, json.JSONDecodeError, ValueError):
            pass

    _refresh()
    return root
