# -*- coding: utf-8 -*-
"""Setup workspace — channel hint shared by every protocol page."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store, vscode_theme

PLUGIN_ID = "protocol-hub"


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(12)

    card, body = vscode_theme.block(
        "Channel",
        "Advisory label for which bus this hub is watching. It does not open hardware.")
    row = QHBoxLayout()
    edit = QLineEdit()
    edit.setPlaceholderText("e.g. CH0 / vcan0")
    edit.setText(session.channel_hint)
    apply_btn = QPushButton("Apply")
    clear_btn = QPushButton("Clear")
    row.addWidget(QLabel("Hint"))
    row.addWidget(edit, 1)
    row.addWidget(apply_btn)
    row.addWidget(clear_btn)
    body.addLayout(row)
    layout.addWidget(card)
    layout.addStretch(1)

    def _push(text):
        session.set_channel_hint(text)
        if hasattr(parent, "hint_edit"):
            parent.hint_edit.setText(text)
        if hasattr(parent, "_persist"):
            parent._persist()
        plugin_shell.set_status(parent, "Channel hint applied", 2000)

    def _apply():
        _push(edit.text().strip())

    def _clear():
        edit.clear()
        _push("")

    def _sync():
        if edit.text() != session.channel_hint:
            edit.setText(session.channel_hint)

    apply_btn.clicked.connect(_apply)
    clear_btn.clicked.connect(_clear)
    session.on_hint_changed(_sync)
    saved = state_store.load_state(PLUGIN_ID, default={}) or {}
    if not edit.text() and saved.get("channel_hint"):
        edit.setText(saved.get("channel_hint") or "")
    return root
