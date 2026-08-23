# -*- coding: utf-8 -*-
"""dbc-diff 插件 — DBC 差异对比（canmatrix compare 风格）
功能：
- 双 DBC 报文/信号/属性级 diff（新增/删除/修改三态）
- 差异树视图（报文→信号嵌套）
- 统计（新增/删除/修改报文数、信号数）
- 差异报告导出 CSV
- 纯离线工具（无需总线）
依赖: pip install PyQt6
"""

import csv
import time

import sin
import dbcparse


def _sig_changes(a, b):
    """信号属性级差异"""
    diffs = []
    for attr in ("start_bit", "bit_length", "little_endian", "is_signed",
                 "factor", "offset", "minimum", "maximum", "unit"):
        va, vb = getattr(a, attr), getattr(b, attr)
        if va != vb:
            diffs.append("%s: %s → %s" % (attr, va, vb))
    return diffs


def diff_dbc(db_a, db_b):
    """返回 (msg_tree_rows, stats)
    rows: (kind, id, name, detail, children:[(kind, name, detail)])"""
    rows = []
    stats = {"msg_new": 0, "msg_del": 0, "msg_mod": 0, "sig_new": 0,
             "sig_del": 0, "sig_mod": 0}
    ids_a = set(db_a.messages.keys())
    ids_b = set(db_b.messages.keys())
    for cid in sorted(ids_a | ids_b):
        ma = db_a.messages.get(cid)
        mb = db_b.messages.get(cid)
        if ma and not mb:
            rows.append(("删除", "0x%X" % cid, ma.name, "仅在 A 中（%d 信号）" % len(ma.signals), []))
            stats["msg_del"] += 1
            stats["sig_del"] += len(ma.signals)
        elif mb and not ma:
            rows.append(("新增", "0x%X" % cid, mb.name, "仅在 B 中（%d 信号）" % len(mb.signals), []))
            stats["msg_new"] += 1
            stats["sig_new"] += len(mb.signals)
        else:
            children = []
            # 报文级差异
            msg_diffs = []
            if ma.name != mb.name:
                msg_diffs.append("名称: %s → %s" % (ma.name, mb.name))
            if ma.dlc != mb.dlc:
                msg_diffs.append("DLC: %d → %d" % (ma.dlc, mb.dlc))
            if ma.sender != mb.sender:
                msg_diffs.append("发送节点: %s → %s" % (ma.sender, mb.sender))
            if ma.cycle_time != mb.cycle_time:
                msg_diffs.append("周期: %d → %d" % (ma.cycle_time, mb.cycle_time))
            # 信号级
            sig_a = {s.name: s for s in ma.signals}
            sig_b = {s.name: s for s in mb.signals}
            for name in sorted(set(sig_a) | set(sig_b)):
                sa = sig_a.get(name)
                sb = sig_b.get(name)
                if sa and not sb:
                    children.append(("删除", name, "仅 A"))
                    stats["sig_del"] += 1
                elif sb and not sa:
                    children.append(("新增", name, "仅 B"))
                    stats["sig_new"] += 1
                else:
                    d = _sig_changes(sa, sb)
                    if d:
                        children.append(("修改", name, "; ".join(d)))
                        stats["sig_mod"] += 1
            if msg_diffs or children:
                rows.append(("修改", "0x%X" % cid, ma.name,
                             "; ".join(msg_diffs) or "信号变化", children))
                stats["msg_mod"] += 1
    return rows, stats


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("DBC 对比插件需要 PyQt6: pip install PyQt6")
        return

    db_a = {"file": None}
    db_b = {"file": None}

    win = sin.ui.create_window("DBC 差异对比")
    win.resize(980, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    load_a_btn = QPushButton("加载 A…")
    load_b_btn = QPushButton("加载 B…")
    diff_btn = QPushButton("执行对比")
    export_btn = QPushButton("导出报告 CSV")
    top.addWidget(load_a_btn)
    top.addWidget(load_b_btn)
    top.addStretch(1)
    top.addWidget(diff_btn)
    top.addWidget(export_btn)
    layout.addLayout(top)

    label_a = QLabel("A: 未加载")
    label_b = QLabel("B: 未加载")
    label_a.setStyleSheet("color:#888;")
    label_b.setStyleSheet("color:#888;")
    layout.addWidget(label_a)
    layout.addWidget(label_b)

    summary = QLabel("加载两个 DBC 后点击「执行对比」")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tree = QTreeWidget()
    tree.setHeaderLabels(["状态", "ID", "名称", "差异详情"])
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    results = {"rows": [], "stats": {}}

    def _load(side, label):
        path, _ = QFileDialog.getOpenFileName(win, "加载 DBC %s" % side, "",
                                              "DBC 文件 (*.dbc);;所有文件 (*)")
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "加载失败", "无报文定义: %s" % path)
            return
        db_a if side == "A" else db_b
        if side == "A":
            db_a["file"] = db
            label_a.setText("A: %s（%d 报文）" % (path.split("\\")[-1], len(db.messages)))
            label_a.setStyleSheet("color:#2e7d32;")
        else:
            db_b["file"] = db
            label_b.setText("B: %s（%d 报文）" % (path.split("\\")[-1], len(db.messages)))
            label_b.setStyleSheet("color:#2e7d32;")

    def _on_diff():
        if not db_a["file"] or not db_b["file"]:
            QMessageBox.information(win, "提示", "请先加载 DBC A 和 B")
            return
        rows, stats = diff_dbc(db_a["file"], db_b["file"])
        results["rows"] = rows
        results["stats"] = stats
        tree.clear()
        colors = {"新增": QColor("#2e7d32"), "删除": QColor("#c62828"), "修改": QColor("#ef6c00")}
        for kind, cid, name, detail, children in rows:
            item = QTreeWidgetItem([kind, cid, name, detail])
            item.setForeground(0, colors.get(kind, QColor("black")))
            tree.addTopLevelItem(item)
            for ckind, cname, cdetail in children:
                child = QTreeWidgetItem([ckind, "", cname, cdetail])
                child.setForeground(0, colors.get(ckind, QColor("black")))
                item.addChild(child)
        if not rows:
            summary.setText("两个 DBC 完全一致（报文/信号/属性级无差异）")
        else:
            s = stats
            summary.setText("差异: 报文 新增%d/删除%d/修改%d · 信号 新增%d/删除%d/修改%d" % (
                s["msg_new"], s["msg_del"], s["msg_mod"],
                s["sig_new"], s["sig_del"], s["sig_mod"]))

    def _on_export():
        if not results["rows"]:
            QMessageBox.information(win, "提示", "请先执行对比")
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出差异报告", "dbc_diff.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig", newline="") as f:
                w = csv.writer(f)
                w.writerow(["级别", "状态", "ID", "名称", "差异详情"])
                for kind, cid, name, detail, children in results["rows"]:
                    w.writerow(["报文", kind, cid, name, detail])
                    for ckind, cname, cdetail in children:
                        w.writerow(["信号", ckind, cid, cname, cdetail])
            QMessageBox.information(win, "导出成功", "已导出到:\n%s" % path)
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("dbcDiff.open", _on_open_cmd, "数据库: DBC 对比")

    load_a_btn.clicked.connect(lambda: _load("A", label_a))
    load_b_btn.clicked.connect(lambda: _load("B", label_b))
    diff_btn.clicked.connect(_on_diff)
    export_btn.clicked.connect(_on_export)

    win.show()
    sin.output.append("DBC 对比插件已加载（报文/信号/属性级 diff，离线工具）")


def deactivate():
    sin.output.append("DBC 对比插件已停用")
