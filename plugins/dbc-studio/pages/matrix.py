# -*- coding: utf-8 -*-
"""Communications matrix — CANdb++ style (signal rows x node columns)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QBrush, QColor, QFont
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, plugin_shell


def _cells_for_signal(db, msg, sig):
    """Map node -> (role, message_name) where role is 'tx' | 'rx' | 'both'."""
    out = {}
    sender = (msg.sender or "").strip()
    if sender and sender != "Vector__XXX":
        out[sender] = ("tx", msg.name)
    for r in sig.receivers or []:
        r = (r or "").strip()
        if not r or r == "Vector__XXX":
            continue
        if r in out:
            out[r] = ("both", msg.name)
        else:
            out[r] = ("rx", msg.name)
    return out


def build_matrix_model(db):
    """Return (nodes, rows) where each row is dict with can_id, signal, msg, cells."""
    nodes = []
    seen = set()
    for n in db.nodes or []:
        n = (n or "").strip()
        if n and n != "Vector__XXX" and n not in seen:
            seen.add(n)
            nodes.append(n)
    for msg in db.messages.values():
        s = (msg.sender or "").strip()
        if s and s != "Vector__XXX" and s not in seen:
            seen.add(s)
            nodes.append(s)
        for sig in msg.signals:
            for r in sig.receivers or []:
                r = (r or "").strip()
                if r and r != "Vector__XXX" and r not in seen:
                    seen.add(r)
                    nodes.append(r)
    rows = []
    for cid, msg in db.messages.items():
        for sig in msg.signals:
            rows.append({
                "can_id": cid,
                "signal": sig.name,
                "msg": msg.name,
                "cells": _cells_for_signal(db, msg, sig),
            })
    return nodes, rows


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome = QWidget()
    chrome.setObjectName("SuiteToolbar")
    crow = QHBoxLayout(chrome)
    crow.setContentsMargins(12, 6, 12, 6)
    crow.setSpacing(8)
    filt = QLineEdit()
    filt.setPlaceholderText("Filter signal or message…")
    filt.setClearButtonEnabled(True)
    filt.setFixedHeight(28)
    filt.setMinimumWidth(220)
    crow.addWidget(filt, 1)
    refresh_btn = QPushButton("Refresh")
    refresh_btn.setObjectName("GhostButton")
    refresh_btn.setFixedHeight(28)
    codicons.set_button(refresh_btn, "refresh")
    crow.addWidget(refresh_btn)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    bl.setContentsMargins(16, 12, 16, 12)
    bl.setSpacing(10)

    hint = QLabel(
        "Communications matrix — same job as CANdb++ View|Communications matrix, "
        "but live-filtered and one double-click from Editor.")
    hint.setWordWrap(True)
    hint.setStyleSheet("color:#78909c;font-size:12px;")
    bl.addWidget(hint)

    chips = QHBoxLayout()
    chips.setSpacing(8)
    for text, color in (
        ("Transmit", "#1565C0"),
        ("Receive", "#2E7D32"),
        ("Both", "#6A1B9A"),
    ):
        chip = QLabel("●  " + text)
        chip.setStyleSheet(
            "QLabel { color: %s; font-size: 11px; font-weight: 600; "
            "padding: 3px 10px; background: #F5F7FA; border-radius: 11px; }"
            % color)
        chips.addWidget(chip)
    chips.addStretch(1)
    status = QLabel("")
    status.setStyleSheet("color:#90A4AE;font-size:11px;")
    chips.addWidget(status)
    bl.addLayout(chips)

    table = QTableWidget(0, 2)
    table.setObjectName("SuiteMatrix")
    table.setAlternatingRowColors(True)
    table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectItems)
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.setShowGrid(True)
    table.verticalHeader().setVisible(False)
    table.verticalHeader().setDefaultSectionSize(28)
    table.setSortingEnabled(False)
    table.setStyleSheet(
        "QTableWidget#SuiteMatrix { gridline-color: #EEEEEE; }"
        "QTableWidget#SuiteMatrix::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
    )
    bl.addWidget(table, 1)
    layout.addWidget(body, 1)

    def _paint():
        nodes, rows = build_matrix_model(document.db)
        q = (filt.text() or "").strip().lower()
        if q:
            rows = [
                r for r in rows
                if q in (r["signal"] or "").lower()
                or q in (r["msg"] or "").lower()
                or q in ("0x%x" % r["can_id"])
            ]

        cols = 2 + len(nodes)
        table.clear()
        table.setColumnCount(cols)
        table.setHorizontalHeaderLabels(["Signal", "Message"] + nodes)
        table.setRowCount(len(rows))

        tx_brush = QBrush(QColor("#1565C0"))
        rx_brush = QBrush(QColor("#2E7D32"))
        both_brush = QBrush(QColor("#6A1B9A"))
        bold = QFont()
        bold.setBold(True)

        for ri, row in enumerate(rows):
            sig_item = QTableWidgetItem(row["signal"])
            sig_item.setData(Qt.ItemDataRole.UserRole, row)
            sig_item.setFont(bold)
            table.setItem(ri, 0, sig_item)

            msg_item = QTableWidgetItem("%s (0x%X)" % (row["msg"], row["can_id"]))
            msg_item.setData(Qt.ItemDataRole.UserRole, row)
            table.setItem(ri, 1, msg_item)

            cells = row["cells"]
            for ci, node in enumerate(nodes):
                cell = QTableWidgetItem("")
                cell.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                cell.setData(Qt.ItemDataRole.UserRole, row)
                info = cells.get(node)
                if info:
                    role, mname = info
                    if role == "tx":
                        cell.setText("> %s" % mname)
                        cell.setForeground(tx_brush)
                    elif role == "rx":
                        cell.setText(mname)
                        cell.setForeground(rx_brush)
                    else:
                        cell.setText("<> %s" % mname)
                        cell.setForeground(both_brush)
                    cell.setToolTip("%s — %s on %s" % (role.upper(), mname, node))
                table.setItem(ri, 2 + ci, cell)

        table.horizontalHeader().setSectionResizeMode(
            0, QHeaderView.ResizeMode.Interactive)
        table.horizontalHeader().setSectionResizeMode(
            1, QHeaderView.ResizeMode.ResizeToContents)
        for i in range(2, cols):
            table.horizontalHeader().setSectionResizeMode(
                i, QHeaderView.ResizeMode.Stretch)
        table.resizeColumnToContents(0)
        if table.columnWidth(0) < 140:
            table.setColumnWidth(0, 140)
        if table.columnWidth(0) > 280:
            table.setColumnWidth(0, 280)

        status.setText("%d signals · %d nodes" % (len(rows), len(nodes)))
        plugin_shell.set_status(
            shell, "Matrix: %d signals, %d nodes" % (len(rows), len(nodes)), 2500)

    def _goto(item):
        if item is None:
            return
        row = item.data(Qt.ItemDataRole.UserRole) or {}
        cid = row.get("can_id")
        sig = row.get("signal")
        if cid is None:
            return
        shell.goto_editor_target(cid, sig)
        log_fn("Matrix", "Jump to 0x%X / %s" % (int(cid), sig or ""))

    refresh_btn.clicked.connect(_paint)
    filt.textChanged.connect(lambda _t: _paint())
    table.itemDoubleClicked.connect(_goto)
    document.on_changed(_paint)
    _paint()
    return root
