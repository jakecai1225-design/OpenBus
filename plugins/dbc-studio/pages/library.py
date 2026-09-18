# -*- coding: utf-8 -*-
"""Library workspace — recent files + workspace DBC list."""

from __future__ import annotations

import os

from PyQt6.QtWidgets import (
    QHBoxLayout, QLabel, QListWidget, QMessageBox, QPushButton,
    QVBoxLayout, QWidget,
)

from _shared import dbc_picker


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)

    layout.addWidget(QLabel("Recent files"))
    recent = QListWidget()
    layout.addWidget(recent, 1)

    layout.addWidget(QLabel("Workspace DBC files"))
    workspace = QListWidget()
    layout.addWidget(workspace, 1)

    row = QHBoxLayout()
    refresh_btn = QPushButton("Refresh")
    open_btn = QPushButton("Open selected")
    browse_btn = QPushButton("Browse workspace…")
    row.addWidget(refresh_btn)
    row.addStretch()
    row.addWidget(browse_btn)
    row.addWidget(open_btn)
    layout.addLayout(row)

    def _refresh():
        recent.clear()
        for p in shell.recent_files():
            recent.addItem(p)
        workspace.clear()
        paths = []
        try:
            import sin
            paths = list(sin.workspace.get_dbc_files() or [])
        except Exception:
            paths = []
        for p in paths:
            if p:
                workspace.addItem(p)

    def _selected_path():
        for lst in (recent, workspace):
            items = lst.selectedItems()
            if items:
                return items[0].text()
        return ""

    def _on_open():
        path = _selected_path()
        if not path:
            QMessageBox.information(shell, "Library", "Select a DBC path first")
            return
        if not os.path.isfile(path):
            QMessageBox.warning(shell, "Library", "File not found:\n%s" % path)
            return
        if not shell._confirm_discard():
            return
        if shell._load_path(path):
            shell.goto_page("editor")
            log_fn("Library", "Opened %s" % path)

    def _on_browse():
        path = dbc_picker.pick_dbc(shell, "Workspace DBC")
        if path:
            if not shell._confirm_discard():
                return
            if shell._load_path(path):
                shell.goto_page("editor")

    refresh_btn.clicked.connect(_refresh)
    open_btn.clicked.connect(_on_open)
    browse_btn.clicked.connect(_on_browse)
    recent.itemDoubleClicked.connect(lambda _i: _on_open())
    workspace.itemDoubleClicked.connect(lambda _i: _on_open())

    _refresh()
    document.on_changed(_refresh)
    return root
