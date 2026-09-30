# -*- coding: utf-8 -*-
"""Shared live tables fed by the J1939 analyzer engine."""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QGuiApplication
from PyQt6.QtWidgets import (
    QHeaderView,
    QLineEdit,
    QMenu,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from pages import _ui, analyzer


def _host(
        parent, session, headers, fill, empty_title, empty_hint,
        goto_live=True, pgn_col=None, related=None):
    """Build a filterable table with right-click Related bridges.

    pgn_col: column index holding a PGN hex (optional).
    related: list of (label, page_key) for Open … menu entries.
    """
    related = list(related or [])
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    filter_edit = QLineEdit()
    filter_edit.setPlaceholderText("Filter…")
    filter_edit.setClearButtonEnabled(True)
    filter_edit.setToolTip("Filter table rows")
    live_btn = _ui.ghost_btn("Open Live", "Go to Live analyzer", "search")
    layout.addWidget(_ui.tool_strip(live_btn))
    layout.addWidget(_ui.inline_filter(filter_edit))

    tree = QTreeWidget()
    tree.setHeaderLabels(headers)
    _ui.style_tree(tree, header_hidden=False)
    tree.setRootIsDecorated(False)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)

    open_live = _ui.ghost_btn("Open Live", "Go to Live analyzer", "search")
    empty = _ui.empty_state(empty_title, empty_hint, actions=[open_live])
    layout.addWidget(empty)

    def _goto(page: str):
        if hasattr(parent, "run_action"):
            parent.run_action("j1939.goto", page=page)
        elif hasattr(parent, "goto_page"):
            parent.goto_page(page)

    def _goto_live():
        _goto("analyzer")

    if goto_live:
        live_btn.clicked.connect(_goto_live)
        open_live.clicked.connect(_goto_live)

    def _copy_text(text: str):
        if not text:
            return
        QGuiApplication.clipboard().setText(str(text))
        plugin_shell.set_status(parent, "Copied", 1500)

    def _parse_pgn(item):
        if item is None or pgn_col is None:
            return None
        try:
            return int(item.text(int(pgn_col)), 0) & 0x3FFFF
        except Exception:
            return None

    def _tick():
        needle = (filter_edit.text() or "").strip().lower()
        focus_pgn = int(getattr(session, "focus_pgn", 0) or 0)
        rows = list(fill())
        tree.clear()
        shown = 0
        focus_item = None
        for cols in rows:
            text = " ".join(str(c) for c in cols)
            if needle and needle not in text.lower():
                continue
            it = QTreeWidgetItem([str(c) for c in cols])
            if pgn_col is not None and pgn_col < len(cols):
                try:
                    pgn = int(str(cols[pgn_col]), 0) & 0x3FFFF
                    it.setData(0, Qt.ItemDataRole.UserRole, pgn)
                    if focus_pgn and pgn == focus_pgn and focus_item is None:
                        focus_item = it
                except Exception:
                    pass
            tree.addTopLevelItem(it)
            shown += 1
        if focus_item is not None:
            tree.setCurrentItem(focus_item)
            tree.scrollToItem(focus_item)
        empty.setVisible(shown == 0)
        tree.setVisible(shown > 0)

    def on_activate(item, _col):
        pgn = None
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if data is not None:
            try:
                pgn = int(data)
            except (TypeError, ValueError):
                pgn = None
        if pgn is None:
            pgn = _parse_pgn(item)
        if pgn is not None and hasattr(session, "set_focus"):
            session.set_focus(pgn=pgn)

    def _menu(pos):
        item = tree.itemAt(pos)
        menu = QMenu(tree)
        if item is not None:
            tree.setCurrentItem(item)
            pgn = item.data(0, Qt.ItemDataRole.UserRole)
            if pgn is None:
                pgn = _parse_pgn(item)
            menu.addAction(
                "Copy row",
                lambda: _copy_text(
                    "\t".join(item.text(c) for c in range(item.columnCount()))))
            if pgn is not None:
                session.set_focus(pgn=int(pgn))
                menu.addAction(
                    "Copy PGN",
                    lambda p=int(pgn): _copy_text("0x%04X" % p))
                menu.addSeparator()
                menu.addAction(
                    "Open Live (PGN)…",
                    lambda p=int(pgn): parent.run_action(
                        "j1939.goto_pgn", pgn=p)
                    if hasattr(parent, "run_action") else _goto_live())
            menu.addSeparator()
        else:
            menu.addAction("Open Live…", _goto_live)
        for label, page in related:
            menu.addAction(
                label, lambda _c=False, p=page: _goto(p))
        if menu.actions():
            menu.exec(tree.viewport().mapToGlobal(pos))

    def _on_focus(pgn, _sa):
        if int(pgn or 0):
            _tick()

    if hasattr(session, "on_focus"):
        session.on_focus(_on_focus)

    tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    tree.customContextMenuRequested.connect(_menu)
    tree.itemClicked.connect(on_activate)
    filter_edit.textChanged.connect(lambda _t: _tick())
    timer = QTimer(root)
    timer.timeout.connect(_tick)
    timer.start(500)
    _tick()
    _ui.polish_work_surface(root)
    return root


