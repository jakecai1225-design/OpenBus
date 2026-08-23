# -*- coding: utf-8 -*-
"""can-gateway 插件 — CAN 报文网关/转发器
功能：
- 规则表：ID 匹配（精确/掩码）→ 动作（ID 重映射/载荷补丁/限频/直通）
- 规则启停、命中计数、最近转发时间
- 全局启停（默认停止；仅用户点击「启动转发」才发帧）
- 规则导入/导出 JSON
依赖: pip install PyQt6
"""

import json
import time

from PyQt6.QtCore import QTimer

import sin

_rules = []          # {match, mask, new_id, patch(pos,hex), rate_limit_ms, enabled, hits, last_ts}
_active = False


def _match_rule(frame):
    for r in _rules:
        if not r.get("enabled", True):
            continue
        mask = r.get("mask", 0x7FF)
        if (frame.id & mask) == (r["match"] & mask):
            # 限频
            rate = r.get("rate_limit_ms", 0)
            now = time.time() * 1000.0
            if rate and r.get("last_tx") and now - r["last_tx"] < rate:
                return None, r, True     # 被限频丢弃
            return r, r, False
    return None, None, False


def _on_frame(frame):
    if not _active:
        return
    r, _, limited = _match_rule(frame)
    if r is None:
        return
    r["hits"] = r.get("hits", 0) + 1
    r["last_ts"] = time.time()
    if limited:
        r["dropped"] = r.get("dropped", 0) + 1
        return
    # 构造转发帧
    new_id = r.get("new_id")
    out_id = new_id if (new_id is not None and new_id >= 0) else frame.id
    data = bytearray(frame.data)
    patch = r.get("patch")   # {"pos": int, "hex": "AA BB"}
    if patch:
        try:
            pos = int(patch.get("pos", 0))
            vals = bytes.fromhex(patch.get("hex", "").replace(" ", ""))
            data[pos:pos + len(vals)] = vals
        except (ValueError, IndexError):
            pass
    r["last_tx"] = time.time() * 1000.0
    sin.frames.send(out_id, bytes(data), extended=frame.extended, fd=frame.fd)


