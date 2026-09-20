# -*- coding: utf-8 -*-
"""Setup — Node-ID and EDS path (not a strip on every page)."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    outer = QVBoxLayout(root)
    outer.setContentsMargins(16, 12, 16, 12)
    outer.setSpacing(12)

    host = QWidget()
    host.setMaximumWidth(520)
    form = QFormLayout(host)
    form.setSpacing(10)
    form.setContentsMargins(0, 0, 0, 0)

    node = QSpinBox()
    node.setObjectName("SuiteSpin")
    node.setRange(1, 127)
    node.setValue(session.node_id)
    node.setFixedHeight(28)
    node.setToolTip("Shared Node-ID for SDO, NMT and PDO reads")

    eds_lbl = QLabel("(no EDS)")
    eds_lbl.setObjectName("SuiteHint")
    eds_lbl.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)

    form.addRow("Node-ID", node)
    form.addRow("EDS file", eds_lbl)
    outer.addWidget(host)

    row = QHBoxLayout()
    apply_btn = QPushButton("Apply node")
    apply_btn.setObjectName("PrimaryButton")
    apply_btn.setFixedHeight(28)
    apply_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    apply_btn.setToolTip("Apply Node-ID to the shared session")

    open_btn = QPushButton("Open EDS")
    open_btn.setObjectName("GhostButton")
    open_btn.setFixedHeight(28)
    open_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    open_btn.setToolTip("Load an EDS or DCF into OD and the editor draft")

    clear_btn = QPushButton("Clear EDS")
    clear_btn.setObjectName("GhostButton")
    clear_btn.setFixedHeight(28)
    clear_btn.setCursor(Qt.CursorShape.PointingHandCursor)

    open_editor = QPushButton("Edit EDS")
    open_editor.setObjectName("GhostButton")
    open_editor.setFixedHeight(28)
    open_editor.setCursor(Qt.CursorShape.PointingHandCursor)
    open_editor.setToolTip("Open the EDS Dictionary workbench")

    row.addWidget(apply_btn)
    row.addWidget(open_btn)
    row.addWidget(clear_btn)
    row.addWidget(open_editor)
    row.addStretch(1)
    outer.addLayout(row)
    outer.addStretch(1)

    def _sync_eds():
        path = session.eds_path
        if path:
            eds_lbl.setText(os.path.basename(path))
            eds_lbl.setToolTip(path)
        else:
            n = len(session.od_entries)
            eds_lbl.setText(
                "(no EDS%s)" % (", %d OD objects" % n if n else ""))
            eds_lbl.setToolTip("")

    def _apply():
        session.set_node_id(node.value())
        plugin_shell.set_status(parent, "Node-ID %d" % session.node_id, 2500)
        if hasattr(parent, "_persist"):
            parent._persist()

    def _open():
        path, _ = QFileDialog.getOpenFileName(
            parent, "Open EDS/DCF",
            session.eds_path or "",
            "EDS/DCF (*.eds *.dcf);;All files (*.*)")
        if not path:
            return
        if session.load_eds(path):
            _sync_eds()
            if hasattr(parent, "_persist"):
                parent._persist()
            plugin_shell.set_status(parent, "EDS loaded", 3000)

    def _clear():
        session.clear_eds()
        _sync_eds()
        if hasattr(parent, "_persist"):
            parent._persist()

    def _goto_eds():
        if hasattr(parent, "goto_page"):
            parent.goto_page("eds")

    def _on_node():
        node.blockSignals(True)
        node.setValue(session.node_id)
        node.blockSignals(False)

    apply_btn.clicked.connect(_apply)
    open_btn.clicked.connect(_open)
    clear_btn.clicked.connect(_clear)
    open_editor.clicked.connect(_goto_eds)
    session.on_node_changed(_on_node)
    session.on_od_changed(_sync_eds)
    _sync_eds()
    return root
