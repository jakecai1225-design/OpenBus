# -*- coding: utf-8 -*-
"""iso-tp-monitor 插件 — ISO-TP（ISO 15765-2）会话被动监视
功能：
- 被动解码总线上所有 ISO-TP 会话：SF / FF / CF / FC PCI 全识别
- 多帧重组：按 CAN ID 跟踪会话（期望长度、已收字节、序号校验、超时检测）
- FC 流控帧解码（FS=CTS/WAIT/OVFLW，BS、STmin）
- 完整 PDU 清单（时间戳、ID、长度、Hex）+ 事件日志 + CSV 导出
- 纯监视，不发送任何帧
数据源：context.on_frame 订阅（约 100ms 批量推送）
依赖: pip install PyQt6
"""

import time

import sin

# 会话状态：can_id → dict
_sessions = {}
_pdus = []          # 完整 PDU 记录 [(ts, can_id, pdu_hex)]
_events = []        # 事件日志 [(ts, kind, text)]
_total_frames = 0
_fc_frames = 0
_timeout_ms = 1000.0
_running = True
_enabled = True

FS_NAMES = {0: "CTS", 1: "WAIT", 2: "OVFLW"}


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 2000:
        del _events[:1000]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _stmin_text(raw):
    if raw <= 0x7F:
        return "%dms" % raw
    if 0xF1 <= raw <= 0xF9:
        return "%d×100µs" % (raw - 0xF0)
    return "保留(%02X)" % raw


def _on_frame(frame):
    global _total_frames, _fc_frames
    if not _running or not _enabled:
        return
    data = frame.data
    if not data:
        return
    _total_frames += 1
    fid = frame.id
    ts = frame.timestamp
    pci = data[0]
    kind = pci & 0xF0

    if kind == 0x00:                       # SF
        length = pci & 0x0F
        if length == 0:
            return
        pdu = bytes(data[1:1 + length])
        if pdu:
            _pdus.append((ts, fid, _hex(pdu)))
            if len(_pdus) > 5000:
                del _pdus[:1000]
            _ev("PDU", "SF 0x%X 完成 %d 字节: %s" % (fid, length, _hex(pdu[:24])))
    elif kind == 0x10:                     # FF → 新会话
        expected = ((pci & 0x0F) << 8) | (data[1] if len(data) > 1 else 0)
        got = max(0, len(data) - 2)
        _sessions[fid] = {
            "expected": expected, "got": got, "last_sn": 0,
            "last_ts": ts, "done": 0, "state": "接收中",
        }
        _ev("FF", "0x%X FF 期望 %d 字节（首帧携带 %d）" % (fid, expected, got))
    elif kind == 0x20:                     # CF
        sn = pci & 0x0F
        st = _sessions.get(fid)
        if st is None:
            return
        st["last_ts"] = ts
        st["got"] += max(0, len(data) - 1)
        if sn == ((st["last_sn"] + 1) & 0x0F):
            st["last_sn"] = sn
        else:
            _ev("SN", "0x%X CF 序号异常: 期望 %d 收到 %d" % (fid, (st["last_sn"] + 1) & 0x0F, sn))
            st["last_sn"] = sn
        if st["got"] >= st["expected"]:
            st["done"] += 1
            st["state"] = "完成"
            _pdus.append((ts, fid, "<多帧 %d 字节>" % st["expected"]))
            _ev("PDU", "0x%X 多帧重组完成 %d 字节（第 %d 次）" % (fid, st["expected"], st["done"]))
    elif kind == 0x30:                     # FC（被动观察）
        _fc_frames += 1
        fs = pci & 0x0F
        bs = data[1] if len(data) > 1 else 0
        stmin = data[2] if len(data) > 2 else 0
        _ev("FC", "0x%X FC: FS=%s BS=%d STmin=%s"
            % (fid, FS_NAMES.get(fs, "%d" % fs), bs, _stmin_text(stmin)))


