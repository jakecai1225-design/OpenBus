# -*- coding: utf-8 -*-
"""Library workspace — recent, favorites, workspace DBC list."""

from __future__ import annotations

import os
import subprocess
import sys

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, dbc_picker, plugin_shell, suite_chrome


def _reveal_in_os(path: str) -> None:
    path = os.path.normpath(path)
    if not os.path.exists(path):
        return
    try:
        if sys.platform.startswith("win"):
            subprocess.Popen(["explorer", "/select,", path])  # noqa: S603
        elif sys.platform == "darwin":
            subprocess.Popen(["open", "-R", path])  # noqa: S603
        else:
            subprocess.Popen(["xdg-open", os.path.dirname(path)])  # noqa: S603
    except Exception:
        pass


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    refresh_btn = QPushButton("Refresh")
    refresh_btn.setObjectName("GhostButton")
    refresh_btn.setFixedHeight(28)
    codicons.set_button(refresh_btn, "refresh")
    pin_btn = QPushButton("Pin")
    pin_btn.setObjectName("GhostButton")
    pin_btn.setFixedHeight(28)
    pin_btn.setToolTip("Add selection to Favorites")
    codicons.set_button(pin_btn, "add")
    unpin_btn = QPushButton("Unpin")
    unpin_btn.setObjectName("GhostButton")
    unpin_btn.setFixedHeight(28)
    codicons.set_button(unpin_btn, "delete")
    reveal_btn = QPushButton("Reveal")
    reveal_btn.setObjectName("GhostButton")
    reveal_btn.setFixedHeight(28)
    reveal_btn.setToolTip("Show in file manager")
    codicons.set_button(reveal_btn, "folder")
    clear_btn = QPushButton("Clear recent")
    clear_btn.setObjectName("GhostButton")
    clear_btn.setFixedHeight(28)
    codicons.set_button(clear_btn, "clear")
    browse_btn = QPushButton("Browse")
    browse_btn.setObjectName("GhostButton")
    browse_btn.setFixedHeight(28)
    codicons.set_button(browse_btn, "browse")
    open_btn = QPushButton("Open")
    open_btn.setFixedHeight(28)
    codicons.set_button(open_btn, "file", primary=True)
    for w in (refresh_btn, pin_btn, unpin_btn, reveal_btn, clear_btn):
        crow.addWidget(w)
    crow.addStretch(1)
    crow.addWidget(browse_btn)
    crow.addWidget(open_btn)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl)
    bl.setSpacing(10)

    hint = QLabel(
        "Favorites stay pinned. Recent is auto-filled on open. "
        "Double-click any path to open in Editor.")
    hint.setWordWrap(True)
    hint.setStyleSheet("color:#78909c;font-size:12px;")
    bl.addWidget(hint)

    split = QSplitter(Qt.Orientation.Horizontal)

    def _make_list(title: str):
        wrap = QWidget()
        vl = QVBoxLayout(wrap)
        vl.setContentsMargins(0, 0, 4, 0)
        vl.setSpacing(6)
        head = QLabel(title)
        head.setObjectName("SuiteSectionTitle")
        vl.addWidget(head)
        lst = QListWidget()
        lst.setAlternatingRowColors(True)
        lst.setStyleSheet("QListWidget { border: 1px solid #EEEEEE; }")
        vl.addWidget(lst, 1)
        return wrap, lst

    fav_w, favorites = _make_list("Favorites")
    recent_w, recent = _make_list("Recent")
    ws_w, workspace = _make_list("Workspace")
    split.addWidget(fav_w)
    split.addWidget(recent_w)
    split.addWidget(ws_w)
    split.setSizes([280, 320, 320])
    bl.addWidget(split, 1)

    status = QLabel("")
    status.setStyleSheet("color:#90A4AE;font-size:11px;")
    bl.addWidget(status)
    layout.addWidget(body, 1)

    lists = (favorites, recent, workspace)

    def _refresh():
        favorites.clear()
        for p in shell.favorite_files():
            item = QListWidgetItem(p)
            item.setToolTip(p)
            favorites.addItem(item)
        recent.clear()
        for p in shell.recent_files():
            item = QListWidgetItem(p)
            missing = not os.path.isfile(p)
            if missing:
                item.setForeground(Qt.GlobalColor.gray)
                item.setToolTip("Missing: %s" % p)
            else:
                item.setToolTip(p)
            recent.addItem(item)
        workspace.clear()
        paths = []
        try:
            import sin
            paths = list(sin.workspace.get_dbc_files() or [])
        except Exception:
            paths = []
        for p in paths:
            if p:
                item = QListWidgetItem(p)
                item.setToolTip(p)
                workspace.addItem(item)
        status.setText(
            "%d favorites · %d recent · %d workspace"
            % (favorites.count(), recent.count(), workspace.count()))

    def _selected_path():
        for lst in lists:
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
                plugin_shell.set_status(
                    shell, "Opened %s" % os.path.basename(path), 2500)

    def _on_pin():
        path = _selected_path()
        if not path:
            return
        shell.add_favorite(path)
        _refresh()
        plugin_shell.set_status(shell, "Pinned %s" % os.path.basename(path), 2000)

    def _on_unpin():
        path = _selected_path()
        if not path:
            return
        shell.remove_favorite(path)
        _refresh()

    def _on_reveal():
        path = _selected_path()
        if not path:
            return
        _reveal_in_os(path)

    def _on_clear_recent():
        shell.clear_recent()
        _refresh()
        log_fn("Library", "Cleared recent list")

    refresh_btn.clicked.connect(_refresh)
    open_btn.clicked.connect(_on_open)
    browse_btn.clicked.connect(_on_browse)
    pin_btn.clicked.connect(_on_pin)
    unpin_btn.clicked.connect(_on_unpin)
    reveal_btn.clicked.connect(_on_reveal)
    clear_btn.clicked.connect(_on_clear_recent)
    for lst in lists:
        lst.itemDoubleClicked.connect(lambda _i: _on_open())

    _refresh()
    document.on_changed(_refresh)
    return root
