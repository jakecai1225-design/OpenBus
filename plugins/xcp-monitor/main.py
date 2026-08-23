# -*- coding: utf-8 -*-
"""xcp-monitor 插件 — XCP on CAN（标定测量协议）监视
功能：
- CTO 命令/响应识别：PID 分类（CMD/RES/ERR/EV/SERV）
- 常用命令解码：CONNECT / GET_STATUS / GET_ID / GET_SEED / UNLOCK /
  SET_MTA / UPLOAD / SHORT_UPLOAD / DOWNLOAD / BUILD_CHECKSUM 等
- DAQ/STIM DTO 识别（ODT 序号），CTO/DTO 会话视图
- 活动统计（命令计数 / 错误码计数）+ 事件流 + CSV 导出；纯监视不发送
依赖: pip install PyQt6
"""

import time

import sin

PID_NAMES = {
    0xFF: "RES 响应", 0xFE: "ERR 错误", 0xFD: "EV 事件", 0xFC: "SERV 服务请求",
    0xFB: "DAQRDY? (0xFB)", 0xFA: "DAQRDY? (0xFA)",
}

CMD_NAMES = {
    0x00: "CONNECT",
    0xFD: "GET_STATUS",
    0xFC: "GET_COMM_MODE_INFO",
    0xFB: "GET_ID",
    0xFA: "SET_REQUEST",
    0xF9: "GET_SEED",
    0xF7: "UNLOCK",
    0xF6: "SET_MTA",
    0xF5: "UPLOAD",
    0xF4: "SHORT_UPLOAD",
    0xF3: "BUILD_CHECKSUM",
    0xF2: "TRANSPORT_LAYER_CMD",
    0xF1: "DOWNLOAD_NEXT? / SET_CAL_PAGE 组",
    0xF0: "DOWNLOAD",
    0xEE: "DOWNLOAD_MAX",
    0xE7: "DIAG_SERVICE",
    0xE5: "FREE_DAQ",
    0xE4: "ALLOC_DAQ",
    0xE3: "SET_DAQ_PTR",
    0xE2: "WRITE_DAQ",
    0xE0: "START_STOP",
    0xDF: "DIAG_SERVICE? (0xDF)",
    0xDE: "SELECT_CAL_PAGE 组",
    0xDD: "PGM 组",
}

DAQ_RANGE = (0xD6, 0xE5)

ERR_CODES = {
    0x00: "命令被理解但无法执行",
    0x10: "命令不被支持（已暂时失效）",
    0x11: "命令未被该设备实现",
    0x12: "命令非法 / 参数无效",
    0x13: "数据字节越界（长度错）",
    0x14: "页面模式保护 / 访问被拒",
    0x20: "资源被功能锁保护",
    0x21: "数据块太大 / 越界访问",
    0x22: "校验错误",
    0x23: "超范围 / 资源不可用",
    0x30: "通用错误",
    0x31: "处理器被挂起（内部错误）",
    0x32: "通用错误（定义未生效）",
}

_cto_count = 0
_dto_count = 0
_err_count = 0
_cmd_stats = {}
_events = []
_running = True


def _ev(kind, text):
    _events.append((time.time(), kind, text))
    if len(_events) > 2000:
        del _events[:1000]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _on_frame(frame):
    global _cto_count, _dto_count, _err_count
    if not _running or not frame.data:
        return
    data = frame.data
    first = data[0]

    # CTO：PID 高位区（0xFC-0xFF）或已知命令码
    if first >= 0xFC:
        _cto_count += 1
        name = PID_NAMES.get(first, "PID %02X" % first)
        if first == 0xFF:       # RES：回显对应命令（byte1）
            cmd = data[1] if len(data) > 1 else 0
            cmd_name = CMD_NAMES.get(cmd, "CMD %02X" % cmd)
            _ev("RES", "%s ← 命令 %s: %s" % (name, cmd_name, _hex(data[:8])))
            _cmd_stats[("RES", cmd_name)] = _cmd_stats.get(("RES", cmd_name), 0) + 1
        elif first == 0xFE:     # ERR：错误码
            _err_count += 1
            code = data[1] if len(data) > 1 else 0
            desc = ERR_CODES.get(code, "未知错误码")
            _ev("ERR", "错误响应 code=%02X (%s): %s" % (code, desc, _hex(data[:8])))
            _cmd_stats[("ERR", "code=%02X" % code)] = _cmd_stats.get(("ERR", "code=%02X" % code), 0) + 1
        elif first == 0xFD:     # EV：事件
            code = data[1] if len(data) > 1 else 0
            _ev("EV", "事件 code=%02X: %s" % (code, _hex(data[:8])))
        elif first == 0xFC:     # SERV
            _ev("SERV", "服务请求: %s" % _hex(data[:8]))
        else:
            _ev("CTO", "%s: %s" % (name, _hex(data[:8])))
        return

    # 命令方向（master→slave）：已知命令码或 DAQ 配置区
    if first in CMD_NAMES:
        _cto_count += 1
        cmd_name = CMD_NAMES[first]
        _ev("CMD", "%s: %s" % (cmd_name, _hex(data[:8])))
        _cmd_stats[("CMD", cmd_name)] = _cmd_stats.get(("CMD", cmd_name), 0) + 1
        return

    if DAQ_RANGE[0] <= first <= DAQ_RANGE[1]:
        _cto_count += 1
        cmd_name = CMD_NAMES.get(first, "DAQ 组 %02X" % first)
        _ev("CMD", "%s: %s" % (cmd_name, _hex(data[:8])))
        _cmd_stats[("CMD", cmd_name)] = _cmd_stats.get(("CMD", cmd_name), 0) + 1
        return

    # 其余小首字节 → DTO（DAQ/STIM 数据，byte0 = ODT 序号）
    _dto_count += 1
    odt = first
    if odt == 0:
        # ODT 0 常含 PID 字节与 DAQ 目录信息，仍按数据记录
        _ev("DTO", "ODT0: %s" % _hex(data[:8]))


