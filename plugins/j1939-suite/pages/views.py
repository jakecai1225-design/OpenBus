# -*- coding: utf-8 -*-
"""Shared live tables fed by the J1939 analyzer engine."""

from __future__ import annotations

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import (
    QHeaderView,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pages import analyzer


def _host(parent, headers, fill):
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    tree = QTreeWidget()
    tree.setHeaderLabels(headers)
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    def _tick():
        tree.clear()
        for cols in fill():
            tree.addTopLevelItem(QTreeWidgetItem([str(c) for c in cols]))

    timer = QTimer(root)
    timer.timeout.connect(_tick)
    timer.start(500)
    _tick()
    return root


def build_transport(parent, session, log_fn) -> QWidget:
    def fill():
        rows = []
        for ts, sa, da, pgn, payload, kind in analyzer.snapshot_tp():
            rows.append((
                "%.3f" % ts, kind, "%02X" % sa, "%02X" % da,
                "0x%04X" % pgn, len(payload),
                " ".join("%02X" % b for b in payload[:24]),
            ))
        return rows

    return _host(
        parent,
        ["Time", "Kind", "SA", "DA", "PGN", "Bytes", "Payload"],
        fill,
    )


def build_diagnostics(parent, session, log_fn) -> QWidget:
    def fill():
        rows = []
        for ts, pgn, sa, spn, text, lamps in analyzer.snapshot_dm():
            rows.append((
                "%.3f" % ts,
                "DM1" if pgn == 0xFECA else "DM2",
                "%02X" % sa,
                "" if spn is None else str(spn),
                text,
                lamps,
            ))
        return rows

    return _host(
        parent,
        ["Time", "PGN", "SA", "SPN", "DTC", "Lamps"],
        fill,
    )


def build_network(parent, session, log_fn) -> QWidget:
    def fill():
        rows = []
        for sa, claim in analyzer.snapshot_addr():
            rows.append((
                "%02X" % sa,
                claim.get("name_hex", ""),
                claim.get("manufacturer", ""),
                claim.get("function", ""),
                claim.get("function_name", ""),
            ))
        return rows

    return _host(
        parent,
        ["SA", "NAME", "Manufacturer", "Function", "Name"],
        fill,
    )
