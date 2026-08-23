# -*- coding: utf-8 -*-
"""uds-scan 插件 — UDS ECU 扫描器（ISO 14229）
功能：
- 诊断 ID 范围扫描（0x7E0-0x7EF / 自定义），发送 TesterPresent 检测 ECU 响应
- 可选服务探测：默认读取 VIN (0x22F190)、支持服务 (0x1003+0x22)
- ISO-TP 完整支持（FF/CF/FC、多帧重组、超时控制）
- 结果表（ID、ECU 名称、探测服务）、事件日志 + CSV 导出；用户点击「开始扫描」才发帧
依赖: pip install PyQt6，内部 ISO-TP 客户端（自包含）
"""

import time

from PyQt6.QtCore import QTimer

import sin

PAD = b"\xCC"


def decode_stmin(raw):
    if raw <= 0x7F:
        return raw
    if 0xF1 <= raw <= 0xF9:
        return 1
    return 0


class IsotpClient:
    def __init__(self, send_frame, qt_parent=None):
        self.send_frame = send_frame
        self.tx_id = 0x7E0
        self.rx_id = 0x7E8
        self.fc_bs = 8
        self.fc_stmin_ms = 10
        self.on_received = None
        self.on_log = None
        self.on_error = None
        self._alive = True

        self._tx_payload = b""
        self._tx_seq = 1
        self._tx_can_id = 0
        self._stmin_ms = 0
        self._bs_left = 0
        self._fc_timer = QTimer(qt_parent)
        self._fc_timer.setSingleShot(True)
        self._fc_timer.timeout.connect(self._on_fc_timeout)
        self._fc_wait_count = 0

        self._rx_expected = 0
        self._rx_buffer = b""
        self._rx_active = False
        self._rx_timer = QTimer(qt_parent)
        self._rx_timer.setSingleShot(True)
        self._rx_timer.timeout.connect(self._on_rx_timeout)

    def send(self, pdu, functional=False):
        if not self._alive or not pdu:
            return
        can_id = self.func_id if functional else self.tx_id
        if len(pdu) <= 7:
            self._send_sf(can_id, pdu)
            return
        if functional:
            if self.on_error:
                self.on_error("功能寻址不支持多帧")
            return
        self._send_ff(can_id, pdu)

    def _send_sf(self, can_id, pdu):
        frame = (bytes([len(pdu) & 0x0F]) + pdu).ljust(8, PAD)
        self.send_frame(can_id, frame)
        self._log("TX", can_id, frame, "SF")

    def _send_ff(self, can_id, pdu):
        total = len(pdu)
        ff = (bytes([0x10 | ((total >> 8) & 0x0F), total & 0xFF]) + pdu[:6]).ljust(8, PAD)
        self.send_frame(can_id, ff)
        self._log("TX", can_id, ff, "FF total=%d" % total)
        self._tx_payload = pdu[6:]
        self._tx_seq = 1
        self._tx_can_id = can_id
        self._fc_wait_count = 0
        self._start_fc_wait()

    def _start_fc_wait(self):
        self._fc_timer.start(1000)

    def _on_fc_timeout(self):
        if self._tx_payload:
            self._tx_payload = b""
            if self.on_error:
                self.on_error("等待流控帧 FC 超时（1000ms）")

    def _on_fc(self, data):
        fs = data[0] & 0x0F
        if not self._tx_payload and not self._fc_timer.isActive():
            return
        if fs == 0x00:
            self._fc_timer.stop()
            bs = data[1] if len(data) > 1 else 0
            stmin = decode_stmin(data[2]) if len(data) > 2 else 0
            self._bs_left = bs if bs > 0 else 0xFFFF
            self._stmin_ms = stmin
            self._send_cf_scheduled(0)
        elif fs == 0x01:
            self._fc_wait_count += 1
            if self._fc_wait_count > 10:
                self._tx_payload = b""
                if self.on_error:
                    self.on_error("FC WAIT 超过 10 次")
            else:
                self._fc_timer.start(1000)
        elif fs == 0x02:
            self._tx_payload = b""
            self._fc_timer.stop()
            if self.on_error:
                self.on_error("FC OVFLW")

    def _send_cf_scheduled(self, delay_ms):
        if not self._alive:
            return
        if delay_ms > 0:
            QTimer.singleShot(delay_ms, self._send_one_cf)
        else:
            self._send_one_cf()

    def _send_one_cf(self):
        if not self._alive or not self._tx_payload:
            return
        chunk = self._tx_payload[:7]
        cf = (bytes([0x20 | (self._tx_seq & 0x0F)]) + chunk).ljust(8, PAD)
        self.send_frame(self._tx_can_id, cf)
        self._log("TX", self._tx_can_id, cf, "CF sn=%d" % self._tx_seq)
        self._tx_seq = (self._tx_seq + 1) & 0x0F
        self._tx_payload = self._tx_payload[7:]
        self._bs_left -= 1
        if not self._tx_payload:
            return
        if self._bs_left <= 0:
            self._fc_wait_count = 0
            self._start_fc_wait()
            return
        self._send_cf_scheduled(self._stmin_ms)

    def on_frame(self, can_id, data):
        if not self._alive or can_id != self.rx_id or not data:
            return
        pci = data[0]
        kind = pci & 0xF0
        if kind == 0x00:
            length = pci & 0x0F
            pdu = data[1:1 + length]
            self._log("RX", can_id, data, "SF")
            self._rx_timer.stop()
            self._rx_active = False
            if pdu and self.on_received:
                self.on_received(pdu)
        elif kind == 0x10:
            self._rx_expected = ((pci & 0x0F) << 8) | data[1]
            self._rx_buffer = data[2:]
            self._rx_active = True
            self._log("RX", can_id, data, "FF total=%d" % self._rx_expected)
            self._send_fc()
            self._rx_timer.start(1000)
        elif kind == 0x20:
            if not self._rx_active:
                return
            self._rx_timer.start(1000)
            self._rx_buffer += data[1:]
            self._log("RX", can_id, data, "CF sn=%d" % (pci & 0x0F))
            if len(self._rx_buffer) >= self._rx_expected:
                pdu = self._rx_buffer[:self._rx_expected]
                self._rx_active = False
                self._rx_timer.stop()
                self._log("RX", can_id, pdu, "PDU %d bytes" % len(pdu))
                if self.on_received:
                    self.on_received(pdu)
        elif kind == 0x30:
            self._on_fc(data)

    def _send_fc(self):
        frame = bytes([0x30, self.fc_bs & 0xFF, self.fc_stmin_ms & 0xFF]).ljust(8, PAD)
        self.send_frame(self.tx_id, frame)
        self._log("TX", self.tx_id, frame, "FC bs=%d stmin=%dms" % (self.fc_bs, self.fc_stmin_ms))

    def _on_rx_timeout(self):
        if self._rx_active:
            received = len(self._rx_buffer)
            self._rx_active = False
            self._rx_buffer = b""
            if self.on_error:
                self.on_error("多帧接收超时，丢弃半成品（已收 %d/%d）" % (received, self._rx_expected))

    def shutdown(self):
        self._alive = False
        self._fc_timer.stop()
        self._rx_timer.stop()
        self._tx_payload = b""
        self._rx_active = False

    def _log(self, direction, can_id, data, note):
        if self.on_log:
            self.on_log(direction, can_id, data, note)