def activate(context):
    global _running
    _cmd_stats.clear()
    del _events[:]
    global _cto_count, _dto_count, _err_count
    _cto_count = _dto_count = _err_count = 0

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog,
            QMessageBox, QHeaderView, QTabWidget
        )
        from PyQt6.QtCore import QTimer
    except ImportError:
        sin.output.append("XCP 监视插件需要 PyQt6: pip install PyQt6")
        return

    _running = True
    win = sin.ui.create_window("XCP on CAN 监视")
    win.resize(920, 600)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    summary = QLabel("等待 XCP 报文（按 CTO/DTO 结构识别）...")
    summary.setStyleSheet("font-weight: bold;")
    top.addWidget(summary, 1)
    clear_btn = QPushButton("清零")
    export_btn = QPushButton("导出 CSV")
    top.addWidget(clear_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    stat_tab = QWidget()
    sv = QVBoxLayout(stat_tab)
    stat_tree = QTreeWidget()
    stat_tree.setHeaderLabels(["方向", "命令/类型", "次数"])
    stat_tree.setRootIsDecorated(False)
    stat_tree.setAlternatingRowColors(True)
    stat_tree.setSortingEnabled(True)
    sh = stat_tree.header()
    sh.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    sv.addWidget(stat_tree, 1)

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    log_view = QTextEdit()
    log_view.setReadOnly(True)
    lv.addWidget(log_view, 1)

    tabs.addTab(stat_tab, "命令统计")
    tabs.addTab(log_tab, "事件流")

    hint = QLabel("识别规则：byte0=PID（FF RES / FE ERR / FD EV / FC SERV）为 CTO；"
                  "已知命令码为 CMD；其余按 DAQ/STIM DTO（ODT 序号）统计")
    hint.setStyleSheet("color: #888; font-size: 11px;")
    layout.addWidget(hint)

    context.on_frame(_on_frame)

    def refresh():
        summary.setText("CTO 帧 %d    DTO 帧 %d    错误响应 %d    命令类型 %d"
                        % (_cto_count, _dto_count, _err_count, len(_cmd_stats)))

        stat_tree.setSortingEnabled(False)
        stat_tree.clear()
        for (kind, name), count in _cmd_stats.items():
            stat_tree.addTopLevelItem(QTreeWidgetItem([kind, name, str(count)]))
        stat_tree.setSortingEnabled(True)

        if _events:
            log_view.setPlainText("\n".join(
                "[%s] %s %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-200:]))
            sb = log_view.verticalScrollBar()
            sb.setValue(sb.maximum())

    timer = QTimer()
    timer.timeout.connect(refresh)
    timer.start(600)

    def on_clear():
        global _cto_count, _dto_count, _err_count
        _cmd_stats.clear()
        del _events[:]
        _cto_count = _dto_count = _err_count = 0

    def on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 XCP 记录 CSV",
                                              "xcp_monitor.csv", "CSV 文件 (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("方向,命令/类型,次数\n")
                for (kind, name), count in _cmd_stats.items():
                    f.write("%s,%s,%d\n" % (kind, name, count))
                f.write("\n事件流\n时间,类型,内容\n")
                for ts, k, t in _events:
                    f.write("%s,%s,%s\n" % (time.strftime("%H:%M:%S", time.localtime(ts)), k, t))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("xcpMonitor.open", on_open_cmd, "标定: XCP 监视")

    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    win.show()
    sin.output.append("XCP 监视插件已加载（订阅实时帧，CTO/DTO 被动识别）")


def deactivate():
    global _running
    _running = False
    sin.output.append("XCP 监视插件已停用")
