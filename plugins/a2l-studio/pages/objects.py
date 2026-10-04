# -*- coding: utf-8 -*-
"""Objects — ASAP2 symbol explorer + property panel."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import suite_chrome
from pages import _ui

_KINDS = (
    "MEASUREMENT", "CHARACTERISTIC", "AXIS_PTS",
    "COMPU_METHOD", "COMPU_VTAB",
)


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    filt = _ui.line_edit("Filter by name / kind / module", "Filter…")
    filt.setMaximumWidth(200)
    kind_box = _ui.combo(
        ["All kinds"] + list(_KINDS), "Filter by ASAP2 block kind")
    count = _ui.count_label()
    apply_xcp = _ui.primary_btn(
        "Apply → XCP", "Hand off A2L path to XCP Studio", "apply")
    crow.addWidget(filt)
    crow.addWidget(kind_box)
    crow.addWidget(count)
    crow.addStretch(1)
    crow.addWidget(apply_xcp)
    layout.addWidget(chrome)

    split = QSplitter(Qt.Orientation.Horizontal)
    split.setHandleWidth(1)
    split.setChildrenCollapsible(False)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Name", "Kind", "Address", "Type"])
    _ui.style_tree(tree)
    tree.setMinimumWidth(320)
    split.addWidget(tree)

    props = QWidget()
    props.setMaximumWidth(_ui.FORM_MAX_W if hasattr(_ui, "FORM_MAX_W") else 360)
    flay = QFormLayout(props)
    flay.setContentsMargins(8, 8, 8, 8)
    name_lbl = QLabel("—")
    kind_lbl = QLabel("—")
    addr_edit = _ui.line_edit("ECU_ADDRESS (hex)", "0x…")
    dtype_lbl = QLabel("—")
    conv_lbl = QLabel("—")
    char_lbl = QLabel("—")
    axis_lbl = QLabel("—")
    desc_lbl = QLabel("—")
    desc_lbl.setWordWrap(True)
    apply_addr = _ui.ghost_btn("Write address", "Update ECU_ADDRESS on selection", "save")
    flay.addRow("Name", name_lbl)
    flay.addRow("Kind", kind_lbl)
    flay.addRow("Address", addr_edit)
    flay.addRow("", apply_addr)
    flay.addRow("Datatype", dtype_lbl)
    flay.addRow("Conversion", conv_lbl)
    flay.addRow("Char type", char_lbl)
    flay.addRow("Axis ref", axis_lbl)
    flay.addRow("Description", desc_lbl)
    split.addWidget(props)
    split.setStretchFactor(0, 3)
    split.setStretchFactor(1, 2)
    layout.addWidget(split, 1)

    selected = {"kind": None, "name": None}

    def _addr_text(sym) -> str:
        return ("0x%X" % sym.address) if sym.address else ""

    def _type_text(sym) -> str:
        return sym.datatype or sym.char_type or ""

    def refresh():
        tree.clear()
        q = (filt.text() or "").strip().lower()
        kind_f = kind_box.currentText()
        n = 0
        for sym in document.symbols:
            if kind_f != "All kinds" and sym.kind != kind_f:
                continue
            blob = " ".join([
                sym.name, sym.kind, sym.module or "",
                sym.description or "", _type_text(sym),
            ]).lower()
            if q and q not in blob:
                continue
            item = QTreeWidgetItem([
                sym.name, sym.kind, _addr_text(sym), _type_text(sym)])
            item.setData(0, Qt.ItemDataRole.UserRole, (sym.kind, sym.name))
            tree.addTopLevelItem(item)
            n += 1
        count.setText("%d symbols" % n)

    def _show_props(kind, name):
        selected["kind"] = kind
        selected["name"] = name
        document.set_focus(kind, name)
        sym = document.doc.find(kind, name) if kind and name else None
        if sym is None:
            name_lbl.setText("—")
            kind_lbl.setText("—")
            addr_edit.setText("")
            dtype_lbl.setText("—")
            conv_lbl.setText("—")
            char_lbl.setText("—")
            axis_lbl.setText("—")
            desc_lbl.setText("—")
            return
        name_lbl.setText(sym.name)
        kind_lbl.setText(sym.kind)
        addr_edit.setText(_addr_text(sym))
        dtype_lbl.setText(sym.datatype or "—")
        conv_lbl.setText(sym.conversion or "—")
        char_lbl.setText(sym.char_type or "—")
        axis_lbl.setText(sym.axis_ref or "—")
        desc_lbl.setText(sym.description or "—")

    def _on_sel():
        item = tree.currentItem()
        if not item:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        _show_props(data[0], data[1])

    def _write_addr():
        kind, name = selected["kind"], selected["name"]
        if not kind or not name:
            return
        text = (addr_edit.text() or "").strip()
        try:
            addr = int(text, 0) if text else 0
        except ValueError:
            log_fn("ERR", "-", b"", "Bad address: %s" % text)
            return
        if document.update_symbol_address(kind, name, addr):
            log_fn("OK", "-", b"", "Address %s.%s = 0x%X" % (kind, name, addr))
            refresh()

    def _apply():
        if hasattr(shell, "run_action"):
            shell.run_action("a2l.apply_xcp")

    filt.textChanged.connect(lambda *_: refresh())
    kind_box.currentIndexChanged.connect(lambda *_: refresh())
    tree.itemSelectionChanged.connect(_on_sel)
    apply_addr.clicked.connect(_write_addr)
    apply_xcp.clicked.connect(_apply)
    document.on_changed(refresh)
    refresh()
    root.refresh = refresh  # type: ignore[attr-defined]
    return root
