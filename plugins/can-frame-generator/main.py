# -*- coding: utf-8 -*-
"""can-frame-generator 插件 — CAN 报文发送器（PCAN-View 风格）
功能：
- 多条目发送表（ID/扩展/FD/DLC/Hex 数据/周期/次数/启用）
- 立即发送（单条）/全部周期启停（QTimer 驱动）
- DBC 感知：加载 DBC 后按报文选择 + 信号级编码（Intel/Motorola、factor/offset）
- 序列播放（按行序一次发出）、条目导入/导出 JSON
- 仅用户点击才发帧（安全基线）
依赖: pip install PyQt6
"""

import json
import time

from PyQt6.QtCore import QTimer

import sin
import dbcparse

_rows = []          # {id, ext, fd, dlc, data, cycle, count, enabled, sent}
_dbc = None         # DbcFile or None
_running = False


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _parse_hex(text):
    text = text.strip().replace(" ", "").replace("0x", "")
    if not text:
        return b""
    if len(text) % 2:
        text = "0" + text
    try:
        return bytes.fromhex(text)
    except ValueError:
        return None


def activate(context):
    global _dbc
    _dbc = None

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QGroupBox, QFormLayout, QLineEdit,
            QSpinBox, QCheckBox, QComboBox, QDialog, QDialogButtonBox,
            QInputDialog
        )
    except ImportError:
        sin.output.append("报文发送器插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN 报文发送器")
    win.resize(1020, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("快速添加（手动模式）")
    cfg_l = QFormLayout(cfg)
    id_edit = QLineEdit("0x123")
    data_edit = QLineEdit("01 02 03 04 05 06 07 08")
    cycle_spin = QSpinBox()
    cycle_spin.setRange(0, 60000)
    cycle_spin.setValue(100)
    cycle_spin.setSuffix(" ms")
    cfg_l.addRow("ID:", id_edit)
    cfg_l.addRow("数据 Hex:", data_edit)
    cfg_l.addRow("周期(0=单次):", cycle_spin)
    layout.addWidget(cfg)

    dbc_bar = QHBoxLayout()
    dbc_btn = QPushButton("加载 DBC…")
    dbc_label = QLabel("未加载（手动 Hex 模式）")
    dbc_label.setStyleSheet("color:#888;")
    dbc_bar.addWidget(dbc_btn)
    dbc_bar.addWidget(dbc_label, 1)
    layout.addLayout(dbc_bar)

    btns = QHBoxLayout()
    add_btn = QPushButton("添加条目")
    add_dbc_btn = QPushButton("从 DBC 添加…")
    del_btn = QPushButton("删除选中")
    send_one_btn = QPushButton("发送选中")
    play_seq_btn = QPushButton("序列播放（全表一次）")
    start_btn = QPushButton("开始周期发送")
    stop_btn = QPushButton("停止")
    save_btn = QPushButton("导出 JSON")
    load_btn = QPushButton("导入 JSON")
    btns.addWidget(add_btn)
    btns.addWidget(add_dbc_btn)
    btns.addWidget(del_btn)
    btns.addWidget(send_one_btn)
    btns.addWidget(play_seq_btn)
    btns.addWidget(start_btn)
    btns.addWidget(stop_btn)
    btns.addStretch(1)
    btns.addWidget(load_btn)
    btns.addWidget(save_btn)
    layout.addLayout(btns)

    tree = QTreeWidget()
    tree.setHeaderLabels(["启用", "ID", "扩展", "FD", "DLC", "数据 Hex",
                          "周期ms", "已发", "报文名"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    header = tree.header()
    header.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    log_view = QTextEdit()
    log_view.setReadOnly(True)
    log_view.setMaximumHeight(140)
    layout.addWidget(log_view)

    def _log(text):
        log_view.append("[%s] %s" % (time.strftime("%H:%M:%S"), text))

    def _refresh():
        tree.clear()
        for r in _rows:
            item = QTreeWidgetItem([
                "√" if r.get("enabled", True) else "×",
                "0x%X" % r["id"], "是" if r.get("ext") else "否",
                "是" if r.get("fd") else "否", str(r.get("dlc", len(r.get("data", b"")))),
                _hex(r.get("data", b"")), str(r.get("cycle", 0)),
                str(r.get("sent", 0)), r.get("name", "")])
            tree.addTopLevelItem(item)

    def _add_manual():
        try:
            cid = int(id_edit.text(), 0)
        except ValueError:
            QMessageBox.warning(win, "格式错误", "ID 需为十六进制（如 0x123）")
            return
        data = _parse_hex(data_edit.text())
        if data is None or len(data) > 64:
            QMessageBox.warning(win, "格式错误", "数据 Hex 非法或超过 64 字节")
            return
        _rows.append({"id": cid, "ext": False, "fd": len(data) > 8,
                      "dlc": len(data), "data": data, "cycle": cycle_spin.value(),
                      "count": 0, "enabled": True, "sent": 0})
        _refresh()
        _log("添加条目 ID=0x%X 数据=%s 周期=%dms" % (cid, _hex(data), cycle_spin.value()))

    def _on_load_dbc():
        global _dbc
        path, _ = QFileDialog.getOpenFileName(win, "加载 DBC", "", "DBC 文件 (*.dbc);;所有文件 (*)")
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "加载失败", "DBC 无报文定义: %s" % path)
            return
        _dbc = db
        names = sorted(db.messages.keys())
        dbc_label.setText("已加载 %s（%d 报文）" % (path.split("\\")[-1], len(names)))
        dbc_label.setStyleSheet("color:#2e7d32;")
        _log("DBC 已加载: %d 报文" % len(names))

    def _add_from_dbc():
        global _dbc
        if _dbc is None:
            QMessageBox.information(win, "提示", "请先加载 DBC")
            return
        dlg = QDialog(win)
        dlg.setWindowTitle("从 DBC 选择报文")
        dlg.resize(480, 560)
        dl = QVBoxLayout(dlg)
        combo = QComboBox()
        for mid in sorted(_dbc.messages.keys()):
            m = _dbc.messages[mid]
            combo.addItem("0x%X %s (%d 信号, 周期 %dms)" % (mid, m.name, len(m.signals), m.cycle_time), mid)
        dl.addWidget(combo)
        msg = _dbc.messages[combo.currentData()]

        sig_edits = {}
        from PyQt6.QtWidgets import QScrollArea, QFrame
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        frame = QFrame()
        fl = QVBoxLayout(frame)
        combo.currentIndexChanged.connect(lambda _i: _rebuild())
        sig_edits_holder = {"box": fl}

        def _rebuild():
            while fl.count():
                item = fl.takeAt(0)
                w = item.widget()
                if w:
                    w.deleteLater()
            nonlocal msg
            msg = _dbc.messages[combo.currentData()]
            sig_edits.clear()
            for sig in msg.signals:
                row = QHBoxLayout()
                row.addWidget(QLabel("%s [min %.2f, max %.2f]%s" % (
                    sig.name, sig.minimum, sig.maximum, (" " + sig.unit) if sig.unit else "")))
                e = QLineEdit("%.2f" % sig.minimum)
                sig_edits[sig.name] = e
                row.addWidget(e)
                fl.addLayout(row)

        _rebuild()
        scroll.setWidget(frame)
        dl.addWidget(scroll, 1)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        dl.addWidget(bb)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        values = {}
        for sig in msg.signals:
            try:
                values[sig.name] = float(sig_edits[sig.name].text())
            except ValueError:
                values[sig.name] = sig.minimum
        data = bytes(dbcparse.encode_message(msg, values, dlc=max(msg.dlc, 8)))
        _rows.append({"id": msg.can_id, "ext": msg.extended, "fd": False,
                      "dlc": len(data), "data": data,
                      "cycle": msg.cycle_time or 100, "count": 0, "enabled": True,
                      "sent": 0, "name": msg.name})
        _refresh()
        _log("从 DBC 添加 %s ID=0x%X 数据=%s" % (msg.name, msg.can_id, _hex(data)))

    def _send_row(r):
        sin.frames.send(r["id"], r["data"], extended=r.get("ext", False), fd=r.get("fd", False))
        r["sent"] = r.get("sent", 0) + 1

    def _on_send_selected():
        sel = tree.selectedItems()
        if not sel:
            return
        idx = tree.indexOfTopLevelItem(sel[0])
        if 0 <= idx < len(_rows):
            _send_row(_rows[idx])
            _log("发送 ID=0x%X %s" % (_rows[idx]["id"], _hex(_rows[idx]["data"])))
            _refresh()

    def _on_play_sequence():
        n = 0
        for r in _rows:
            if r.get("enabled", True):
                _send_row(r)
                n += 1
        _log("序列播放: 发送 %d 条" % n)
        _refresh()

    period_timer = QTimer()

    def _on_tick():
        for r in _rows:
            if not r.get("enabled", True) or not r.get("cycle"):
                continue
            count = r.get("count", 0)
            if count and r.get("sent", 0) >= count:
                continue
            _send_row(r)
        _refresh()

    def _on_start():
        global _running
        if not any(r.get("cycle") for r in _rows):
            QMessageBox.information(win, "提示", "无周期条目（周期 ms 为 0 的条目不参与周期发送）")
            return
        _running = True
        # 找最小周期，取 10ms 粒度 tick
        min_cyc = min(r["cycle"] for r in _rows if r.get("cycle"))
        tick = max(10, min_cyc)
        period_timer.timeout.connect(_on_tick)
        period_timer.start(tick)
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        _log("周期发送开始（tick %dms）" % tick)

    def _on_stop():
        global _running
        _running = False
        period_timer.stop()
        try:
            period_timer.timeout.disconnect(_on_tick)
        except TypeError:
            pass
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        _log("周期发送停止")

    def _on_del():
        sel = tree.selectedItems()
        if not sel:
            return
        idx = tree.indexOfTopLevelItem(sel[0])
        if 0 <= idx < len(_rows):
            _rows.pop(idx)
            _refresh()

    def _on_save():
        path, _ = QFileDialog.getSaveFileName(win, "导出条目 JSON", "tx_entries.json",
                                              "JSON 文件 (*.json)")
        if not path:
            return
        try:
            data = [{"id": r["id"], "ext": r.get("ext", False), "fd": r.get("fd", False),
                     "dlc": r.get("dlc", 8), "data": r["data"].hex(),
                     "cycle": r.get("cycle", 0), "count": r.get("count", 0),
                     "enabled": r.get("enabled", True), "name": r.get("name", "")}
                    for r in _rows]
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            QMessageBox.information(win, "导出成功", "已导出 %d 条" % len(data))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_load():
        path, _ = QFileDialog.getOpenFileName(win, "导入条目 JSON", "", "JSON 文件 (*.json)")
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            _rows.clear()
            for d in data:
                _rows.append({"id": int(d["id"]), "ext": bool(d.get("ext")),
                              "fd": bool(d.get("fd")), "dlc": int(d.get("dlc", 8)),
                              "data": bytes.fromhex(d.get("data", "")),
                              "cycle": int(d.get("cycle", 0)),
                              "count": int(d.get("count", 0)),
                              "enabled": bool(d.get("enabled", True)),
                              "sent": 0, "name": d.get("name", "")})
            _refresh()
            _log("导入 %d 条" % len(_rows))
        except (OSError, ValueError, KeyError) as e:
            QMessageBox.warning(win, "导入失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("canFrameGenerator.open", _on_open_cmd, "发送: 报文发送器")

    add_btn.clicked.connect(_add_manual)
    dbc_btn.clicked.connect(_on_load_dbc)
    add_dbc_btn.clicked.connect(_add_from_dbc)
    del_btn.clicked.connect(_on_del)
    send_one_btn.clicked.connect(_on_send_selected)
    play_seq_btn.clicked.connect(_on_play_sequence)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    save_btn.clicked.connect(_on_save)
    load_btn.clicked.connect(_on_load)
    stop_btn.setEnabled(False)

    # 预置一条示例（不发送）
    _rows.append({"id": 0x123, "ext": False, "fd": False, "dlc": 8,
                  "data": bytes([1, 2, 3, 4, 5, 6, 7, 8]), "cycle": 100,
                  "count": 0, "enabled": False, "sent": 0, "name": "示例（禁用）"})
    _refresh()

    win.show()
    sin.output.append("报文发送器插件已加载（多条目/周期/DBC 信号编码，激活期间零发送）")


def deactivate():
    global _running
    _running = False
    sin.output.append("报文发送器插件已停用")
