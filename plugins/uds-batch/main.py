# -*- coding: utf-8 -*-
"""uds-batch 插件 — UDS 批量测试（ISO 14229）
功能：
- CSV 请求表导入/编辑（步骤名/请求 Hex/期望响应/超时/延时）
- 顺序执行（每步独立超时与期望校验，支持 7F xx 延续等待）
- 逐行结果（PASS/FAIL/超时）+ 通过率统计
- 报告导出 CSV；内置 ISO-TP 客户端（SF/FF/CF/FC 全状态机）
- 仅用户点击「开始执行」才发帧（安全基线）
依赖: pip install PyQt6
"""

import csv
import time

from PyQt6.QtCore import QTimer

import sin
from isotp_client import IsotpClient

NRC_TEXTS = {
    0x10: "generalReject", 0x11: "serviceNotSupported", 0x12: "subFunctionNotSupported",
    0x13: "incorrectMessageLengthOrInvalidFormat", 0x14: "responseTooLong",
    0x21: "busyRepeatRequest", 0x22: "conditionsNotCorrect", 0x24: "requestSequenceError",
    0x31: "requestOutOfRange", 0x33: "securityAccessDenied", 0x35: "invalidKey",
    0x36: "exceededNumberOfAttempts", 0x37: "requiredTimeDelayNotExpired",
    0x70: "uploadDownloadNotAccepted", 0x71: "transferDataSuspended",
    0x72: "generalProgrammingFailure", 0x73: "wrongBlockSequenceCounter",
    0x78: "requestCorrectlyReceived-ResponsePending",
    0x7E: "subFunctionNotSupportedInActiveSession",
    0x7F: "serviceNotSupportedInActiveSession",
}

DEFAULT_ROWS = [
    {"name": "进扩展会话", "req": "1003", "expect": "50", "timeout": "2000", "delay": "300"},
    {"name": "读 VIN", "req": "22F190", "expect": "62F190", "timeout": "2000", "delay": "300"},
    {"name": "读 DTC", "req": "19FF04", "expect": "59", "timeout": "2000", "delay": "300"},
]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _parse_hex(text):
    text = text.strip().replace(" ", "").replace("0x", "").replace("0X", "")
    if not text:
        return b""
    try:
        if len(text) % 2:
            text = "0" + text
        return bytes.fromhex(text)
    except ValueError:
        return None


