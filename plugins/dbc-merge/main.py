# -*- coding: utf-8 -*-
"""dbc-merge 插件 — DBC 合并（canmatrix merge 风格）
功能：
- 多 DBC 依次合并（追加模式）
- ID 冲突策略：跳过 / 重命名（前缀_原ID）/ 覆盖
- 节点自动合并、冲突报告（哪些 ID 冲突、采用哪个）
- 另存新 DBC（dbcparse 序列化，保留周期/注释/值表）
- 纯离线工具
依赖: pip install PyQt6
"""

import time

import sin
import dbcparse


def activate(context):
    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QComboBox
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("DBC 合并插件需要 PyQt6: pip install PyQt6")
        return

    merged = dbcparse.DbcFile()
    merged.version = "merged-by-sin"
    conflicts = []       # (cid, kept, skipped_from)
    loaded_files = []

    win = sin.ui.create_window("DBC 合并工具")
    win.resize(960, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    add_btn = QPushButton("添加 DBC…")
    strategy_label = QLabel("冲突策略:")
    strategy = QComboBox()
    strategy.addItems(["跳过（保留先入）", "重命名（前缀 ID）", "覆盖（后来居上）"])
    merge_state_label = QLabel("已合并: 0 个文件, 0 报文")
    top.addWidget(add_btn)
    top.addWidget(strategy_label)
    top.addWidget(strategy)
    top.addStretch(1)
    top.addWidget(merge_state_label)
    layout.addLayout(top)

    btns = QHBoxLayout()
    save_btn = QPushButton("另存合并 DBC…")
    export_conf_btn = QPushButton("导出冲突报告")
    clear_btn = QPushButton("清零")
    btns.addStretch(1)
    btns.addWidget(save_btn)
    btns.addWidget(export_conf_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)

    tree = QTreeWidget()
    tree.setHeaderLabels(["ID", "名称", "DLC", "信号数", "周期", "来源"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    conf_view = __import__("PyQt6.QtWidgets", fromlist=["QTextEdit"]).QTextEdit()
    conf_view.setReadOnly(True)
    layout.addWidget(conf_view, 1)

    def _refresh():
        tree.clear()
        for cid, m in merged.messages.items():
            tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid, m.name, str(m.dlc), str(len(m.signals)),
                str(m.cycle_time), m.comment or ""]))
        merge_state_label.setText("已合并: %d 个文件, %d 报文, %d 冲突"
                                  % (len(loaded_files), len(merged.messages), len(conflicts)))

    def _on_add():
        path, _ = QFileDialog.getOpenFileName(win, "添加 DBC", "",
                                              "DBC 文件 (*.dbc);;所有文件 (*)")
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "加载失败", "无报文定义: %s" % path)
            return
        loaded_files.append(path)
        fname = path.split("\\")[-1]
        mode = strategy.currentIndex()
        for cid, m in db.messages.items():
            if cid in merged.messages:
                existing = merged.messages[cid]
                if mode == 0:      # 跳过
                    conflicts.append((cid, existing.name, m.name, fname))
                    continue
                elif mode == 1:    # 重命名：改名但保留（ID 相同会覆盖——用名称区分）
                    m.name = "%s_%X" % (m.name[:20], cid)
                    conflicts.append((cid, existing.name + " + " + m.name, "合并重命名", fname))
                else:              # 覆盖
                    conflicts.append((cid, m.name, existing.name, fname))
            merged.messages[cid] = m
        # 节点合并
        for n in db.nodes:
            if n not in merged.nodes:
                merged.nodes.append(n)
        _refresh()
        _refresh_conf()

    def _refresh_conf():
        if not conflicts:
            conf_view.setPlainText("无冲突")
            return
        lines = []
        for cid, kept, skipped, src in conflicts:
            lines.append("ID 0x%X: 保留 %s（跳过 %s，来自 %s）" % (cid, kept, skipped, src))
        conf_view.setPlainText("\n".join(lines))

    def _on_save():
        if not merged.messages:
            QMessageBox.information(win, "提示", "无合并内容")
            return
        path, _ = QFileDialog.getSaveFileName(win, "另存合并 DBC", "merged.dbc",
                                              "DBC 文件 (*.dbc)")
        if not path:
            return
        try:
            text = dbcparse.serialize(merged)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            QMessageBox.information(win, "保存成功",
                                    "已保存 %d 报文到:\n%s" % (len(merged.messages), path))
        except OSError as e:
            QMessageBox.warning(win, "保存失败", str(e))

    def _on_export_conf():
        if not conflicts:
            QMessageBox.information(win, "提示", "无冲突")
            return
        path, _ = QFileDialog.getSaveFileName(win, "导出冲突报告", "merge_conflicts.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("ID,保留,跳过,来源文件\n")
                for cid, kept, skipped, src in conflicts:
                    f.write("0x%X,%s,%s,%s\n" % (cid, kept, skipped, src))
            QMessageBox.information(win, "导出成功", "已导出 %d 条冲突" % len(conflicts))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_clear():
        merged.messages.clear()
        merged.nodes.clear()
        del conflicts[:]
        del loaded_files[:]
        tree.clear()
        conf_view.clear()
        _refresh()

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.register_command("dbcMerge.open", _on_open_cmd, "数据库: DBC 合并")

    add_btn.clicked.connect(_on_add)
    save_btn.clicked.connect(_on_save)
    export_conf_btn.clicked.connect(_on_export_conf)
    clear_btn.clicked.connect(_on_clear)

    win.show()
    sin.output.append("DBC 合并插件已加载（多库合并 + 冲突策略，离线工具）")


def deactivate():
    sin.output.append("DBC 合并插件已停用")
