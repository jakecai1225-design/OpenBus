# -*- coding: utf-8 -*-
"""trigger-logger 插件 — 条件触发记录（CANalyzer Trigger 风格）
功能：
- 触发条件：ID 匹配 + 数据掩码（位级）/ 简单条件（任意帧）
- 预触发环形缓冲（深度可配 100-10000 帧）
- 后触发窗口（触发后再录 N 帧或 N 秒）
- 单次 / 重复触发模式；触发冻结清单 + 时间戳
- CSV 导出；纯订阅只读（不发送）
依赖: pip install PyQt6
"""

import csv
import time
from collections import deque

from PyQt6.QtCore import QTimer

import sin

_pre_buf = deque(maxlen=1000)
_post_frames = []
_events = []
_state = {"armed": False, "mode_once": True, "triggered": False,
          "post_target": 100, "post_count": 0,
          "cond": None, "pre_depth": 1000}
_running = True


def _check_trigger(frame):
    cond = _state["cond"]
    if cond is None:
        return True     # 无条件（任意帧触发）
    if frame.id != cond["id"]:
        return False
    if cond["mask"] and frame.data:
        for i, mask_byte in enumerate(cond["mask"]):
            if i < len(frame.data):
                if (frame.data[i] & mask_byte) != (cond["value"][i] & mask_byte):
                    return False
    return True


def _on_frame(frame):
    if not _running or not _state["armed"]:
        return
    rec = (time.time(), frame.id, frame.extended, frame.direction,
           frame.dlc, frame.data)
    if not _state["triggered"]:
        _pre_buf.append(rec)
        if _check_trigger(frame):
            _state["triggered"] = True
            _state["post_count"] = 0
            _events.append((time.time(), "触发", "ID 0x%X 命中条件" % frame.id))
    else:
        _post_frames.append(rec)
        _state["post_count"] += 1
        if _state["post_count"] >= _state["post_target"]:
            _events.append((time.time(), "完成", "后触发窗口已满（%d 帧）"
                            % _state["post_count"]))
            if _state["mode_once"]:
                _state["armed"] = False
                _state["triggered"] = False
            else:
                _pre_buf.clear()
                _state["triggered"] = False