def activate(context):
    global _active
    _active = False

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QLineEdit, QFormLayout, QGroupBox,
            QSpinBox
        )
    except ImportError:
        sin.output.append("报文网关插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("CAN 报文网关")
    win.resize(980, 600)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("添加规则")
    cfg_l = QFormLayout(cfg)
    match_edit = QLineEdit("0x100")
    mask_edit = QLineEdit("0x7FF")
    newid_edit = QLineEdit("（不变）")
    patch_edit = QLineEdit("（无）")
    patch_edit.setPlaceholderText("如 pos=0,hex=AA BB")
    rate_spin = QSpinBox()
    rate_spin.setRange(0, 10000)
    rate_spin.setValue(0)
    rate_spin.setSuffix(" ms")
    cfg_l.addRow("匹配 ID:", match_edit)
    cfg_l.addRow("匹配掩码:", mask_edit)
    cfg_l.addRow("重映射 ID:", newid_edit)
    cfg_l.addRow("载荷补丁:", patch_edit)
    cfg_l.addRow("限频(0=不限):", rate_spin)
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    add_btn = QPushButton("添加规则")
    del_btn = QPushButton("删除选中")
    start_btn = QPushButton("启动转发")
    stop_btn = QPushButton("停止转发")
    save_btn = QPushButton("导出 JSON")
    load_btn = QPushButton("导入 JSON")
    clear_btn = QPushButton("清零计数")
    btns.addWidget(add_btn)
    btns.addWidget(del_btn)
    btns.addStretch(1)
    btns.addWidget(start_btn)
    btns.addWidget(stop_btn)
    btns.addWidget(clear_btn)
    btns.addWidget(load_btn)
    btns.addWidget(save_btn)
    layout.addLayout(btns)

    tree = QTreeWidget()
    tree.setHeaderLabels(["启用", "匹配 ID", "掩码", "重映射", "补丁", "限频ms",
                          "命中", "丢弃", "最后命中"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    header = tree.header()
    header.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    status = QLabel("转发未启动（点击「启动转发」后按规则表转发命中帧）")
    status.setStyleSheet("font-weight:bold;")
    layout.addWidget(status)

    def _refresh():
        tree.clear()
        for r in _rules:
            tree.addTopLevelItem(QTreeWidgetItem([
                "√" if r.get("enabled", True) else "×",
                "0x%X" % r["match"], "0x%X" % r.get("mask", 0x7FF),
                ("0x%X" % r["new_id"]) if r.get("new_id") is not None else "-",
                r.get("patch_text", "-"),
                str(r.get("rate_limit_ms", 0)), str(r.get("hits", 0)),
                str(r.get("dropped", 0)),
                time.strftime("%H:%M:%S", time.localtime(r["last_ts"]))
                if r.get("last_ts") else "-"]))

    def _parse_id(text, default=None):
        text = text.strip()
        if not text or text.startswith("（"):
            return default
        try:
            return int(text, 0)
        except ValueError:
            return default

    def _on_add():
        match = _parse_id(match_edit.text())
        if match is None:
            QMessageBox.warning(win, "格式错误", "匹配 ID 需为十六进制")
            return
        mask = _parse_id(mask_edit.text(), 0x7FF) or 0x7FF
        new_id = _parse_id(newid_edit.text(), None)
        patch_text = patch_edit.text().strip()
        patch = None
        if patch_text and not patch_text.startswith("（"):
            try:
                pos_s, hex_s = patch_text.split(",", 1)
                pos = int(pos_s.strip().replace("pos=", ""))
                hex_s = hex_s.strip().replace("hex=", "").replace(" ", "")
                bytes.fromhex(hex_s)
                patch = {"pos": pos, "hex": hex_s}
            except (ValueError, IndexError):
                QMessageBox.warning(win, "格式错误", "补丁格式: pos=0,hex=AABB")
                return
        _rules.append({"match": match, "mask": mask, "new_id": new_id,
                       "patch": patch, "patch_text": patch_text if patch else "-",
                       "rate_limit_ms": rate_spin.value(), "enabled": True,
                       "hits": 0, "dropped": 0})
        _refresh()

    def _on_del():
        sel = tree.selectedItems()
        if not sel:
            return
        idx = tree.indexOfTopLevelItem(sel[0])
        if 0 <= idx < len(_rules):
            _rules.pop(idx)
            _refresh()

    def _on_start():
        global _active
        if not _rules:
            QMessageBox.information(win, "提示", "请先添加规则")
            return
        _active = True
        start_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        status.setText("转发运行中（%d 条规则）" % len(_rules))
        status.setStyleSheet("font-weight:bold;color:#c62828;")

    def _on_stop():
        global _active
        _active = False
        start_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        status.setText("转发已停止")
        status.setStyleSheet("font-weight:bold;")

    def _on_clear():
        for r in _rules:
            r["hits"] = 0
            r["dropped"] = 0
            r["last_ts"] = None
        _refresh()

    def _on_save():
        path, _ = QFileDialog.getSaveFileName(win, "导出规则", "gateway_rules.json",
                                              "JSON (*.json)")
        if not path:
            return
        try:
            data = [{"match": r["match"], "mask": r.get("mask", 0x7FF),
                     "new_id": r.get("new_id"), "patch": r.get("patch"),
                     "rate_limit_ms": r.get("rate_limit_ms", 0),
                     "enabled": r.get("enabled", True)} for r in _rules]
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            QMessageBox.information(win, "导出成功", "已导出 %d 条规则" % len(data))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_load():
        path, _ = QFileDialog.getOpenFileName(win, "导入规则", "", "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            _rules.clear()
            for d in data:
                p = d.get("patch")
                _rules.append({"match": int(d["match"]), "mask": int(d.get("mask", 0x7FF)),
                               "new_id": d.get("new_id"), "patch": p,
                               "patch_text": ("pos=%d,hex=%s" % (p["pos"], p["hex"])) if p else "-",
                               "rate_limit_ms": int(d.get("rate_limit_ms", 0)),
                               "enabled": bool(d.get("enabled", True)),
                               "hits": 0, "dropped": 0})
            _refresh()
        except (OSError, ValueError, KeyError) as e:
            QMessageBox.warning(win, "导入失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("canGateway.open", _on_open_cmd, "网关: 报文转发")

    add_btn.clicked.connect(_on_add)
    del_btn.clicked.connect(_on_del)
    start_btn.clicked.connect(_on_start)
    stop_btn.clicked.connect(_on_stop)
    clear_btn.clicked.connect(_on_clear)
    save_btn.clicked.connect(_on_save)
    load_btn.clicked.connect(_on_load)
    stop_btn.setEnabled(False)

    # 示例规则（禁用状态）
    _rules.append({"match": 0x100, "mask": 0x700, "new_id": None, "patch": None,
                   "patch_text": "-", "rate_limit_ms": 0, "enabled": False,
                   "hits": 0, "dropped": 0})
    _refresh()

    refresh_timer = QTimer()
    refresh_timer.timeout.connect(_refresh)
    refresh_timer.start(500)

    win.show()
    sin.output.append("报文网关插件已加载（规则转发，激活期间零发送）")


def deactivate():
    global _active
    _active = False
    sin.output.append("报文网关插件已停用")
