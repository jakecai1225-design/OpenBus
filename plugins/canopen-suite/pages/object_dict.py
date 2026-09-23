# -*- coding: utf-8 -*-
"""Object Dictionary — live OD tree + SDO. No nested chrome."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core.od_cia301 import CIA301_OBJECTS


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QHBoxLayout(root)
    layout.setContentsMargins(8, 6, 8, 6)
    layout.setSpacing(8)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Index", "Name", "Access", "Default"])
    tree.setAlternatingRowColors(True)
    tree.setMinimumWidth(240)
    tree.setMaximumWidth(480)
    tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    layout.addWidget(tree, 3)

    right = QWidget()
    right.setMinimumWidth(240)
    right.setMaximumWidth(320)
    rv = QVBoxLayout(right)
    rv.setContentsMargins(0, 0, 0, 0)
    rv.setSpacing(8)
    form = QFormLayout()
    form.setSpacing(8)
    idx_spin = QSpinBox()
    idx_spin.setObjectName("SuiteSpin")
    idx_spin.setRange(0x1000, 0xFFFF)
    idx_spin.setDisplayIntegerBase(16)
    idx_spin.setPrefix("0x")
    idx_spin.setValue(0x1018)
    idx_spin.setFixedHeight(28)
    sub_spin = QSpinBox()
    sub_spin.setObjectName("SuiteSpin")
    sub_spin.setRange(0, 255)
    sub_spin.setValue(1)
    sub_spin.setFixedHeight(28)
    val_edit = QLineEdit("0")
    val_edit.setFixedHeight(28)
    val_edit.setToolTip("Write value (decimal or 0x hex)")
    size_spin = QSpinBox()
    size_spin.setObjectName("SuiteSpin")
    size_spin.setRange(1, 4)
    size_spin.setValue(4)
    size_spin.setFixedHeight(28)
    result_lbl = QLabel("-")
    result_lbl.setTextInteractionFlags(
        Qt.TextInteractionFlag.TextSelectableByMouse)
    form.addRow("Index", idx_spin)
    form.addRow("Sub-index", sub_spin)
    form.addRow("Value", val_edit)
    form.addRow("Size", size_spin)
    form.addRow("Result", result_lbl)
    rv.addLayout(form)

    btn_row = QHBoxLayout()
    read_btn = QPushButton("Read")
    read_btn.setObjectName("PrimaryButton")
    read_btn.setFixedHeight(28)
    read_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    write_btn = QPushButton("Write")
    write_btn.setObjectName("GhostButton")
    write_btn.setFixedHeight(28)
    write_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    write_btn.setToolTip("Confirm before writing to the node")
    btn_row.addWidget(read_btn)
    btn_row.addWidget(write_btn)
    rv.addLayout(btn_row)
    rv.addStretch(1)
    layout.addWidget(right, 1)

    def _entries() -> list:
        if session.od_entries:
            return session.od_entries
        return list(CIA301_OBJECTS)

    def refresh_tree():
        tree.clear()
        parents = {}
        for e in _entries():
            if e.subindex == 0:
                item = QTreeWidgetItem([
                    e.display_index(), e.name or "", e.access_type, e.default_value,
                ])
                item.setData(0, Qt.ItemDataRole.UserRole, (e.index, e.subindex))
                tree.addTopLevelItem(item)
                parents[e.index] = item
            else:
                parent_item = parents.get(e.index)
                if parent_item is None:
                    parent_item = QTreeWidgetItem([
                        "0x%04X" % e.index, "", "", "",
                    ])
                    parent_item.setData(
                        0, Qt.ItemDataRole.UserRole, (e.index, 0))
                    tree.addTopLevelItem(parent_item)
                    parents[e.index] = parent_item
                child = QTreeWidgetItem([
                    e.display_index(), e.name or "", e.access_type, e.default_value,
                ])
                child.setData(0, Qt.ItemDataRole.UserRole, (e.index, e.subindex))
                parent_item.addChild(child)

    def on_select():
        item = tree.currentItem()
        if not item:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        idx, sub = data
        idx_spin.setValue(idx)
        sub_spin.setValue(sub)

    def _parse_value(text: str) -> int:
        t = (text or "").strip()
        if t.lower().startswith("0x"):
            return int(t, 16)
        return int(t, 0)

    def on_read():
        idx = idx_spin.value()
        sub = sub_spin.value()

        def done(ok, value, note):
            if ok:
                result_lbl.setText("0x%X (%s)" % (value if value is not None else 0, note))
                plugin_shell.set_status(parent, "SDO read OK", 2500)
            else:
                result_lbl.setText("FAIL: %s" % note)
                plugin_shell.set_status(parent, "SDO read failed", 3000)

        if not session.sdo_upload(idx, sub, on_done=done):
            result_lbl.setText("Busy")

    def on_write():
        idx = idx_spin.value()
        sub = sub_spin.value()
        try:
            value = _parse_value(val_edit.text())
        except ValueError:
            QMessageBox.warning(parent, "Invalid value", "Enter a decimal or 0x hex value.")
            return
        size = size_spin.value()
        reply = QMessageBox.question(
            parent,
            "Confirm SDO write",
            "Write 0x%X to node %d object 0x%04X:%02X (%d bytes)?"
            % (value, session.node_id, idx, sub, size),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        def done(ok, _value, note):
            if ok:
                result_lbl.setText("Write OK")
                plugin_shell.set_status(parent, "SDO write OK", 2500)
            else:
                result_lbl.setText("FAIL: %s" % note)
                plugin_shell.set_status(parent, "SDO write failed", 3000)

        if not session.sdo_download(idx, sub, value, size, on_done=done):
            result_lbl.setText("Busy")

    tree.itemSelectionChanged.connect(on_select)
    read_btn.clicked.connect(on_read)
    write_btn.clicked.connect(on_write)
    session.on_od_changed(refresh_tree)
    refresh_tree()
    return root