def activate(context):
    global _running
    _running = True
    _pre_buf.clear()
    del _post_frames[:]
    del _events[:]
    _state.update({"armed": False, "triggered": False})

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QLineEdit, QTreeWidget, QTreeWidgetItem, QTextEdit,
            QFileDialog, QMessageBox, QHeaderView, QGroupBox, QFormLayout,
            QSpinBox, QCheckBox
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("触发记录插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("条件触发记录")
    win.resize(980, 640)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    cfg = QGroupBox("触发条件")
    cfg_l = QFormLayout(cfg)
    id_edit = QLineEdit("0x123")
    mask_edit = QLineEdit("（无掩码=该 ID 任意数据）")
    mask_edit.setPlaceholderText("如: FF 00 FF 00 00 00 00 00")
    val_edit = QLineEdit("（无）")
    val_edit.setPlaceholderText("如: AB 00 11 00 00 00 00 00")
    pre_spin = QSpinBox()
    pre_spin.setRange(100, 10000)
    pre_spin.setValue(1000)
    pre_spin.setSuffix(" 帧")
    post_spin = QSpinBox()
    post_spin.setRange(10, 10000)
    post_spin.setValue(100)
    post_spin.setSuffix(" 帧")
    once_chk = QCheckBox("单次模式（触发一次后停止；不勾=重复触发）")
    once_chk.setChecked(True)
    cfg_l.addRow("触发 ID:", id_edit)
    cfg_l.addRow("数据掩码:", mask_edit)
    cfg_l.addRow("数据值:", val_edit)
    cfg_l.addRow("预触发缓冲:", pre_spin)
    cfg_l.addRow("后触发窗口:", post_spin)
    cfg_l.addRow(once_chk)
    layout.addWidget(cfg)

    btns = QHBoxLayout()
    arm_btn = QPushButton("布防（开始等待触发）")
    disarm_btn = QPushButton("撤防")
    export_btn = QPushButton("导出冻结帧 CSV")
    clear_btn = QPushButton("清零")
    btns.addWidget(arm_btn)
    btns.addWidget(disarm_btn)
    btns.addStretch(1)
    btns.addWidget(export_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)

    status = QLabel("未布防")
    status.setStyleSheet("font-weight:bold;")
    layout.addWidget(status)

    tree = QTreeWidget()
    tree.setHeaderLabels(["#", "时间", "ID", "方向", "DLC", "数据", "相对触发"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    event_view = QTextEdit()
    event_view.setReadOnly(True)
    event_view.setMaximumHeight(110)
    layout.addWidget(event_view)

    def _parse_bytes(text):
        text = text.strip()
        if not text or text.startswith("（"):
            return None
        try:
            if len(text) % 2:
                text = "0" + text
            return bytes.fromhex(text.replace(" ", ""))
        except ValueError:
            return b""

    def _on_arm():
        try:
            cid = int(id_edit.text(), 0)
        except ValueError:
            QMessageBox.warning(win, "格式错误", "ID 需为十六进制")
            return
        mask = _parse_bytes(mask_edit.text())
        value = _parse_bytes(val_edit.text())
        if mask is None or all(b == 0 for b in (mask or b"")):
            cond = {"id": cid, "mask": None, "value": None}
        else:
            if value is None or len(value) < len(mask):
                value = (value or b"").ljust(len(mask), b"\x00")
            cond = {"id": cid, "mask": mask, "value": value}
        _state["cond"] = cond
        _state["mode_once"] = once_chk.isChecked()
        _state["post_target"] = post_spin.value()
        # 重建预触发环形缓冲（deque maxlen 不可变）
        globals()["_pre_buf"] = deque(maxlen=pre_spin.value())
        _state["pre_depth"] = pre_spin.value()
        _state["armed"] = True
        _state["triggered"] = False
        del _post_frames[:]
        arm_btn.setEnabled(False)
        disarm_btn.setEnabled(True)
        cond_desc = "ID 0x%X" % cid
        if mask:
            cond_desc += " 掩码 %s" % " ".join("%02X" % b for b in mask)
        status.setText("已布防: %s（预 %d 帧 / 后 %d 帧 / %s）"
                       % (cond_desc, pre_spin.value(), post_spin.value(),
                          "单次" if once_chk.isChecked() else "重复"))
        _events.append((time.time(), "布防", cond_desc))

    def _on_disarm():
        _state["armed"] = False
        _state["triggered"] = False
        arm_btn.setEnabled(True)
        disarm_btn.setEnabled(False)
        status.setText("已撤防")
        _events.append((time.time(), "撤防", ""))

    def _refresh():
        if _events:
            event_view.setPlainText("\n".join(
                "[%s] %s %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-80:]))
            sb = event_view.verticalScrollBar()
            sb.setValue(sb.maximum())
        if _state["armed"]:
            status.setText("已布防 · 预缓冲 %d/%d%s"
                           % (len(_pre_buf), _state["pre_depth"],
                              " · 已触发，后录 %d/%d" % (
                                  _state["post_count"], _state["post_target"])
                              if _state["triggered"] else ""))

        # 冻结清单（触发完成后展示）
        if _post_frames:
            tree.clear()
            all_frames = list(_pre_buf) + _post_frames
            if all_frames:
                trig_time = all_frames[len(_pre_buf)][0] if len(_pre_buf) < len(all_frames) else all_frames[-1][0]
                for i, (ts, cid, ext, direction, dlc, data) in enumerate(all_frames[-300:]):
                    rel = (ts - trig_time) * 1000.0
                    item = QTreeWidgetItem([
                        str(i), time.strftime("%H:%M:%S.%f", time.localtime(ts))[:-3],
                        "0x%X" % cid, direction, str(dlc),
                        " ".join("%02X" % b for b in data),
                        "%+.1fms" % rel])
                    if rel > 0:
                        item.setBackground(6, QColor("#e3f2fd"))
                    tree.addTopLevelItem(item)

    def _on_export():
        all_frames = list(_pre_buf) + _post_frames
        if not all_frames:
            QMessageBox.information(win, "提示", "无冻结数据（先布防并等待触发）")
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出冻结帧", "trigger_capture.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            t0 = all_frames[0][0]
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = csv.writer(f)
                w.writerow(["相对时间s", "ID", "扩展", "方向", "DLC", "数据"])
                for ts, cid, ext, direction, dlc, data in all_frames:
                    w.writerow(["%.6f" % (ts - t0), "0x%X" % cid,
                                "是" if ext else "否", direction, dlc,
                                " ".join("%02X" % b for b in data)])
            QMessageBox.information(win, "导出成功", "已导出 %d 帧" % len(all_frames))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("triggerLogger.open", _on_open_cmd, "录制: 触发记录")

    def _on_clear():
        _pre_buf.clear()
        del _post_frames[:]
        del _events[:]
        tree.clear()

    arm_btn.clicked.connect(_on_arm)
    disarm_btn.clicked.connect(_on_disarm)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)

    timer = QTimer()
    timer.timeout.connect(_refresh)
    timer.start(300)

    disarm_btn.setEnabled(False)

    win.show()
    sin.output.append("触发记录插件已加载（预触发环形缓冲，只读安全）")


def deactivate():
    global _running
    _running = False
    _state["armed"] = False
    sin.output.append("触发记录插件已停用")