class _BatchRunner:
    """顺序执行器：单事务（发→等响应→判定→延时→下一步）"""

    def __init__(self, client, rows, on_step, on_done):
        self.client = client
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
        self._started = 0
        self._pendings = 0

    def start(self):
        self.idx = 0
        self.results = []
        self._alive = True
        self._run_step()

    def stop(self):
        self._alive = False
        self._timer.stop()

    def _run_step(self):
        if not self._alive or self.idx >= len(self.rows):
            self.on_done(self.results)
            return
        row = self.rows[self.idx]
        req = _parse_hex(row.get("req", ""))
        if req is None or not req:
            self.results.append((row, "FAIL", "请求 Hex 非法"))
            self._finish_step(0)
            return
        self._pending_expect = _parse_hex(row.get("expect", "")) or b""
        try:
            timeout_ms = int(float(row.get("timeout", "2000") or 2000))
        except ValueError:
            timeout_ms = 2000
        self._timeout_ms = max(200, timeout_ms)
        self._started = time.time()
        self._pendings = 0
        self.client.on_received = self._on_pdu
        self.client.send(req)
        self._timer.start(self._timeout_ms)

    def _on_pdu(self, pdu):
        if not self._alive:
            return
        # 0x7F xx 78 → 续等（重新计时，最多 30 次防死循环）
        if len(pdu) >= 3 and pdu[0] == 0x7F and pdu[2] == 0x78:
            self._pendings += 1
            if self._pendings <= 30:
                self._timer.start(self._timeout_ms)
                return
        self._timer.stop()
        row = self.rows[self.idx]
        elapsed = (time.time() - self._started) * 1000.0
        if self._pending_expect and pdu[:len(self._pending_expect)] != self._pending_expect:
            detail = _hex(pdu)
            if pdu[0] == 0x7F and len(pdu) >= 3:
                nrc = pdu[2] if len(pdu) >= 3 else pdu[1]
                detail = "NRC %02X (%s)" % (nrc, NRC_TEXTS.get(nrc, "?"))
            self.results.append((row, "FAIL", detail))
        else:
            self.results.append((row, "PASS", _hex(pdu)))
        self.on_step(self.idx, self.results[-1], elapsed)
        self._finish_step(row)

    def _on_timeout(self):
        if not self._alive:
            return
        row = self.rows[self.idx]
        self.results.append((row, "超时", "无响应 (%d ms)" % self._timeout_ms))
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


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QGroupBox, QFormLayout, QLineEdit,
            QSpinBox, QTabWidget, QInputDialog
        )
    except ImportError:
        sin.output.append("UDS 批量测试插件需要 PyQt6: pip install PyQt6")
        return

    rows = [dict(r) for r in DEFAULT_ROWS]
    runner = {"obj": None}
    client = {"obj": None}

    win = sin.ui.create_window("UDS 批量测试 (ISO 14229)")
    win.resize(960, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("连接配置")
    cfg_l = QFormLayout(cfg)
    tx_edit = QLineEdit("0x7E0")
    rx_edit = QLineEdit("0x7E8")
    cfg_l.addRow("请求 ID:", tx_edit)
    cfg_l.addRow("响应 ID:", rx_edit)
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    import_btn = QPushButton("导入 CSV")
    add_btn = QPushButton("添加行")
    edit_btn = QPushButton("编辑选中行")
    del_btn = QPushButton("删除选中行")
    run_btn = QPushButton("开始执行")
    stop_btn = QPushButton("停止")
    export_btn = QPushButton("导出报告")
    btns.addWidget(import_btn)
    btns.addWidget(add_btn)
    btns.addWidget(edit_btn)
    btns.addWidget(del_btn)
    btns.addStretch(1)
    btns.addWidget(run_btn)
    btns.addWidget(stop_btn)
    btns.addWidget(export_btn)
    layout.addLayout(btns)

    tree = QTreeWidget()
    tree.setHeaderLabels(["#", "步骤名", "请求", "期望", "超时ms", "延时ms", "结果", "响应/详情"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setSelectionBehavior(tree.SelectionBehavior.SelectRows)
    header = tree.header()
    header.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    summary = QLabel("就绪（点击「开始执行」后将按表顺序发送请求）")
    summary.setStyleSheet("font-weight: bold;")
    layout.addWidget(summary)

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    layout.addWidget(log_view, 1)

    def _log(text):
        log_view.append("[%s] %s" % (time.strftime("%H:%M:%S"), text))

    def _refresh():
        tree.clear()
        for i, r in enumerate(rows):
            res = r.get("_result", "")
            detail = r.get("_detail", "")
            item = QTreeWidgetItem([
                str(i + 1), r.get("name", ""), r.get("req", ""), r.get("expect", ""),
                r.get("timeout", ""), r.get("delay", ""), res, detail])
            if res == "PASS":
                item.setBackground(6, __import__("PyQt6.QtGui", fromlist=["QColor"]).QColor("#2e7d32"))
                item.setForeground(6, __import__("PyQt6.QtGui", fromlist=["QColor"]).QColor("white"))
            elif res in ("FAIL", "超时"):
                item.setBackground(6, __import__("PyQt6.QtGui", fromlist=["QColor"]).QColor("#c62828"))
                item.setForeground(6, __import__("PyQt6.QtGui", fromlist=["QColor"]).QColor("white"))
            tree.addTopLevelItem(item)

    def _ensure_client():
        if client["obj"] is not None:
            return client["obj"]
        try:
            tx = int(tx_edit.text(), 0)
            rx = int(rx_edit.text(), 0)
        except ValueError:
            QMessageBox.warning(win, "配置错误", "请求/响应 ID 需为十六进制（如 0x7E0）")
            return None
        c = IsotpClient(sin.frames.send, win)
        c.tx_id = tx
        c.rx_id = rx
        c.on_error = lambda msg: _log("ISO-TP 错误: %s" % msg)
        client["obj"] = c
        return c

    def _on_frame(frame):
        c = client["obj"]
        if c is not None:
            c.on_frame(frame.id, frame.data)

    context.on_frame(_on_frame)

    def _on_step(idx, result, elapsed):
        row, verdict, detail = result
        row["_result"] = verdict
        row["_detail"] = detail
        _log("步骤 %d [%s] %s → %s（%.0f ms）" % (idx + 1, verdict, row.get("name"), detail, elapsed))
        _refresh()

    def _on_done(results):
        n_pass = sum(1 for _, v, _ in results if v == "PASS")
        total = len(results)
        rate = (100.0 * n_pass / total) if total else 0.0
        summary.setText("执行完成: %d/%d 通过（%.1f%%）" % (n_pass, total, rate))
        _log("批量测试完成: %d/%d 通过率 %.1f%%" % (n_pass, total, rate))
        run_btn.setEnabled(True)
        stop_btn.setEnabled(False)

    def _on_run():
        c = _ensure_client()
        if c is None:
            return
        for r in rows:
            r.pop("_result", None)
            r.pop("_detail", None)
        _refresh()
        r = _BatchRunner(c, rows, _on_step, _on_done)
        runner["obj"] = r
        run_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        summary.setText("执行中（%d 步）..." % len(rows))
        _log("开始批量执行（%d 步，请求 %s → 响应 %s）"
             % (len(rows), tx_edit.text(), rx_edit.text()))
        r.start()

    def _on_stop():
        if runner["obj"]:
            runner["obj"].stop()
        run_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        summary.setText("已停止")
        _log("用户停止批量执行")

    def _on_import():
        path, _ = QFileDialog.getOpenFileName(win, "导入请求表 CSV", "uds_batch.csv",
                                              "CSV 文件 (*.csv);;所有文件 (*)")
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8-sig", newline="") as f:
                reader = csv.DictReader(f)
                new_rows = []
                for row in reader:
                    new_rows.append({
                        "name": row.get("name", ""),
                        "req": row.get("req", ""),
                        "expect": row.get("expect", ""),
                        "timeout": row.get("timeout", "2000"),
                        "delay": row.get("delay", "300"),
                    })
            if not new_rows:
                QMessageBox.warning(win, "导入失败", "CSV 无数据行")
                return
            rows.clear()
            rows.extend(new_rows)
            _refresh()
            QMessageBox.information(win, "导入成功", "已导入 %d 步" % len(rows))
        except (OSError, csv.Error) as e:
            QMessageBox.warning(win, "导入失败", str(e))

    def _on_add():
        values = {"name": "", "req": "", "expect": "", "timeout": "2000", "delay": "300"}
        _edit_row_dialog(values, "添加行")

    def _edit_row_dialog(values, title):
        dialog = QWidget()
        dialog.setWindowTitle(title)
        dialog.setWindowModality(__import__("PyQt6.QtCore", fromlist=["Qt"]).Qt.WindowModality.ApplicationModal)
        dl = QFormLayout(dialog)
        edits = {}
        for key, label in [("name", "步骤名:"), ("req", "请求 Hex:"), ("expect", "期望响应 Hex:"),
                           ("timeout", "超时 ms:"), ("delay", "延时 ms:")]:
            e = QLineEdit(str(values.get(key, "")))
            edits[key] = e
            dl.addRow(label, e)
        ok_btn = QPushButton("确定")
        cancel_btn = QPushButton("取消")
        bl = QHBoxLayout()
        bl.addStretch(1)
        bl.addWidget(ok_btn)
        bl.addWidget(cancel_btn)
        dl.addRow(bl)

        def _ok():
            values.update({k: e.text().strip() for k, e in edits.items()})
            if _parse_hex(values.get("req", "")) is None:
                QMessageBox.warning(dialog, "格式错误", "请求 Hex 非法")
                return
            dialog.close()
            if title == "添加行":
                rows.append(values)
            _refresh()

        ok_btn.clicked.connect(_ok)
        cancel_btn.clicked.connect(dialog.close)
        dialog.resize(320, 200)
        dialog.show()

    def _on_edit():
        sel = tree.selectedItems()
        if not sel:
            QMessageBox.information(win, "提示", "请先选中一行")
            return
        idx = int(sel[0].text(0)) - 1
        if 0 <= idx < len(rows):
            values = dict(rows[idx])
            _edit_row_dialog(values, "编辑行")

    def _on_del():
        sel = tree.selectedItems()
        if not sel:
            return
        idx = int(sel[0].text(0)) - 1
        if 0 <= idx < len(rows):
            rows.pop(idx)
            _refresh()

    def _on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出报告 CSV", "uds_batch_report.csv",
                                              "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = csv.writer(f)
                w.writerow(["#", "步骤名", "请求", "期望", "结果", "响应/详情"])
                for i, r in enumerate(rows):
                    w.writerow([i + 1, r.get("name"), r.get("req"), r.get("expect"),
                                r.get("_result", ""), r.get("_detail", "")])
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("udsBatch.open", _on_open_cmd, "诊断: UDS 批量测试")

    import_btn.clicked.connect(_on_import)
    add_btn.clicked.connect(_on_add)
    edit_btn.clicked.connect(_on_edit)
    del_btn.clicked.connect(_on_del)
    run_btn.clicked.connect(_on_run)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)
    stop_btn.setEnabled(False)

    _refresh()
    win.show()
    sin.output.append("UDS 批量测试插件已加载（内置 ISO-TP，激活期间零发送）")


def deactivate():
    sin.output.append("UDS 批量测试插件已停用")
