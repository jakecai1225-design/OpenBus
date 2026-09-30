# -*- coding: utf-8 -*-
"""Readiness workspace — Mode 01 PID 01 monitor summary."""

from __future__ import annotations

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QGuiApplication
from PyQt6.QtWidgets import (
    QHeaderView,
    QLabel,
    QMenu,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.readiness import decode_pid01
from pages import _ui, scanner


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    read_btn = _ui.primary_btn(
        "Read monitors", "Request Mode 01 PID 01", "refresh")
    open_scan = _ui.ghost_btn(
        "Open Scanner", "Discover PIDs on Scanner", "search")
    summary = _ui.muted_label("No sample yet")
    summary.setToolTip("MIL / DTC count / ignition type after a successful read")
    layout.addWidget(_ui.tool_strip(read_btn, open_scan, summary))

    tree = QTreeWidget()
    tree.setHeaderLabels(["Monitor", "Kind", "Supported", "Complete"])
    _ui.style_tree(tree, header_hidden=False)
    tree.setRootIsDecorated(False)
    tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 1)

    setup_cta = _ui.ghost_btn("Setup", "Open Setup", "settings")
    scan_cta = _ui.ghost_btn("Scanner", "Open Scanner", "search")
    empty = _ui.empty_state(
        "No readiness sample",
        "Apply IDs on Setup, open Scanner once, then Read monitors.",
        actions=[setup_cta, scan_cta])
    layout.addWidget(empty)

    def _show_empty(on: bool):
        tree.setVisible(not on)
        empty.setVisible(on)

    def _goto(page: str):
        if hasattr(parent, "run_action"):
            parent.run_action("obd.goto", page=page)
        elif hasattr(parent, "goto_page"):
            parent.goto_page(page)

    def _copy_text(text: str):
        if not text:
            return
        QGuiApplication.clipboard().setText(str(text))
        from _shared import plugin_shell
        plugin_shell.set_status(parent, "Copied", 1500)

    def _refresh():
        raw = scanner.readiness_raw()
        if len(raw) < 1:
            _show_empty(True)
            return
        _show_empty(False)
        info = decode_pid01(raw)
        summary.setText(
            "MIL %s · DTCs %d · %s"
            % ("ON" if info["mil"] else "OFF", info["dtc_count"], info["ignition"]))
        tree.clear()
        for mon in info["monitors"]:
            if not mon["supported"] and mon["kind"] != "continuous":
                continue
            it = QTreeWidgetItem([
                mon["name"],
                mon["kind"],
                "yes" if mon["supported"] else "no",
                "yes" if mon["complete"] else "no",
            ])
            it.setData(0, Qt.ItemDataRole.UserRole, mon["name"])
            tree.addTopLevelItem(it)

    def _read():
        session.note_readiness()
        session.set_focus(mode=1, pid=0x01)
        if not scanner.request_pid(1, 0x01, functional=True):
            summary.setText("Open Scanner once so ISO-TP is ready")
            _show_empty(True)
            return
        log_fn("RX", 0, b"", "Readiness request Mode 01 PID 01")
        if hasattr(parent, "_sync_next_hint"):
            parent._sync_next_hint()

    def _menu(pos):
        item = tree.itemAt(pos)
        menu = QMenu(tree)
        menu.addAction("Read monitors", _read)
        menu.addAction("Open Scanner…", lambda: _goto("scanner"))
        menu.addAction(
            "Discover PIDs…",
            lambda: parent.run_action("obd.discover")
            if hasattr(parent, "run_action") else _goto("scanner"))
        if item is not None:
            tree.setCurrentItem(item)
            menu.addSeparator()
            menu.addAction(
                "Copy name", lambda: _copy_text(item.text(0)))
            menu.addAction(
                "Copy row",
                lambda: _copy_text(
                    "\t".join(item.text(c) for c in range(4))))
        if menu.actions():
            menu.exec(tree.viewport().mapToGlobal(pos))

    def _on_focus(mode, pid):
        if int(mode or 0) == 1 and int(pid or 0) == 0x01:
            # Focus landed on readiness PID — ensure page is live
            summary.setToolTip("Focused Mode 01 PID 01 (readiness)")

    session.on_focus(_on_focus)
    tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    tree.customContextMenuRequested.connect(_menu)

    read_btn.clicked.connect(_read)
    open_scan.clicked.connect(lambda: _goto("scanner"))
    setup_cta.clicked.connect(lambda: _goto("setup"))
    scan_cta.clicked.connect(lambda: _goto("scanner"))
    root.do_read = _read  # type: ignore[attr-defined]

    timer = QTimer(root)
    timer.timeout.connect(_refresh)
    timer.start(400)
    _show_empty(True)
    _ui.polish_work_surface(root)
    return root