def build_transport(parent, session, log_fn) -> QWidget:
    def fill():
        if hasattr(session, "note_tp") and analyzer.snapshot_tp():
            session.note_tp()
        rows = []
        for ts, sa, da, pgn, payload, kind in analyzer.snapshot_tp():
            rows.append((
                "%.3f" % ts, kind, "%02X" % sa, "%02X" % da,
                "0x%04X" % pgn, len(payload),
                " ".join("%02X" % b for b in payload[:24]),
            ))
        return rows

    return _host(
        parent, session,
        ["Time", "Kind", "SA", "DA", "PGN", "Bytes", "Payload"],
        fill,
        "No transport PDUs",
        "BAM / RTS-CTS reassembled PDUs appear here when traffic arrives.",
        pgn_col=4,
        related=[
            ("Open Diagnostics…", "diagnostics"),
            ("Open Network…", "network"),
        ],
    )


def build_diagnostics(parent, session, log_fn) -> QWidget:
    def fill():
        if hasattr(session, "note_dm") and analyzer.snapshot_dm():
            session.note_dm()
        rows = []
        for ts, pgn, sa, spn, text, lamps in analyzer.snapshot_dm():
            rows.append((
                "%.3f" % ts,
                "DM1" if pgn == 0xFECA else "DM2",
                "0x%04X" % pgn,
                "%02X" % sa,
                "" if spn is None else str(spn),
                text,
                lamps,
            ))
        return rows

    return _host(
        parent, session,
        ["Time", "Kind", "PGN", "SA", "SPN", "DTC", "Lamps"],
        fill,
        "No diagnostics yet",
        "DM1 / DM2 rows appear when active or previously active DTCs arrive.",
        pgn_col=2,
        related=[
            ("Open Transport…", "transport"),
            ("Open Network…", "network"),
        ],
    )


def build_network(parent, session, log_fn) -> QWidget:
    def fill():
        if hasattr(session, "note_claim") and analyzer.snapshot_addr():
            session.note_claim()
        rows = []
        for sa, claim in analyzer.snapshot_addr():
            rows.append((
                "%02X" % sa,
                claim.get("name_hex", ""),
                str(claim.get("manufacturer", "")),
                str(claim.get("function", "")),
                claim.get("function_name", ""),
                time.strftime("%H:%M:%S", time.localtime(claim.get("ts", 0)))
                if claim.get("ts") else "",
            ))
        return rows

    return _host(
        parent, session,
        ["SA", "NAME", "Mfr", "Func code", "Function", "Claim time"],
        fill,
        "No address claims",
        "Address Claimed NAME rows appear when nodes announce.",
        pgn_col=None,
        related=[
            ("Open Diagnostics…", "diagnostics"),
            ("Open Transport…", "transport"),
        ],
    )
