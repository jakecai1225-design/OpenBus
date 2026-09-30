# -*- coding: utf-8 -*-
"""ID Scan workspace — live CAN ID discovery + optional DBC audit."""

from __future__ import annotations

import os
import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QHBoxLayout, QHeaderView, QLabel, QMessageBox, QPushButton, QTabWidget,
    QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
)

from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PAGE_STATE = "id_scan.json"


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    ids = {}
    state = {"dbc": None, "dbc_path": "", "total": 0}

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
        state_store.save_state("log-analysis", {
            "dbc_path": state["dbc_path"],
        }, PAGE_STATE)

    def _on_frame(frame):
        state["total"] += 1
        st = ids.get(frame.id)
        now = time.time()
        if st is None:
            ids[frame.id] = {
                "count": 1, "extended": frame.extended,
                "dlc": frame.dlc, "first": now, "last": now,
            }
        else:
            st["count"] += 1
            st["last"] = now
            if frame.extended:
                st["extended"] = True

    def _refresh():
        now = time.time()
        if not ids:
            summary.setText("Waiting for frames…")
            tree.clear()
            return
        elapsed = max(0.001, now - min(st["first"] for st in ids.values()))
        std = sum(1 for st in ids.values() if not st["extended"])
        ext = len(ids) - std
        summary.setText(
            "Frames %d · IDs %d (std %d / ext %d) · observed %.0fs"
            % (state["total"], len(ids), std, ext, elapsed))
        tree.clear()
        dbc = state["dbc"]
        for cid, st in sorted(ids.items(), key=lambda kv: -kv[1]["count"]):
            freq = st["count"] / max(0.001, now - st["first"])
            dbc_name = "-"
            if dbc and cid in dbc.messages:
                dbc_name = dbc.messages[cid].name
            item = QTreeWidgetItem([
                "0x%X" % cid,
                "ext" if st["extended"] else "std",
                str(st["count"]), "%.1f" % freq, str(st["dlc"]), dbc_name,
                time.strftime("%H:%M:%S", time.localtime(st["first"])),
                time.strftime("%H:%M:%S", time.localtime(st["last"])),
            ])
            if dbc is not None and cid not in dbc.messages:
                item.setBackground(5, QColor("#ef6c00"))
            tree.addTopLevelItem(item)

    def _on_load_dbc():
        path = dbc_picker.pick_dbc(parent, "Select DBC for audit")
        if not path:
            return
        state["dbc"] = dbcparse.parse_file(path)
        state["dbc_path"] = path
        session.note_dbc_path(path)
        _persist()
        plugin_shell.set_status(
            parent, "DBC loaded (%d messages)" % len(state["dbc"].messages),
            3000)
        _refresh()

    def _on_audit():
        dbc = state["dbc"]
        if dbc is None:
            QMessageBox.information(parent, "Audit", "Load a DBC first")
            return
        audit_tree.clear()
        bus_ids = set(ids.keys())
        dbc_ids = set(dbc.messages.keys())
        undefined = sorted(bus_ids - dbc_ids)
        missing = sorted(dbc_ids - bus_ids)
        for cid in undefined:
            st = ids[cid]
            audit_tree.addTopLevelItem(QTreeWidgetItem([
                "On bus / not in DBC", "0x%X" % cid, "-", "-",
                "%d frames, %.1f Hz" % (
                    st["count"],
                    st["count"] / max(0.001, time.time() - st["first"]))]))
        for cid in missing:
            m = dbc.messages[cid]
            audit_tree.addTopLevelItem(QTreeWidgetItem([
                "In DBC / not on bus", "0x%X" % cid, m.name,
                str(m.cycle_time),
                "Expected cycle %d ms not observed" % m.cycle_time]))
        audit_tree.sortByColumn(0, Qt.SortOrder.AscendingOrder)
        tabs.setCurrentIndex(1)
        log_fn("RX", "-", b"",
               "ID audit: %d undefined, %d missing"
               % (len(undefined), len(missing)))
        plugin_shell.set_status(
            parent,
            "Audit: %d undefined, %d missing" % (len(undefined), len(missing)),
            4000)

    def _on_export():
        if not ids:
            QMessageBox.information(parent, "Export", "No IDs yet")
            return
        now = time.time()
        dbc = state["dbc"]
        rows = []
        for cid, st in sorted(ids.items(), key=lambda kv: -kv[1]["count"]):
            dbc_name = (
                dbc.messages[cid].name if (dbc and cid in dbc.messages) else "")
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
            parent,
            ["ID", "Type", "Frames", "Hz", "DLC", "DBC", "First", "Last"],
            rows,
            "id_scan.csv",
        )
        if path:
            session.note_export_path(path)
            plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    def _on_clear():
        ids.clear()
        state["total"] = 0
        tree.clear()
        audit_tree.clear()
        summary.setText("Waiting for frames…")
        plugin_shell.set_status(parent, "Cleared", 2000)

    dbc_btn.clicked.connect(_on_load_dbc)
    audit_btn.clicked.connect(_on_audit)
    export_btn.clicked.connect(_on_export)
    clear_btn.clicked.connect(_on_clear)

    session.on_bus_frame(_on_frame)

    saved = state_store.load_state("log-analysis", PAGE_STATE) or {}
    p = saved.get("dbc_path") or session.last_dbc_path or ""
    if p and os.path.isfile(p):
        try:
            state["dbc"] = dbcparse.parse_file(p)
            state["dbc_path"] = p
            session.note_dbc_path(p)
        except OSError:
            pass

    timer = QTimer(root)
    timer.timeout.connect(_refresh)
    timer.start(500)

    return root
