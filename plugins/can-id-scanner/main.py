# -*- coding: utf-8 -*-
"""can-id-scanner 插件 — CAN ID 发现与 DBC 增量审计
功能：
- 实时 ID 发现（标准/扩展分类、帧数、频率、DLC、首见/末见时间）
- Top Talker 排名（帧数/占比）
- DBC 增量审计：总线上有而库中没有的 ID（未定义）、库中有而总线上没有的 ID（未出现）
- CSV 导出；纯订阅只读（激活即订阅，安全）
依赖: pip install PyQt6
"""

import time

from PyQt6.QtCore import QTimer

import sin
import dbcparse

_ids = {}            # id -> {count, extended, dlc, first, last}
_dbc = None
_total = 0


def _on_frame(frame):
    global _total
    _total += 1
    st = _ids.get(frame.id)
    now = time.time()
    if st is None:
        _ids[frame.id] = {"count": 1, "extended": frame.extended,
                          "dlc": frame.dlc, "first": now, "last": now}
    else:
        st["count"] += 1
        st["last"] = now
        if frame.extended:
            st["extended"] = True


def activate(context):
    global _dbc
    _dbc = None

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QTabWidget
        )
        from PyQt6.QtGui import QColor
    except ImportError:
        sin.output.append("ID 扫描插件需要 PyQt6: pip install PyQt6")
        return

    _ids.clear()
    globals()["_total"] = 0

    win = sin.ui.create_window("CAN ID 扫描与审计")
    win.resize(960, 620)

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    dbc_btn = QPushButton("加载 DBC 审计…")
    audit_btn = QPushButton("执行审计")
    export_btn = QPushButton("导出 CSV")
    clear_btn = QPushButton("清零")
    top.addWidget(dbc_btn)
    top.addWidget(audit_btn)
    top.addStretch(1)
    top.addWidget(export_btn)
    top.addWidget(clear_btn)
    layout.addLayout(top)

    summary = QLabel("等待报文（订阅实时帧，只读）...")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    id_tab = QWidget()
    iv = QVBoxLayout(id_tab)
    tree = QTreeWidget()
    tree.setHeaderLabels(["ID", "类型", "帧数", "频率Hz", "DLC", "DB C 报文", "首见", "末见"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    th = tree.header()
    th.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    iv.addWidget(tree, 1)
    tabs.addTab(id_tab, "ID 发现")

    audit_tab = QWidget()
    av = QVBoxLayout(audit_tab)
    audit_tree = QTreeWidget()
    audit_tree.setHeaderLabels(["类别", "ID", "DBC 名称", "周期(库)", "说明"])
    audit_tree.setRootIsDecorated(False)
    ah = audit_tree.header()
    ah.setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    av.addWidget(audit_tree, 1)
    tabs.addTab(audit_tab, "DBC 增量审计")

    def _refresh():
        now = time.time()
        elapsed = max(0.001, now - min((st["first"] for st in _ids.values()), default=now))
        std = sum(1 for st in _ids.values() if not st["extended"])
        ext = len(_ids) - std
        summary.setText("总帧数 %d · ID %d 个（标准 %d / 扩展 %d）· 观察 %.0fs"
                        % (_total, len(_ids), std, ext, elapsed))
        tree.clear()
        rows = sorted(_ids.items(), key=lambda kv: -kv[1]["count"])
        for cid, st in rows:
            freq = st["count"] / max(0.001, now - st["first"])
            dbc_name = "-"
            if _dbc and cid in _dbc.messages:
                dbc_name = _dbc.messages[cid].name
            item = QTreeWidgetItem([
                "0x%X" % cid, "扩展" if st["extended"] else "标准",
                str(st["count"]), "%.1f" % freq, str(st["dlc"]), dbc_name,
                time.strftime("%H:%M:%S", time.localtime(st["first"])),
                time.strftime("%H:%M:%S", time.localtime(st["last"]))])
            if _dbc is not None and cid not in _dbc.messages:
                item.setBackground(5, QColor("#ef6c00"))   # 库中无
            tree.addTopLevelItem(item)

    def _on_load_dbc():
        global _dbc
        path, _ = QFileDialog.getOpenFileName(win, "加载 DBC", "", "DBC 文件 (*.dbc)")
        if not path:
            return
        _dbc = dbcparse.parse_file(path)
        QMessageBox.information(win, "加载成功",
                                "已加载 %d 报文定义" % len(_dbc.messages))
        _refresh()

    def _on_audit():
        if _dbc is None:
            QMessageBox.information(win, "提示", "请先加载 DBC")
            return
        audit_tree.clear()
        bus_ids = set(_ids.keys())
        dbc_ids = set(_dbc.messages.keys())
        undefined = sorted(bus_ids - dbc_ids)
        missing = sorted(dbc_ids - bus_ids)
        n = 0
        for cid in undefined:
            st = _ids[cid]
            audit_tree.addTopLevelItem(QTreeWidgetItem([
                "总线上有·库中无", "0x%X" % cid, "-", "-",
                "出现 %d 次，%.1f Hz" % (st["count"],
                                         st["count"] / max(0.001, time.time() - st["first"]))]))
            n += 1
        for cid in missing:
            m = _dbc.messages[cid]
            audit_tree.addTopLevelItem(QTreeWidgetItem([
                "库中有·总线上无", "0x%X" % cid, m.name,
                str(m.cycle_time), "预期周期 %dms 未观察到" % m.cycle_time]))
            n += 1
        audit_tree.sortByColumn(0, __import__("PyQt6.QtCore", fromlist=["Qt"]).Qt.SortOrder.AscendingOrder)
        QMessageBox.information(win, "审计完成",
                                "未定义 ID: %d 个\n未出现 ID: %d 个" % (len(undefined), len(missing)))

    def _on_export():
        path, _ = QFileDialog.getSaveFileName(win, "导出 ID 清单", "id_scan.csv",
                                              "CSV (*.csv)")
        if not path:
            return
        try:
            now = time.time()
            with open(path, "w", encoding="utf-8-sig") as f:
                f.write("ID,类型,帧数,频率Hz,DLC,DBC报文,首见,末见\n")
                for cid, st in sorted(_ids.items(), key=lambda kv: -kv[1]["count"]):
                    dbc_name = _dbc.messages[cid].name if (_dbc and cid in _dbc.messages) else ""
                    f.write("0x%X,%s,%d,%.1f,%d,%s,%s,%s\n" % (
                        cid, "扩展" if st["extended"] else "标准", st["count"],
                        st["count"] / max(0.001, now - st["first"]), st["dlc"],
                        dbc_name,
                        time.strftime("%H:%M:%S", time.localtime(st["first"])),
                        time.strftime("%H:%M:%S", time.localtime(st["last"]))))
            QMessageBox.information(win, "导出成功", "已导出 %d 条" % len(_ids))
        except OSError as e:
            QMessageBox.warning(win, "导出失败", str(e))

    def _on_clear():
        _ids.clear()
        globals()["_total"] = 0
        _refresh()

    def _on_open_cmd():
        win.show()
        win.raise_()
        win.activateWindow()

    context.on_frame(_on_frame)
    context.register_command("canIdScanner.open", _on_open_cmd, "分析: ID 扫描审计")

    dbc_btn.clicked.connect(_on_load_dbc)
    audit_btn.clicked.connect(_on_audit)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)

    timer = QTimer()
    timer.timeout.connect(_refresh)
    timer.start(500)

    win.show()
    sin.output.append("ID 扫描插件已加载（订阅实时帧 + DBC 增量审计，只读安全）")


def deactivate():
    sin.output.append("ID 扫描插件已停用")
