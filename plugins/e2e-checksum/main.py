# -*- coding: utf-8 -*-
"""e2e-checksum 插件 — E2E 校验工具（AUTOSAR E2E 检查风格）
功能：
- 多规则配置：报文 ID / CRC 类型（CRC8-J1850 / CRC8H2F / CRC16-CCITT / CRC32 / XOR）
  / CRC 位置（8/16/32bit）/ 保护范围（起始/结束字节）/ 初值与异或覆写
- 实时校验：CRC 比对（期望值 vs 实际值）、alive counter 递增检查（4bit）
- 通过率统计 + 错误清单（时间/类型/期望/实际）+ CSV 导出
- 纯订阅只读（不发送任何帧）
依赖: pip install PyQt6
"""

import csv
import time

from PyQt6.QtCore import QTimer

import sin

# CRC 类型: (width, poly, init, xorout)
_CRC_PRESETS = {
    "CRC8 (SAE J1850, E2E P01)": (8, 0x1D, 0xFF, 0xFF),
    "CRC8H2F (E2E P02)": (8, 0x2F, 0xFF, 0xFF),
    "CRC16-CCITT (E2E P05)": (16, 0x1021, 0xFFFF, 0x00),
    "CRC32 (E2E P04, MPEG-2)": (32, 0x04C11DB7, 0xFFFFFFFF, 0x00000000),
    "XOR-8 校验和": (8, 0x00, 0x00, 0x00),
}

_rules = {}           # id -> rule dict
_errors = []          # [(ts, cid, kind, detail)]
_summary = {"checked": 0, "crc_ok": 0, "crc_err": 0, "alive_err": 0}
_monitor = False
_running = True


def _crc_compute(data, preset_name, init_override=None, xor_override=None):
    if preset_name == "XOR-8 校验和":
        acc = 0
        for b in data:
            acc ^= b
        return acc
    width, poly, init, xorout = _CRC_PRESETS[preset_name]
    if init_override is not None:
        init = init_override & ((1 << width) - 1)
    if xor_override is not None:
        xorout = xor_override & ((1 << width) - 1)
    reg = init
    mask = (1 << width) - 1
    for b in data:
        reg ^= b << (width - 8)
        for _ in range(8):
            if reg & (1 << (width - 1)):
                reg = ((reg << 1) ^ poly) & mask
            else:
                reg = (reg << 1) & mask
    return (reg ^ xorout) & mask


def _crc_extract(data, offset, width):
    """按大端提取 width 位 CRC"""
    if width == 8:
        return data[offset] if offset < len(data) else None
    if width == 16:
        if offset + 1 < len(data):
            return (data[offset] << 8) | data[offset + 1]
        return None
    if width == 32:
        if offset + 3 < len(data):
            return ((data[offset] << 24) | (data[offset + 1] << 16) |
                    (data[offset + 2] << 8) | data[offset + 3])
        return None
    return None


