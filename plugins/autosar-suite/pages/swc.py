# -*- coding: utf-8 -*-
"""SWC — application component / port mapping lite (no RTE codegen)."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QFormLayout,
    QHeaderView,
    QLineEdit,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import arxmlparse, suite_chrome, vscode_theme
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    derive_btn = _ui.ghost_btn(
        "Derive from COM", "One Sender port per signal", "refresh")
    export_btn = _ui.ghost_btn("Export SWC…", "Write SWC-lite ARXML", "export")
    tip = _ui.quiet_label("Port map only — no RTE / BSW codegen")
    crow.addWidget(derive_btn)
    crow.addWidget(export_btn)
    crow.addWidget(tip)
    crow.addStretch(1)
    count = _ui.quiet_label("")
    crow.addWidget(count)
    layout.addWidget(chrome)

    split_host = QWidget()
    sl = QVBoxLayout(split_host)
    suite_chrome.page_margins(sl)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Name", "Kind", "Signal", "PDU", "Direction"])
    _ui.style_tree(tree)
    tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    sl.addWidget(tree, 1)

    form_host = QWidget()
    form_host.setMaximumWidth(360)
    form = QFormLayout(form_host)
    vscode_theme.tune_form(form)
    name_ed = QLineEdit()
    name_ed.setFixedHeight(28)
    dir_cb = QComboBox()
    dir_cb.addItems(["Sender", "Receiver"])
    dir_cb.setFixedHeight(28)
    sig_ed = QLineEdit()
    sig_ed.setFixedHeight(28)
    pdu_ed = QLineEdit()
    pdu_ed.setFixedHeight(28)
    apply_btn = _ui.ghost_btn("Apply", "Write port fields", "apply")
    form.addRow(vscode_theme.field_label("Port name"), name_ed)
    form.addRow(vscode_theme.field_label("Direction"), dir_cb)
    form.addRow(vscode_theme.field_label("Signal"), sig_ed)
    form.addRow(vscode_theme.field_label("PDU"), pdu_ed)
    form.addRow(apply_btn)
    vscode_theme.polish_form_labels(form)
    sl.addWidget(form_host)
    layout.addWidget(split_host, 1)

    selected = {"comp": None, "port": None}

    def _rebuild():
        tree.blockSignals(True)
        tree.clear()
        n_ports = 0
        for comp in document.swc.components:
            item = QTreeWidgetItem([comp.name, "SWC", "", "", ""])
            item.setData(0, Qt.ItemDataRole.UserRole, ("comp", comp.name, ""))
            tree.addTopLevelItem(item)
            for p in comp.ports:
                n_ports += 1
                ch = QTreeWidgetItem([
                    p.name, "Port", p.signal, p.pdu, p.direction])
                ch.setData(
                    0, Qt.ItemDataRole.UserRole, ("port", comp.name, p.name))
                item.addChild(ch)
        tree.expandAll()
        tree.blockSignals(False)
        count.setText("%d SWC · %d ports" % (
            len(document.swc.components), n_ports))

    def _on_sel():
        item = tree.currentItem()
        if not item:
            return
        kind, cname, pname = item.data(0, Qt.ItemDataRole.UserRole)
        selected["comp"] = cname
        selected["port"] = pname if kind == "port" else None
        if kind != "port":
            return
        comp = next(
            (c for c in document.swc.components if c.name == cname), None)
        if not comp:
            return
        port = next((p for p in comp.ports if p.name == pname), None)
        if not port:
            return
        name_ed.setText(port.name)
        dir_cb.setCurrentText(port.direction)
        sig_ed.setText(port.signal)
        pdu_ed.setText(port.pdu)

    def _apply():
        if not selected["comp"] or not selected["port"]:
            return
        swc = document.swc.clone()
        comp = next((c for c in swc.components if c.name == selected["comp"]), None)
        if not comp:
            return
        port = next((p for p in comp.ports if p.name == selected["port"]), None)
        if not port:
            return
        port.name = name_ed.text().strip() or port.name
        port.direction = dir_cb.currentText()
        port.signal = sig_ed.text().strip()
        port.pdu = pdu_ed.text().strip()
        selected["port"] = port.name
        document.apply_swc(swc)
        log_fn("OK", "Updated port %s" % port.name)

    def _derive():
        name = "AppSwc"
        if document.manifest:
            name = "%sApp" % document.manifest.ecu_name
        document.derive_swc(name)
        log_fn("OK", "Derived SWC %s (%d ports)" % (
            name, sum(len(c.ports) for c in document.swc.components)))

    def _export():
        if not document.swc.components:
            document.derive_swc()
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export SWC-lite", "swc_lite.arxml", "ARXML (*.arxml)")
        if not path:
            return
        if not path.lower().endswith(".arxml"):
            path += ".arxml"
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(arxmlparse.serialize_swc_lite(document.swc))
            log_fn("OK", "Exported %s" % path)
        except OSError as e:
            log_fn("ERR", str(e))

    tree.itemSelectionChanged.connect(_on_sel)
    apply_btn.clicked.connect(_apply)
    derive_btn.clicked.connect(_derive)
    export_btn.clicked.connect(_export)
    document.on_changed(_rebuild)
    _rebuild()
    root.apply_from_menu = _apply
    return root