def activate(context):
    global _running, _enabled, _timeout_ms
    _sessions.clear()
    del _pdus[:]
    del _events[:]

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QSpinBox, QTabWidget
        )
        from PyQt6.QtCore import QTimer
    except ImportError:
        sin.output.append("ISO-TP 监视插件需要 PyQt6: pip install PyQt6")
        return

    _running = True
    _enabled = True

    win = sin.ui.create_window("ISO-TP 会话监视 (ISO 15765-2)")
    win.resize(940, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    # ---------- Tab 1: 会话 ----------
    sess_tab = QWidget()
    sv = QVBoxLayout(sess_tab)

    top = QHBoxLayout()
    summary = QLabel("等待数据...")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    top.addWidget(QLabel("会话超时(ms)"))
    timeout_spin = QSpinBox()
    timeout_spin.setRange(200, 10000)
    timeout_spin.setSingleStep(100)
    timeout_spin.setValue(int(_timeout_ms))
    top.addWidget(timeout_spin)
    pause_btn = QPushButton("暂停")
    clear_btn = QPushButton("清零")
    export_btn = QPushButton("导出 CSV")
    top.addWidget(pause_btn)
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    sv.addLayout(top)

    sess_tree = QTreeWidget()
    sess_tree.setHeaderLabels(["CAN ID", "状态", "期望字节", "已收字节", "完成次数",
                               "最后序号", "最后活动(s)"])
    sess_tree.setRootIsDecorated(False)
    sess_tree.setAlternatingRowColors(True)
    header = sess_tree.header()
    header.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(sess_tree, 2)

    hint = QLabel("被动监视：识别 SF/FF/CF/FC，按 CAN ID 跟踪多帧会话重组进度；不发送任何帧")
    hint.setStyleSheet("color: #888; font-size: 11px;")
    sv.addWidget(hint)

    # ---------- Tab 2: PDU 清单 ----------
    pdu_tab = QWidget()
    pv = QVBoxLayout(pdu_tab)
    pdu_tree = QTreeWidget()
    pdu_tree.setHeaderLabels(["时间戳(s)", "CAN ID", "PDU Hex"])
    pdu_tree.setRootIsDecorated(False)
    pdu_tree.setAlternatingRowColors(True)
    ph = pdu_tree.header()
    ph.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    pv.addWidget(pdu_tree, 1)

    # ---------- Tab 3: 事件日志 ----------
    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(sess_tab, "会话")
    tabs.addTab(pdu_tab, "PDU 清单")
    tabs.addTab(log_tab, "事件日志")

    context.on_frame(_on_frame)

    def refresh():
        now = time.time()
        if _sessions:
            summary.setText("总帧数 %d    ISO-TP 会话 %d    完整 PDU %d    FC 帧 %d"
                            % (_total_frames, len(_sessions), len(_pdus), _fc_frames))
        else:
            summary.setText("总帧数 %d    ISO-TP 会话 0    完整 PDU %d    FC 帧 %d"
                            % (_total_frames, len(_pdus), _fc_frames))

        # 超时检测
        for fid, st in _sessions.items():
            if st["state"] == "接收中" and now - st["last_ts"] > _timeout_ms / 1000.0:
                st["state"] = "超时"
                _ev("TO", "0x%X 会话超时（%d/%d 字节）" % (fid, st["got"], st["expected"]))

        sess_tree.setSortingEnabled(False)
        sess_tree.clear()
        for fid, st in _sessions.items():
            sess_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % fid, st["state"], str(st["expected"]), str(st["got"]),
                str(st["done"]), str(st["last_sn"]),
                "%.3f" % (now - st["last_ts"])]))
        sess_tree.setSortingEnabled(True)

        pdu_tree.setSortingEnabled(False)
        pdu_tree.clear()
        for ts, fid, hx in _pdus[-300:]:
            pdu_tree.addTopLevelItem(QTreeWidgetItem(["%.6f" % ts, "0x%X" % fid, hx]))
        pdu_tree.scrollToBottom()
        pdu_tree.setSortingEnabled(True)

        if _events:
            lines = []
            for ts, kind, text in _events[-200:]:
                lines.append("[%s] %s %s" % (time.strftime("%H:%M:%S"), kind, text))
            log_view.setPlainText("\n".join(lines))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

    timer = QTimer()
    timer.timeout.connect(refresh)
    timer.start(500)

    def on_pause():
        global _enabled
        _enabled = not _enabled
        pause_btn.setText("继续" if not _enabled else "暂停")

    def on_clear():
        _sessions.clear()
        del _pdus[:]
        del _events[:]
        sess_tree.clear()
        pdu_tree.clear()
        log_view.clear()
        summary.setText("等待数据...")

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 ISO-TP 记录 CSV",
                                              "isotp_monitor.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("时间戳s,CAN ID,PDU/说明\n")
                for ts, fid, hx in _pdus:
                    f.write("%.6f,0x%X,%s\n" % (ts, fid, hx))
                f.write("\n事件日志\n时间,类型,内容\n")
                for ts, kind, text in _events:
                    f.write("%s,%s,%s\n" % (time.strftime("%H:%M:%S", time.localtime(ts)),
                                            kind, text))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_timeout_changed(v):
        global _timeout_ms
        _timeout_ms = float(v)

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("isoTpMonitor.open", on_open_cmd, "协议: ISO-TP 监视")

    pause_btn.clicked.connect(on_pause)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)
    timeout_spin.valueChanged.connect(on_timeout_changed)

    win.show()
    sin.output.append("ISO-TP 监视插件已加载（订阅实时帧，被动监视）")


def deactivate():
    global _running
    _running = False
    sin.output.append("ISO-TP 监视插件已停用")
