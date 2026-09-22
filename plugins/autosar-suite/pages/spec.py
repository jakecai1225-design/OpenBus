# -*- coding: utf-8 -*-
"""Spec — AUTOSAR CP + BSWMD-lite parameter encyclopedia."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QSplitter,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from _shared import arxmlparse, suite_chrome
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    filt = QLineEdit()
    filt.setPlaceholderText("Search parameters…")
    filt.setFixedHeight(26)
    filt.setMaximumWidth(220)
    count = _ui.quiet_label("")
    import_btn = _ui.ghost_btn(
        "Import BSWMD…", "Load BSWMD-lite JSON tip pack", "folder")
    default_btn = _ui.ghost_btn(
        "Load built-in pack", "Com/CanIf/PduR/CanNm tip pack", "file")
    crow.addWidget(filt)
    crow.addWidget(count)
    crow.addStretch(1)
    crow.addWidget(default_btn)
    crow.addWidget(import_btn)
    layout.addWidget(chrome)

    split = QSplitter(Qt.Orientation.Horizontal)
    lst = QListWidget()
    lst.setMinimumWidth(260)
    split.addWidget(lst)

    right = QWidget()
    right.setObjectName("SuiteContent")
    rl = QVBoxLayout(right)
    suite_chrome.page_margins(rl)
    title = QLabel("Select a parameter")
    title.setObjectName("SuiteSectionTitle")
    body = QTextEdit()
    body.setReadOnly(True)
    body.setObjectName("SuiteReport")
    rl.addWidget(title)
    rl.addWidget(body, 1)
    split.addWidget(right)
    split.setStretchFactor(0, 2)
    split.setStretchFactor(1, 3)
    layout.addWidget(split, 1)

    def _refresh():
        lst.clear()
        rows = arxmlparse.search_spec(filt.text())
        for row in rows:
            item = QListWidgetItem(row["title"])
            item.setData(Qt.ItemDataRole.UserRole, row)
            item.setToolTip(row.get("summary", ""))
            lst.addItem(item)
        count.setText("%d topics" % len(rows))

    def _on_sel():
        item = lst.currentItem()
        if not item:
            return
        row = item.data(Qt.ItemDataRole.UserRole) or {}
        title.setText(row.get("title", ""))
        body.setPlainText(
            "Category: %s\n\n%s\n\n%s\n\nTypical range: %s\n\n"
            "Id: %s\nDefinition: %s" % (
                row.get("category", ""),
                row.get("summary", ""),
                row.get("detail", ""),
                row.get("range", ""),
                row.get("id", ""),
                row.get("definition", ""),
            ))

    def _import():
        path, _ = QFileDialog.getOpenFileName(
            shell, "Import BSWMD-lite JSON", "",
            "JSON (*.json);;All (*)")
        if not path:
            return
        try:
            rows = arxmlparse.load_bswmd_lite(path)
            log_fn("OK", "Imported %d BSWMD tips" % len(rows))
            _refresh()
        except (OSError, ValueError, KeyError) as e:
            log_fn("ERR", str(e))

    def _default():
        n = arxmlparse.load_default_bswmd_pack()
        log_fn("OK", "Loaded built-in BSWMD pack (+%d)" % n)
        _refresh()

    filt.textChanged.connect(lambda *_: _refresh())
    lst.currentItemChanged.connect(lambda *_: _on_sel())
    import_btn.clicked.connect(_import)
    default_btn.clicked.connect(_default)
    _refresh()
    if lst.count():
        lst.setCurrentRow(0)
    return root
