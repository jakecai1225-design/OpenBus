# -*- coding: utf-8 -*-
"""can-id-scanner — Live CAN ID discovery + optional DBC audit.

Real-time ID map (std/ext, count, Hz, DLC), DBC undefined/missing audit, CSV export.
Subscribe-only (read-safe).
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QMessageBox, QHeaderView, QTabWidget,
)

import sin
from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "can-id-scanner"

_ids = {}
_dbc = None
_dbc_path = ""
_total = 0
_win = None


def _on_frame(frame):
    global _total
    _total += 1
    st = _ids.get(frame.id)
    now = time.time()
    if st is None:
        _ids[frame.id] = {
            "count": 1, "extended": frame.extended,
            "dlc": frame.dlc, "first": now, "last": now,
        }
    else:
        st["count"] += 1
        st["last"] = now
        if frame.extended:
            st["extended"] = True


def activate(context):
    global _dbc, _dbc_path, _win, _total
    _dbc = None
    _dbc_path = ""
    _ids.clear()
    _total = 0

    win = sin.ui.create_window("CAN ID Scanner")
    win.resize(980, 640)
    plugin_shell.attach_status_bar(win, "Listening…")
    _win = win

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    dbc_btn = QPushButton("Load DBC…")
    audit_btn = QPushButton("Run audit")
    export_btn = QPushButton("Export CSV")
    clear_btn = QPushButton("Clear")
    top.addWidget(dbc_btn)
    top.addWidget(audit_btn)
    top.addStretch(1)
    top.addWidget(export_btn)
    top.addWidget(clear_btn)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Live ID map from the bus (subscribe-only). Optional DBC audit finds IDs "
        "on the bus but not in the database, and vice versa."))

    summary = QLabel("Waiting for frames…")
    summary.setStyleSheet("font-weight:bold;")
    layout.addWidget(summary)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    id_tab = QWidget()
    iv = QVBoxLayout(id_tab)
    tree = QTreeWidget()
    tree.setHeaderLabels([
        "ID", "Type", "Frames", "Hz", "DLC", "DBC message", "First", "Last"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    iv.addWidget(tree, 1)
    tabs.addTab(id_tab, "ID map")

    audit_tab = QWidget()
    av = QVBoxLayout(audit_tab)
    audit_tree = QTreeWidget()
    audit_tree.setHeaderLabels(["Category", "ID", "DBC name", "Cycle (db)", "Note"])
    audit_tree.setRootIsDecorated(False)
    audit_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    av.addWidget(audit_tree, 1)
    tabs.addTab(audit_tab, "DBC audit")

    def _persist():
        state_store.save_state(PLUGIN_ID, {"dbc_path": _dbc_path})

    def _refresh():
        now = time.time()
        if not _ids:
            summary.setText("Waiting for frames…")
            tree.clear()
            return
        elapsed = max(0.001, now - min(st["first"] for st in _ids.values()))
        std = sum(1 for st in _ids.values() if not st["extended"])
        ext = len(_ids) - std
        summary.setText(
            "Frames %d · IDs %d (std %d / ext %d) · observed %.0fs"
            % (_total, len(_ids), std, ext, elapsed))
        tree.clear()
        for cid, st in sorted(_ids.items(), key=lambda kv: -kv[1]["count"]):
            freq = st["count"] / max(0.001, now - st["first"])
            dbc_name = "-"
            if _dbc and cid in _dbc.messages:
                dbc_name = _dbc.messages[cid].name
            item = QTreeWidgetItem([
                "0x%X" % cid,
                "ext" if st["extended"] else "std",
                str(st["count"]), "%.1f" % freq, str(st["dlc"]), dbc_name,
                time.strftime("%H:%M:%S", time.localtime(st["first"])),
                time.strftime("%H:%M:%S", time.localtime(st["last"])),
            ])
            if _dbc is not None and cid not in _dbc.messages:
                item.setBackground(5, QColor("#ef6c00"))
            tree.addTopLevelItem(item)

    def _on_load_dbc():
        global _dbc, _dbc_path
        path = dbc_picker.pick_dbc(win, "Select DBC for audit")
        if not path:
            return
        _dbc = dbcparse.parse_file(path)
        _dbc_path = path
        _persist()
        plugin_shell.set_status(
            win, "DBC loaded (%d messages)" % len(_dbc.messages), 3000)
        _refresh()

    def _on_audit():
        if _dbc is None:
            QMessageBox.information(win, "Audit", "Load a DBC first")
            return
        audit_tree.clear()
        bus_ids = set(_ids.keys())
        dbc_ids = set(_dbc.messages.keys())
        undefined = sorted(bus_ids - dbc_ids)
        missing = sorted(dbc_ids - bus_ids)
        for cid in undefined:
            st = _ids[cid]
            audit_tree.addTopLevelItem(QTreeWidgetItem([
                "On bus / not in DBC", "0x%X" % cid, "-", "-",
                "%d frames, %.1f Hz" % (
                    st["count"],
                    st["count"] / max(0.001, time.time() - st["first"]))]))
        for cid in missing:
            m = _dbc.messages[cid]
            audit_tree.addTopLevelItem(QTreeWidgetItem([
                "In DBC / not on bus", "0x%X" % cid, m.name,
                str(m.cycle_time),
                "Expected cycle %d ms not observed" % m.cycle_time]))
        audit_tree.sortByColumn(0, Qt.SortOrder.AscendingOrder)
        tabs.setCurrentIndex(1)
        plugin_shell.set_status(
            win,
            "Audit: %d undefined, %d missing" % (len(undefined), len(missing)),
            4000)

    def _on_export():
        if not _ids:
            QMessageBox.information(win, "Export", "No IDs yet")
            return
        now = time.time()
        rows = []
        for cid, st in sorted(_ids.items(), key=lambda kv: -kv[1]["count"]):
            dbc_name = (
                _dbc.messages[cid].name if (_dbc and cid in _dbc.messages) else "")
            rows.append([
                "0x%X" % cid,
                "ext" if st["extended"] else "std",
                st["count"],
                "%.1f" % (st["count"] / max(0.001, now - st["first"])),
                st["dlc"], dbc_name,
                time.strftime("%H:%M:%S", time.localtime(st["first"])),
                time.strftime("%H:%M:%S", time.localtime(st["last"])),
            ])
        path = plugin_shell.export_csv(
            win,
            ["ID", "Type", "Frames", "Hz", "DLC", "DBC", "First", "Last"],
            rows,
            "id_scan.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    def _on_clear():
        global _total
        _ids.clear()
        _total = 0
        tree.clear()
        audit_tree.clear()
        summary.setText("Waiting for frames…")
        plugin_shell.set_status(win, "Cleared", 2000)

    dbc_btn.clicked.connect(_on_load_dbc)
    audit_btn.clicked.connect(_on_audit)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)
    plugin_shell.bind_shortcut(win, "Ctrl+E", _on_export)

    context.on_frame(_on_frame)
    context.register_command(
        "canIdScanner.open", plugin_shell.bind_raise(win), "Tools: ID Scanner")

    saved = state_store.load_state(PLUGIN_ID, default={}) or {}
    p = saved.get("dbc_path") or ""
    if p:
        try:
            import os
            if os.path.isfile(p):
                _dbc = dbcparse.parse_file(p)
                _dbc_path = p
        except OSError:
            pass

    timer = QTimer(win)
    timer.timeout.connect(_refresh)
    timer.start(500)

    win.show()
    sin.output.append("can-id-scanner loaded (live ID map + optional DBC audit)")


def deactivate():
    global _win
    _win = None
    try:
        sin.output.append("can-id-scanner deactivated")
    except Exception:
        pass
