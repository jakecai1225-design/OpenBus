# -*- coding: utf-8 -*-
"""EDS Editor workspace — basic OD edit + export EDS text."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QTextEdit,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core.eds_parse import OdEntry, export_eds_text, validate_eds


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(12, 8, 12, 8)
    layout.setSpacing(8)

    tip = QLabel(
        "CANeds-style draft OD. FileInfo/DeviceInfo are preserved on Save. "
        "Apply pushes the draft into the live Object Dictionary.")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    layout.addWidget(tip)

    split = QHBoxLayout()

    tree = QTreeWidget()
    tree.setHeaderLabels(["Index", "Name", "Access", "Default"])
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    split.addWidget(tree, 2)

    right = QWidget()
    rv = QVBoxLayout(right)
    rv.setContentsMargins(0, 0, 0, 0)
    form = QFormLayout()
    form.setSpacing(8)
    idx_spin = QSpinBox()
    idx_spin.setObjectName("SuiteSpin")
    idx_spin.setRange(0x1000, 0xFFFF)
    idx_spin.setDisplayIntegerBase(16)
    idx_spin.setPrefix("0x")
    idx_spin.setValue(0x2000)
    idx_spin.setFixedHeight(28)
    sub_spin = QSpinBox()
    sub_spin.setObjectName("SuiteSpin")
    sub_spin.setRange(0, 255)
    sub_spin.setFixedHeight(28)
    name_edit = QLineEdit()
    name_edit.setFixedHeight(28)
    access_edit = QLineEdit("rw")
    access_edit.setFixedHeight(28)
    dtype_edit = QLineEdit("0x0007")
    dtype_edit.setFixedHeight(28)
    default_edit = QLineEdit()
    default_edit.setFixedHeight(28)
    form.addRow("Index", idx_spin)
    form.addRow("Subindex", sub_spin)
    form.addRow("Name", name_edit)
    form.addRow("Access", access_edit)
    form.addRow("DataType", dtype_edit)
    form.addRow("Default", default_edit)
    rv.addLayout(form)

    btn_row = QHBoxLayout()
    add_btn = QPushButton("Add / Update")
    add_btn.setObjectName("GhostButton")
    remove_btn = QPushButton("Remove")
    remove_btn.setObjectName("GhostButton")
    apply_btn = QPushButton("Apply → OD")
    apply_btn.setObjectName("PrimaryButton")
    validate_btn = QPushButton("Validate")
    validate_btn.setObjectName("GhostButton")
    save_btn = QPushButton("Save")
    save_btn.setObjectName("GhostButton")
    save_btn.setToolTip("Overwrite the loaded EDS path when set")
    export_btn = QPushButton("Save As…")
    export_btn.setObjectName("GhostButton")
    preview_btn = QPushButton("Preview")
    preview_btn.setObjectName("GhostButton")
    for w in (add_btn, remove_btn, apply_btn, validate_btn, save_btn, export_btn, preview_btn):
        w.setFixedHeight(28)
        w.setCursor(Qt.CursorShape.PointingHandCursor)
        btn_row.addWidget(w)
    rv.addLayout(btn_row)

    preview = QTextEdit()
    preview.setReadOnly(True)
    preview.setPlaceholderText("EDS text preview / validate findings…")
    rv.addWidget(preview, 1)
    split.addWidget(right, 1)
    layout.addLayout(split, 1)

    def refresh_tree():
        tree.clear()
        for e in session.draft_entries:
            item = QTreeWidgetItem([
                e.display_index(),
                e.name,
                e.access_type,
                e.default_value,
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, e.key)
            tree.addTopLevelItem(item)

    def on_select():
        item = tree.currentItem()
        if not item:
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        if not key:
            return
        for e in session.draft_entries:
            if e.key == key:
                idx_spin.setValue(e.index)
                sub_spin.setValue(e.subindex)
                name_edit.setText(e.name)
                access_edit.setText(e.access_type)
                dtype_edit.setText(e.data_type)
                default_edit.setText(e.default_value)
                break

    def on_add():
        idx = idx_spin.value()
        sub = sub_spin.value()
        entry = OdEntry(
            index=idx,
            subindex=sub,
            name=name_edit.text().strip() or ("Object 0x%04X" % idx),
            object_type="0x7",
            data_type=dtype_edit.text().strip() or "0x0007",
            access_type=access_edit.text().strip() or "rw",
            default_value=default_edit.text().strip(),
        )
        replaced = False
        for i, e in enumerate(session.draft_entries):
            if e.key == entry.key:
                session.draft_entries[i] = entry
                replaced = True
                break
        if not replaced:
            session.draft_entries.append(entry)
            session.draft_entries.sort(key=lambda x: (x.index, x.subindex))
        refresh_tree()
        log_fn("RX", "-", b"", "%s draft entry %s" % (
            "Updated" if replaced else "Added", entry.display_index()))

    def on_remove():
        item = tree.currentItem()
        if not item:
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        session.draft_entries = [
            e for e in session.draft_entries if e.key != key]
        refresh_tree()

    def on_apply():
        session.sync_od_from_draft()
        plugin_shell.set_status(parent, "Draft applied to Object Dictionary", 3000)
        log_fn("RX", "-", b"", "EDS draft applied to OD (%d entries)" % len(session.od_entries))

    def _text():
        base = os.path.basename(session.eds_path) if session.eds_path else "export.eds"
        return export_eds_text(
            session.draft_entries,
            file_name=base,
            file_info=getattr(session, "eds_file_info", None),
            device_info=getattr(session, "eds_device_info", None),
            other_meta=getattr(session, "eds_other_meta", None),
        )

    def on_preview():
        preview.setPlainText(_text())

    def on_validate():
        lines = []
        for finding in validate_eds(session.draft_entries):
            lines.append("[%s] %s  %s" % (
                finding["level"], finding.get("index", ""), finding["message"]))
        preview.setPlainText("\n".join(lines))
        log_fn("SYS", "-", b"", "EDS validate (%d)" % len(lines))

    def _write(path: str) -> bool:
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(_text())
        except OSError as e:
            QMessageBox.warning(parent, "Save failed", str(e))
            return False
        session.eds_path = path
        log_fn("RX", "-", b"", "Saved EDS: %s" % path)
        plugin_shell.set_status(parent, "Saved %s" % path, 4000)
        preview.setPlainText(_text())
        return True

    def on_save():
        if session.eds_path:
            _write(session.eds_path)
            return
        on_export()

    def on_export():
        path, _ = QFileDialog.getSaveFileName(
            parent, "Save EDS",
            session.eds_path or "export.eds",
            "EDS (*.eds);;All files (*.*)")
        if path:
            _write(path)

    tree.itemSelectionChanged.connect(on_select)
    add_btn.clicked.connect(on_add)
    remove_btn.clicked.connect(on_remove)
    apply_btn.clicked.connect(on_apply)
    validate_btn.clicked.connect(on_validate)
    save_btn.clicked.connect(on_save)
    export_btn.clicked.connect(on_export)
    preview_btn.clicked.connect(on_preview)
    session.on_od_changed(refresh_tree)
    refresh_tree()
    return root