def _on_frame(frame):
    if not _running or not _monitor:
        return
    rule = _rules.get(frame.id)
    if rule is None:
        return
    _summary["checked"] += 1
    data = frame.data
    width = rule["width"]
    off = rule["crc_off"]
    # 保护范围（不含 CRC 字节本身）
    start, end = rule["data_start"], min(rule["data_end"], len(data))
    covered = bytearray()
    for i in range(start, end):
        if off <= i < off + (width // 8):
            continue          # 跳过 CRC 字段
        covered.append(data[i])
    if len(covered) < 1 or end <= start:
        return
    if rule["crc_type"] == "XOR-8 校验和":
        expected = _crc_compute(bytes(covered), rule["crc_type"])
    else:
        expected = _crc_compute(bytes(covered), rule["crc_type"],
                                rule["init"], rule["xor"])
    actual = _crc_extract(data, off, width)
    if actual is None:
        return
    if actual == expected:
        _summary["crc_ok"] += 1
    else:
        _summary["crc_err"] += 1
        _errors.append((time.time(), frame.id, "CRC 错误",
                        "期望 0x%X 实际 0x%X" % (expected, actual)))
    # alive counter 检查（4bit）
    if rule["alive_byte"] < len(data):
        b = data[rule["alive_byte"]]
        cnt = (b >> 4) & 0xF if rule["alive_high"] else b & 0xF
        last = rule.get("alive_last")
        if last is not None:
            delta = (cnt - last) & 0xF
            if delta == 0:
                _summary["alive_err"] += 1
                _errors.append((time.time(), frame.id, "计数器重复",
                                "alive=%d 未递增" % cnt))
            elif delta != 1:
                _summary["alive_err"] += 1
                _errors.append((time.time(), frame.id, "计数器跳变",
                                "%d → %d（丢失 %d 步）" % (last, cnt, delta - 1)))
        rule["alive_last"] = cnt


def activate(context):
    global _monitor
    _monitor = False
    _rules.clear()
    del _errors[:]
    _summary.update({"checked": 0, "crc_ok": 0, "crc_err": 0, "alive_err": 0})

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QDialog, QDialogButtonBox, QFormLayout,
            QLineEdit, QComboBox, QSpinBox, QCheckBox
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("E2E 校验插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("E2E 校验工具")
    win.resize(980, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    btns = QHBoxLayout()
    add_btn = QPushButton("添加校验规则…")
    del_btn = QPushButton("删除选中规则")
    start_btn = QPushButton("开始校验")
    stop_btn = QPushButton("停止")
    export_btn = QPushButton("导出错误 CSV")
    clear_btn = QPushButton("清空结果")
    btns.addWidget(add_btn)
    btns.addWidget(del_btn)
    btns.addWidget(start_btn)
    btns.addWidget(stop_btn)
    btns.addStretch(1)
    btns.addWidget(export_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)

    summary = QLabel("先添加规则（默认 E2E P01 布局：CRC@byte0，alive@byte1 低半字节）")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    rule_tree = QTreeWidget()
    rule_tree.setHeaderLabels(["ID", "CRC 类型", "CRC 位置", "保护范围", "Alive", "状态"])
    rule_tree.setRootIsDecorated(False)
    rule_tree.setAlternatingRowColors(True)
    rule_tree.setMaximumHeight(160)
    layout.addWidget(rule_tree)

    err_tree = QTreeWidget()
    err_tree.setHeaderLabels(["时间", "ID", "类型", "详情"])
    err_tree.setRootIsDecorated(False)
    err_tree.setAlternatingRowColors(True)
    th = err_tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(err_tree, 1)

    def _refresh_rules():
        rule_tree.clear()
        for cid, r in _rules.items():
            rule_tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid, r["crc_type"],
                "byte%d (%dbit)" % (r["crc_off"], r["width"]),
                "byte%d-%d" % (r["data_start"], r["data_end"]),
                "byte%d%s" % (r["alive_byte"], "高" if r["alive_high"] else "低"),
                r["stat_text"]]))

    def _on_add():
        dlg = QDialog(win)
        dlg.setWindowTitle("添加 E2E 校验规则")
        dlg.resize(420, 340)
        form = QFormLayout(dlg)
        id_edit = QLineEdit("0x123")
        preset = QComboBox()
        preset.addItems(list(_CRC_PRESETS.keys()))
        crc_off = QSpinBox()
        crc_off.setRange(0, 63)
        crc_off.setValue(0)
        data_start = QSpinBox()
        data_start.setRange(0, 63)
        data_start.setValue(2)
        data_end = QSpinBox()
        data_end.setRange(1, 64)
        data_end.setValue(8)
        alive_byte = QSpinBox()
        alive_byte.setRange(0, 63)
        alive_byte.setValue(1)
        alive_high = QComboBox()
        alive_high.addItems(["低半字节", "高半字节"])
        init_edit = QLineEdit("默认")
        init_edit.setPlaceholderText("十六进制覆写初值，留空=默认")
        xor_edit = QLineEdit("默认")
        xor_edit.setPlaceholderText("十六进制覆写异或，留空=默认")
        form.addRow("报文 ID:", id_edit)
        form.addRow("CRC 类型:", preset)
        form.addRow("CRC 位置（字节）:", crc_off)
        form.addRow("保护起始字节:", data_start)
        form.addRow("保护结束字节（不含）:", data_end)
        form.addRow("Alive 字节:", alive_byte)
        form.addRow("Alive 半字节:", alive_high)
        form.addRow("初值覆写:", init_edit)
        form.addRow("异或覆写:", xor_edit)
        btns2 = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                 QDialogButtonBox.StandardButton.Cancel)
        btns2.accepted.connect(dlg.accept)
        btns2.rejected.connect(dlg.reject)
        form.addRow(btns2)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        try:
            cid = int(id_edit.text(), 0)
        except ValueError:
            QMessageBox.warning(win, "格式错误", "ID 需为十六进制")
            return
        ptype = preset.currentText()
        width = _CRC_PRESETS[ptype][0]

        def _opt(text):
            t = text.strip()
            if not t or t == "默认":
                return None
            try:
                return int(t, 16)
            except ValueError:
                return None

        _rules[cid] = {"crc_type": ptype, "width": width,
                       "crc_off": crc_off.value(),
                       "data_start": data_start.value(),
                       "data_end": max(data_end.value(), data_start.value() + 1),
                       "alive_byte": alive_byte.value(),
                       "alive_high": alive_high.currentIndex() == 1,
                       "init": _opt(init_edit.text()),
                       "xor": _opt(xor_edit.text()),
                       "alive_last": None, "stat_text": "待校验",
                       "ok": 0, "bad": 0}
        _refresh_rules()

    def _on_del():
        item = rule_tree.currentItem()
        if item is None:
            return
        try:
            cid = int(item.text(0), 0)
            _rules.pop(cid, None)
            _refresh_rules()
        except ValueError:
            pass

    def _on_start():
        if not _rules:
            QMessageBox.information(win, "提示", "请先添加校验规则")
            return
        global _monitor
        _monitor = True
        for r in _rules.values():
            r["alive_last"] = None
            r["stat_text"] = "校验中"

    def _on_stop():
        global _monitor
        _monitor = False
        summary.setText("校验已停止")

    def _on_export():
        if not _errors:
            QMessageBox.information(win, "提示", "无错误记录")
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出错误", "e2e_errors.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = csv.writer(f)
                w.writerow(["时间", "ID", "类型", "详情"])
                for ts, cid, kind, detail in _errors:
                    w.writerow([time.strftime("%H:%M:%S.%f", time.localtime(ts))[:-3],
                                "0x%X" % cid, kind, detail])
            QMessageBox.information(win, "导出成功", "已导出 %d 条" % len(_errors))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_clear():
        del _errors[:]
        _summary.update({"checked": 0, "crc_ok": 0, "crc_err": 0, "alive_err": 0})
        err_tree.clear()
        for r in _rules.values():
            r["alive_last"] = None
            r["ok"] = r["bad"] = 0

    def _refresh():
        if _monitor:
            total = max(1, _summary["crc_ok"] + _summary["crc_err"])
            rate = 100.0 * _summary["crc_ok"] / total
            summary.setText("校验中 · 已检查 %d 帧 · CRC 通过率 %.2f%%（%d/%d）· "
                            "alive 错误 %d · 规则 %d 条"
                            % (_summary["checked"], rate, _summary["crc_ok"],
                               _summary["crc_ok"] + _summary["crc_err"],
                               _summary["alive_err"], len(_rules)))
        if _errors:
            err_tree.clear()
            colors = {"CRC 错误": QColor("#c62828"),
                      "计数器重复": QColor("#ef6c00"),
                      "计数器跳变": QColor("#ef6c00")}
            for ts, cid, kind, detail in _errors[-200:]:
                item = QTreeWidgetItem([
                    time.strftime("%H:%M:%S", time.localtime(ts)),
                    "0x%X" % cid, kind, detail])
                item.setForeground(2, colors.get(kind))
                err_tree.addTopLevelItem(item)
            sb = err_tree.verticalScrollBar()
            sb.setValue(sb.maximum())

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("e2eChecksum.open", _on_open_cmd, "安全: E2E 校验")

    add_btn.clicked.connect(_on_add)
    del_btn.clicked.connect(_on_del)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)

    timer = QTimer()
    timer.timeout.connect(_refresh)
    timer.start(500)

    win.show()
    sin.output.append("E2E 校验插件已加载（多规则实时校验，只读安全）")


def deactivate():
    global _running, _monitor
    _running = False
    _monitor = False
    sin.output.append("E2E 校验插件已停用")