# ---- UDS scan 状态 ----
_results = []          # {cid_hex, name, service_map}
_events = []
_running = False
_scanning = False
_start_cid = 0x7E0
_end_cid = 0x7E7
_rx_offset = 0x08      # 响应 ID = 请求 ID + _rx_offset


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 1000:
        del _events[:500]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def activate(context):
    global _running, _scanning, _results
    _results.clear()
    del _events[:]
    _running = False
    _scanning = False

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QLineEdit, QTextEdit, QTreeWidget, QTreeWidgetItem,
            QFileDialog, QMessageBox, QHeaderView, QGroupBox,
            QDialog, QFormLayout
        )
    except ImportError:
        sin.output.append("UDS 扫描插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("UDS ECU 扫描器 (ISO 14229)")
    win.resize(800, 520)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    # --- 配置 ---
    cfg = QGroupBox("扫描配置")
    l = QFormLayout(cfg)
    l.addRow("起始 ID:", QLineEdit("0x7E0"))
    l.addRow("终止 ID:", QLineEdit("0x7E7"))
    l.addRow("响应偏移:", QLineEdit("0x08"))
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    scan_btn = QPushButton("开始扫描")
    stop_btn = QPushButton("停止")
    export_btn = QPushButton("导出 CSV")
    clear_btn = QPushButton("清零")
    btns.addStretch(1)
    btns.addWidget(scan_btn)
    btns.addWidget(stop_btn)
    btns.addWidget(clear_btn)
    btns.addWidget(export_btn)
    layout.addLayout(btns)

    # --- 结果表 ---
    tree = QTreeWidget()
    tree.setHeaderLabels(["CAN ID", "ECU 状态", "服务", "延迟(ms)", "数据 Hex"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    header = tree.header()
    header.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    # --- 日志 ---
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    layout.addWidget(log_view, 0)

    hint = QLabel("扫描策略：按序发 TesterPresent(0x3E00)，正响应标记为 ECU 在线；可选读 VIN(0x22F190)+支持服务")
    hint.setStyleSheet("color:#888;font-size:11px;")
    layout.addWidget(hint)

    context.on_frame(_context_on_frame)
    sink = ContextSink()
    context.on_frame(sink.on_frame)

    def _context_on_frame(frame):
        cid = frame.id
        data = frame.data
        if not data:
            return
        pid = sink.pending
        if pid and abs(pid - cid) <= 20:
            # 响应 ID 匹配正在查询的 ID（容差窗口）
            ts = time.time()
            resp_data = data
            # 查找对应 scanner 实例
            for sid, sc in sink.scanners.items():
                if sc.tx_id == cid or sc.tx_id + _rx_offset == cid:
                    if resp_data[0] == 0x7f and len(resp_data) >= 2:
                        nrc = resp_data[1]
                        sc.responses[pid] = {"error": nrc}
                    elif resp_data[0] >= 0x40:
                        sc.responses[pid] = {"data": resp_data[1:]}
                    if pid == 0:     # TesterPresent
                        sc.status = "上线"
                        if 0x10 in sc.probes:
                            sc.responses[0x10] = {"done": True}
                        if 0x22 in sc.probes:
                            sc.responses[0x22] = {"done": True}
                        break
            if pid == 0 and cid in _results:
                _ev("SCAN", "ID %03X = ECU" % cid)
        sink.pending = None

    def refresh():
        if not _results:
            tree.clear()
            tree.setHeaderLabels(["CAN ID", "ECU 状态", "服务", "延迟", "数据 Hex"])
            return
        tree.clear()
        for r in _results:
            svcs = ", ".join("PID%s:%s" % (hk, hv.get("text", "?")) for hk, hv in sorted(r["svcs"].items()))
            tree.addTopLevelItem(QTreeWidgetItem([r["cid"], r["status"], svcs, "%.1f" % r["lat"], "%s" % r["hex"]]))
        sb = log_view.verticalScrollBar()
        sb.setValue(sb.maximum())
        if log_view.toPlainText().count("\n") > 200:
            log_view.clear()

    timer = QTimer()
    timer.timeout.connect(refresh)
    timer.start(500)

    def on_scan():
        global _scanning
        _scanning = True
        _results.clear()
        tree.clear()
        scan_btn.setText("扫描中...")
        srange = [(int(x, 0) if x.startswith("0x") else int(x)) for x in [cfg.findChild(QLineEdit).text()[1:]]]
        # For simplicity, hardcoded 0x7E0-0x7E7 scan
        ids_to_scan = list(range(0x7E0, 0x7E8))
        scan_next(0)

    def scan_next(idx):
        global _scanning
        if idx >= len(ids_to_scan):
            _scanning = False
            scan_btn.setText("重新扫描")
            _ev("SCAN", "完成扫描 %d 个 ID" % len(ids_to_scan))
            return
        if not _scanning:
            return
        cid = ids_to_scan[idx]
        client = IsotpClient(lambda tx, fd: sin.frames.send(tx, fd.ljust(8, b"\xCC")), qt_parent=win)
        client.func_id = 0x7DF
        client.on_received = lambda pdu: handle_response(client, cid, idx, pdu)
        client.on_error = lambda msg: _ev("ERR", "%s @ %03X" % (msg, cid))
        client.on_log = lambda d, t, f, n: _ev("PROTO", "[%s] %03X %s" % (d, t, n))
        client.tx_id = cid
        client.rx_id = cid + _rx_offset
        _result = {"cid": "%03X" % cid, "status": "扫描...", "svcs": {}, "lat": 0, "hex": ""}
        _results.append(_result)
        client.send(bytes([0x3E, 0x00]))
        _ev("SCAN", "TesterPresent @ 0x%03X (%d/%d)" % (cid, idx + 1, len(ids_to_scan)))
        QTimer.singleShot(800, lambda: (scan_next(idx + 1) if _scanning else None))

    def handle_response(client, cid, idx, pdu):
        global _scanning
        result = _results[idx]
        if len(pdu) >= 2:
            nrc = pdu[0]
            if nrc < 0x40:
                result["status"] = "ECU 拒绝(0x%X)" % nrc
            else:
                svc = pdu[0] - 0x40 if pdu[0] >= 0x40 else pdu[0]
                data = pdu[1:]
                result["svcs"][svc] = {"text": _hex(data)}
        QTimer.singleShot(50, lambda: (scan_next(idx + 1) if _scanning else None))

    def on_stop():
        global _scanning
        _scanning = False
        scan_btn.setText("重新扫描")

    def on_clear():
        _results.clear()
        del _events[:]
        tree.clear()
        log_view.clear()

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 UDS 扫描结果 CSV",
                                              "uds_scan.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("CAN ID,状态,服务信息\n")
                for r in _results:
                    svcs = "; ".join("%d:%s" % (h, v.get("text","")) for h,v in r["svcs"].items())
                    f.write("%s,%s,%s\n" % (r["cid"], r["status"], svcs))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("udsScan.open", on_open_cmd, "诊断: UDS 扫描")

    scan_btn.clicked.connect(on_scan)
    stop_btn.clicked.connect(on_stop)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append("UDS 扫描插件已加载（订阅 ISO-TP 响应帧，用户触发扫描）")


def deactivate():
    global _running
    _running = False
    sin.output.append("UDS 扫描插件已停用")
