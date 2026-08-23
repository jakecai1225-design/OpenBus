# -*- coding: utf-8 -*-
"""dbc-lint 插件 — DBC 静态检查（lint）
功能：
- 命名规范（非法字符/保留字/长度）
- 信号重叠检测（同报文内 start_bit+length 覆盖区间重叠）
- 信号越界（start_bit+length > DLC*8）
- min/max 与 factor/offset 一致性（原始值范围反算）
- 缺周期属性、缺注释、值表覆盖检查
- 问题分级：错误 / 警告 / 提示；报告导出 CSV
- 纯离线工具
依赖: pip install PyQt6
"""

import csv
import re
import time

import sin
import dbcparse

RESERVED = {"class", "struct", "union", "enum", "typedef", "static", "const",
            "void", "int", "long", "short", "float", "double", "char", "if",
            "else", "for", "while", "switch", "case", "return", "break"}


def lint_dbc(db):
    """返回 [(severity, location, rule, message)]"""
    issues = []

    def _range(sig):
        if sig.little_endian:
            return (sig.start_bit, sig.start_bit + sig.bit_length - 1)
        # Motorola 粗略估计（从 MSB 开始跨字节）
        end = sig.start_bit + sig.bit_length - 1
        return (sig.start_bit, end)

    for cid, m in db.messages.items():
        loc = "报文 0x%X %s" % (cid, m.name)
        # 命名
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", m.name):
            issues.append(("错误", loc, "命名规范", "报文名含非法字符: %r" % m.name))
        if m.name.lower() in RESERVED:
            issues.append(("错误", loc, "保留字", "报文名为 C 保留字: %s" % m.name))
        # 周期
        if m.cycle_time == 0:
            issues.append(("提示", loc, "缺周期", "未定义 GenMsgCycleTime（事件报文？）"))
        # 注释
        if not m.comment:
            issues.append(("提示", loc, "缺注释", "报文无注释说明"))

        # 信号级
        bit_used = {}   # bit -> sig name
        for s in m.signals:
            sloc = "%s / 信号 %s" % (loc, s.name)
            if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", s.name):
                issues.append(("错误", sloc, "命名规范", "信号名含非法字符"))
            if s.name.lower() in RESERVED:
                issues.append(("警告", sloc, "保留字", "信号名为 C 保留字"))
            # 越界
            bits_available = m.dlc * 8
            if s.start_bit + s.bit_length > bits_available:
                issues.append(("错误", sloc, "信号越界",
                               "start %d + len %d > DLC %d×8" % (
                                   s.start_bit, s.bit_length, m.dlc)))
            # 重叠
            lo, hi = _range(s)
            for b in range(lo, hi + 1):
                if b in bit_used:
                    issues.append(("错误", sloc, "信号重叠",
                                   "bit %d 与信号 %s 重叠" % (b, bit_used[b])))
                    break
                bit_used[b] = s.name
            # min/max 与 factor/offset 一致性
            if s.factor and s.factor != 0:
                raw_min = 0 if not s.is_signed else -(1 << (s.bit_length - 1)) if s.bit_length > 1 else -1
                raw_max = (1 << s.bit_length) - 1 if not s.is_signed else (1 << (s.bit_length - 1)) - 1
                phys_min = raw_min * s.factor + s.offset
                phys_max = raw_max * s.factor + s.offset
                if s.minimum < phys_min - abs(phys_min) * 0.01 or s.minimum > phys_max + abs(phys_max) * 0.01:
                    issues.append(("警告", sloc, "min 越界",
                                   "min %g 超出可表示范围 [%g, %g]" % (
                                       s.minimum, phys_min, phys_max)))
                if s.maximum > phys_max + abs(phys_max) * 0.01:
                    issues.append(("警告", sloc, "max 越界",
                                   "max %g 超出可表示范围 [%g, %g]" % (
                                       s.maximum, phys_min, phys_max)))
                if s.minimum > s.maximum:
                    issues.append(("错误", sloc, "区间颠倒", "min %g > max %g" % (
                        s.minimum, s.maximum)))
            # 值表
            if s.value_table:
                for val, desc in s.value_table.items():
                    if val < s.minimum or val > s.maximum:
                        issues.append(("警告", sloc, "值表越界",
                                       "值 %d 超出 [%g, %g]" % (val, s.minimum, s.maximum)))
            elif s.bit_length <= 2:
                issues.append(("提示", sloc, "缺值表", "≤2 位信号建议定义值表"))

        # 解析警告透传
    for w in db.warnings[:50]:
        issues.append(("警告", "文件", "解析告警", w))

    return issues


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("DBC 检查插件需要 PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("DBC 静态检查 (Lint)")
    win.resize(960, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_btn = QPushButton("加载并检查 DBC…")
    export_btn = QPushButton("导出报告 CSV")
    top.addWidget(load_btn)
    top.addStretch(1)
    top.addWidget(export_btn)
    layout.addLayout(top)

    summary = QLabel("加载 DBC 后自动执行全量检查")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["级别", "位置", "规则", "说明"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    def _on_load():
        path, _ = QFileDialog.getOpenFileName(win, "加载 DBC", "", "DBC 文件 (*.dbc)")
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "加载失败", "无报文定义")
            return
        issues = lint_dbc(db)
        tree.clear()
        sev_color = {"错误": QColor("#c62828"), "警告": QColor("#ef6c00"), "提示": QColor("#1565c0")}
        for sev, loc, rule, msg in issues:
            item = QTreeWidgetItem([sev, loc, rule, msg])
            item.setForeground(0, sev_color.get(sev))
            tree.addTopLevelItem(item)
        n_err = sum(1 for i in issues if i[0] == "错误")
        n_warn = sum(1 for i in issues if i[0] == "警告")
        n_info = sum(1 for i in issues if i[0] == "提示")
        summary.setText("%s: %d 报文 · 错误 %d · 警告 %d · 提示 %d%s" % (
            path.split("\\")[-1], len(db.messages), n_err, n_warn, n_info,
            " · ✓ 无错误" if n_err == 0 else " · ✗ 存在错误"))
        tree.sortByColumn(0, __import__("PyQt6.QtCore", fromlist=["Qt"]).Qt.SortOrder.AscendingOrder)
        win._issues_store = issues

    def _on_export():
        issues = getattr(win, "_issues_store", [])
        if not issues:
            QMessageBox.information(win, "提示", "请先执行检查")
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出检查报告", "dbc_lint.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = csv.writer(f)
                w.writerow(["级别", "位置", "规则", "说明"])
                for row in issues:
                    w.writerow(row)
            QMessageBox.information(win, "导出成功", "已导出 %d 条" % len(issues))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("dbcLint.open", _on_open_cmd, "数据库: DBC 检查")

    load_btn.clicked.connect(_on_load)
    export_btn.clicked.connect(_on_export)

    # 动态挂 issues（QTreeWidgetItem 不可 setattr，挂在 wrapper dict）
    win._issues_store = []

    win.show()
    sin.output.append("DBC 静态检查插件已加载（命名/重叠/越界/一致性，离线工具）")


def deactivate():
    sin.output.append("DBC 静态检查插件已停用")
