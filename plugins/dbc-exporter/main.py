# -*- coding: utf-8 -*-
"""dbc-exporter 插件 — DBC 导出（canmatrix convert 风格）
功能：
- DBC → 信号矩阵 CSV / JSON / HTML
- 导出选项：含注释 / 含值表 / 含收发节点 / 含最小最大值
- 分组视图（按发送节点分组）
- 纯离线工具
依赖: pip install PyQt6
"""

import json
import time

import sin
import dbcparse


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QCheckBox
        )
    except ImportError:
        sin.output.append("DBC 导出插件需要 PyQt6: pip install PyQt6")
        return

    db = {"file": None}

    win = sin.ui.create_window("DBC 导出工具")
    win.resize(960, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_btn = QPushButton("加载 DBC…")
    label = QLabel("未加载")
    label.setStyleSheet("color:#888;")
    top.addWidget(load_btn)
    top.addWidget(label, 1)
    layout.addLayout(top)

    opts = QHBoxLayout()
    opt_comment = QCheckBox("含注释")
    opt_comment.setChecked(True)
    opt_values = QCheckBox("含值表")
    opt_values.setChecked(True)
    opt_nodes = QCheckBox("含收发节点")
    opt_nodes.setChecked(True)
    opt_minmax = QCheckBox("含 min/max")
    opt_minmax.setChecked(True)
    for w in (opt_comment, opt_values, opt_nodes, opt_minmax):
        opts.addWidget(w)
    opts.addStretch(1)
    layout.addLayout(opts)

    btns = QHBoxLayout()
    csv_btn = QPushButton("导出信号矩阵 CSV")
    json_btn = QPushButton("导出 JSON")
    html_btn = QPushButton("导出 HTML")
    btns.addWidget(csv_btn)
    btns.addWidget(json_btn)
    btns.addWidget(html_btn)
    btns.addStretch(1)
    layout.addLayout(btns)

    tree = QTreeWidget()
    tree.setHeaderLabels(["报文", "ID", "DLC", "发送节点", "周期", "信号数"])
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    def _refresh():
        tree.clear()
        if not db["file"]:
            return
        for cid, m in db["file"].messages.items():
            parent = QTreeWidgetItem([
                m.name, "0x%X" % cid, str(m.dlc), m.sender,
                str(m.cycle_time), str(len(m.signals))])
            tree.addTopLevelItem(parent)
            for s in m.signals:
                detail = "%d|%d@%s%s (%g,%g) [%g|%g] %s" % (
                    s.start_bit, s.bit_length,
                    "i" if s.little_endian else "m", "-" if s.is_signed else "+",
                    s.factor, s.offset, s.minimum, s.maximum, s.unit)
                if opt_comment.isChecked() and s.comment:
                    detail += " // %s" % s.comment
                parent.addChild(QTreeWidgetItem([s.name, "", "", "", "", detail]))

    def _on_load():
        path, _ = QFileDialog.getOpenFileName(win, "加载 DBC", "", "DBC 文件 (*.dbc)")
        if not path:
            return
        d = dbcparse.parse_file(path)
        if not d.messages:
            QMessageBox.warning(win, "加载失败", "无报文定义")
            return
        db["file"] = d
        label.setText("已加载 %s（%d 报文, %d 信号）"
                      % (path.split("\\")[-1], len(d.messages),
                         sum(len(m.signals) for m in d.messages.values())))
        label.setStyleSheet("color:#2e7d32;")
        _refresh()

    def _require_db():
        if not db["file"]:
            QMessageBox.information(win, "提示", "请先加载 DBC")
            return None
        return db["file"]

    def _on_csv():
        d = _require_db()
        if d is None:
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出信号矩阵 CSV", "signal_matrix.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                header = ["报文名", "ID", "DLC", "发送节点", "周期ms", "信号名",
                          "起始位", "长度", "字节序", "符号", "factor", "offset"]
                if opt_minmax.isChecked():
                    header += ["min", "max"]
                header += ["单位"]
                if opt_nodes.isChecked():
                    header += ["接收节点"]
                if opt_values.isChecked():
                    header += ["值表"]
                if opt_comment.isChecked():
                    header += ["注释"]
                f.write(",".join(header) + "\n")
                for cid, m in d.messages.items():
                    for s in m.signals:
                        row = [m.name, "0x%X" % cid, str(m.dlc), m.sender,
                               str(m.cycle_time), s.name, str(s.start_bit),
                               str(s.bit_length), "Intel" if s.little_endian else "Motorola",
                               "signed" if s.is_signed else "unsigned",
                               str(s.factor), str(s.offset)]
                        if opt_minmax.isChecked():
                            row += [str(s.minimum), str(s.maximum)]
                        row += [s.unit]
                        if opt_nodes.isChecked():
                            row.append(" ".join(s.receivers))
                        if opt_values.isChecked():
                            row.append("; ".join("%d=%s" % kv for kv in sorted(s.value_table.items())))
                        if opt_comment.isChecked():
                            row.append(s.comment.replace(",", "，"))
                        f.write(",".join(str(x).replace(",", "，") if isinstance(x, str) else str(x) for x in row) + "\n")
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_json():
        d = _require_db()
        if d is None:
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出 JSON", "dbc_export.json",
                                              "JSON (*.json)")
        if not path:
            return
        try:
            data = {"version": d.version, "nodes": d.nodes, "messages": []}
            for cid, m in d.messages.items():
                msg = {"id": cid, "name": m.name, "dlc": m.dlc, "sender": m.sender,
                       "cycle_time": m.cycle_time,
                       "comment": m.comment if opt_comment.isChecked() else "",
                       "signals": []}
                for s in m.signals:
                    sig = {"name": s.name, "start_bit": s.start_bit,
                           "bit_length": s.bit_length,
                           "byte_order": "intel" if s.little_endian else "motorola",
                           "signed": s.is_signed, "factor": s.factor,
                           "offset": s.offset, "unit": s.unit}
                    if opt_minmax.isChecked():
                        sig["min"] = s.minimum
                        sig["max"] = s.maximum
                    if opt_nodes.isChecked():
                        sig["receivers"] = s.receivers
                    if opt_values.isChecked() and s.value_table:
                        sig["values"] = {str(k): v for k, v in s.value_table.items()}
                    if opt_comment.isChecked() and s.comment:
                        sig["comment"] = s.comment
                    msg["signals"].append(sig)
                data["messages"].append(msg)
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_html():
        d = _require_db()
        if d is None:
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出 HTML", "dbc_export.html",
                                              "HTML (*.html)")
        if not path:
            return
        try:
            html = ["<html><head><meta charset='utf-8'><title>DBC 信号矩阵</title>",
                    "<style>table{border-collapse:collapse;width:100%;font-size:12px}",
                    "th,td{border:1px solid #999;padding:4px}th{background:#eee}",
                    "tr:nth-child(even){background:#fafafa}</style></head><body>",
                    "<h1>信号矩阵</h1><p>生成: %s · %d 报文</p>" % (
                        time.strftime("%Y-%m-%d %H:%M:%S"), len(d.messages))]
            for cid, m in d.messages.items():
                html.append("<h3>%s (0x%X, DLC %d, %s)</h3>" % (m.name, cid, m.dlc, m.sender))
                if opt_comment.isChecked() and m.comment:
                    html.append("<p>%s</p>" % m.comment)
                html.append("<table><tr><th>信号</th><th>位</th><th>长度</th>"
                            "<th>factor</th><th>offset</th><th>单位</th>")
                if opt_minmax.isChecked():
                    html.append("<th>min</th><th>max</th>")
                if opt_values.isChecked():
                    html.append("<th>值表</th>")
                if opt_comment.isChecked():
                    html.append("<th>注释</th>")
                html.append("</tr>")
                for s in m.signals:
                    html.append("<tr><td>%s</td><td>%d@%s</td><td>%d</td><td>%g</td>"
                                "<td>%g</td><td>%s</td>" % (
                                    s.name, s.start_bit,
                                    "i" if s.little_endian else "m",
                                    s.bit_length, s.factor, s.offset, s.unit))
                    if opt_minmax.isChecked():
                        html.append("<td>%g</td><td>%g</td>" % (s.minimum, s.maximum))
                    if opt_values.isChecked():
                        html.append("<td>%s</td>" % "; ".join(
                            "%d=%s" % kv for kv in sorted(s.value_table.items())))
                    if opt_comment.isChecked():
                        html.append("<td>%s</td>" % (s.comment or ""))
                    html.append("</tr>")
                html.append("</table>")
            html.append("</body></html>")
            with open(path, "w", encoding="utf-8") as f:
                f.write("\n".join(html))
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("dbcExporter.open", _on_open_cmd, "数据库: DBC 导出")

    load_btn.clicked.connect(_on_load)
    csv_btn.clicked.connect(_on_csv)
    json_btn.clicked.connect(_on_json)
    html_btn.clicked.connect(_on_html)
    for chk in (opt_comment, opt_values, opt_nodes, opt_minmax):
        chk.stateChanged.connect(_refresh)

    win.show()
    sin.output.append("DBC 导出插件已加载（CSV/JSON/HTML 信号矩阵，离线工具）")


def deactivate():
    sin.output.append("DBC 导出插件已停用")
