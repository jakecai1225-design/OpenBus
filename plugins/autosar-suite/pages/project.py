# -*- coding: utf-8 -*-
"""Project — role-tagged ARXML workspace (DaVinci-style lite)."""

from __future__ import annotations

import os
import subprocess
import sys

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QInputDialog,
    QLineEdit,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import arxml_project, suite_chrome, vscode_theme
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    new_btn = _ui.primary_btn("New project…", "Create folder + project.json", "add")
    open_btn = _ui.ghost_btn("Open…", "Open project folder", "folder")
    save_btn = _ui.ghost_btn("Save project", "Write intermediates + manifest", "save")
    derive_btn = _ui.ghost_btn("Derive ECUC", "Rebuild ECUC-lite from COM", "refresh")
    write_btn = _ui.ghost_btn(
        "Write intermediates", "work/com.arxml + work/ecuc_com.arxml", "export")
    out_btn = _ui.ghost_btn("Write out/", "HTML report + SARIF findings", "export")
    explore_btn = _ui.ghost_btn("", "Open project folder in Explorer", "folder")
    for w in (new_btn, open_btn, save_btn, derive_btn, write_btn, out_btn, explore_btn):
        crow.addWidget(w)
    crow.addStretch(1)
    status = _ui.quiet_label("No project")
    crow.addWidget(status)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    suite_chrome.page_margins(bl, top=12)
    bl.setSpacing(10)

    form = QFormLayout()
    vscode_theme.tune_form(form)
    name_ed = QLineEdit()
    name_ed.setFixedHeight(28)
    name_ed.setMaximumWidth(280)
    ecu_ed = QLineEdit()
    ecu_ed.setFixedHeight(28)
    ecu_ed.setMaximumWidth(280)
    root_lbl = _ui.quiet_label("(none)")
    form.addRow("Name", name_ed)
    form.addRow("ECU", ecu_ed)
    form.addRow("Root", root_lbl)
    bl.addLayout(form)

    tip = _ui.quiet_label(
        "Roles label each ARXML (System / Extract / COM / ECUC). "
        "Intermediates are ARXML only — no BSW/RTE codegen.")
    tip.setWordWrap(True)
    tip.setMaximumWidth(560)
    bl.addWidget(tip)

    table = QTableWidget(0, 4)
    table.setObjectName("SuiteTable")
    table.setHorizontalHeaderLabels(["Role", "Relative path", "Exists", "Absolute"])
    table.verticalHeader().setVisible(False)
    table.setEditTriggers(table.EditTrigger.NoEditTriggers)
    table.setSelectionBehavior(table.SelectionBehavior.SelectRows)
    table.setAlternatingRowColors(True)
    hdr = table.horizontalHeader()
    hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
    hdr.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    table.setMaximumHeight(180)
    bl.addWidget(table)

    assign_row = QHBoxLayout()
    assign_btn = _ui.ghost_btn(
        "Assign COM file…", "Copy/link an existing ARXML as work/com.arxml", "file")
    dbc_btn = _ui.ghost_btn(
        "Import DBC…", "DBC → COM I-PDUs/signals; sync Comm BSW", "import")
    assign_row.addWidget(assign_btn)
    assign_row.addWidget(dbc_btn)
    assign_row.addStretch(1)
    bl.addLayout(assign_row)
    bl.addStretch(1)
    layout.addWidget(body, 1)

    def _refresh():
        if not document.has_project():
            status.setText("No project — New or Open")
            name_ed.setText("")
            ecu_ed.setText("")
            root_lbl.setText("(none)")
            table.setRowCount(0)
            return
        m = document.manifest
        status.setText(
            "Project · derived_dirty=%s" % bool(m.derived_dirty))
        name_ed.setText(m.name)
        ecu_ed.setText(m.ecu_name)
        root_lbl.setText(m.root)
        rows = arxml_project.list_role_status(m)
        table.setRowCount(len(rows))
        for i, r in enumerate(rows):
            table.setItem(i, 0, QTableWidgetItem(r["role"]))
            table.setItem(i, 1, QTableWidgetItem(r["rel"]))
            table.setItem(i, 2, QTableWidgetItem("yes" if r["exists"] else "no"))
            table.setItem(i, 3, QTableWidgetItem(r["path"]))

    def _apply_meta():
        if not document.has_project():
            return
        document.manifest.name = name_ed.text().strip() or document.manifest.name
        document.manifest.ecu_name = ecu_ed.text().strip() or document.manifest.ecu_name
        document.dirty = True
        document._notify()

    def _new():
        if hasattr(shell, "_confirm_discard") and not shell._confirm_discard():
            return
        path = QFileDialog.getExistingDirectory(shell, "New project folder")
        if not path:
            return
        name, ok = QInputDialog.getText(
            shell, "Project name", "ECU / project name:", text="BodyEcu")
        if not ok or not name.strip():
            return
        try:
            document.new_project(path, name=name.strip(), ecu_name=name.strip())
            log_fn("OK", "Created project %s" % path)
            if hasattr(shell, "_remember_project"):
                shell._remember_project(path)
            shell.goto_page("editor")
        except OSError as e:
            log_fn("ERR", str(e))

    def _open():
        if hasattr(shell, "_confirm_discard") and not shell._confirm_discard():
            return
        path = QFileDialog.getExistingDirectory(shell, "Open project folder")
        if not path:
            return
        if not arxml_project.is_project_dir(path):
            log_fn("ERR", "No project.json in %s" % path)
            return
        try:
            document.open_project(path)
            log_fn("OK", "Opened project %s" % path)
            if hasattr(shell, "_remember_project"):
                shell._remember_project(path)
        except OSError as e:
            log_fn("ERR", str(e))

    def _save():
        if not document.has_project():
            log_fn("WARN", "No project open")
            return
        _apply_meta()
        try:
            document.save_project()
            log_fn("OK", "Saved project %s" % document.project_root)
            if hasattr(shell, "_notify_host"):
                shell._notify_host(document.handoff_com_path())
        except (OSError, ValueError) as e:
            log_fn("ERR", str(e))

    def _derive():
        if not document.model.ipdus and not document.has_project():
            log_fn("WARN", "Load COM first")
            return
        document.derive_ecuc()
        log_fn("OK", "Derived ECUC-lite (%d modules)" % len(document.ecuc.modules))
        document.set_editor_mode("ecuc")
        shell.goto_page("editor")

    def _write():
        if not document.has_project():
            log_fn("WARN", "Open or create a project first")
            return
        try:
            paths = document.write_intermediates()
            log_fn("OK", "Wrote %s" % paths.get("com", ""))
            if hasattr(shell, "_notify_host"):
                shell._notify_host(paths.get("com", ""))
            _refresh()
        except (OSError, ValueError) as e:
            log_fn("ERR", str(e))

    def _out():
        if not document.has_project():
            log_fn("WARN", "No project")
            return
        try:
            paths = document.write_out_reports()
            log_fn("OK", "Wrote %s" % paths.get("sarif", ""))
        except (OSError, ValueError) as e:
            log_fn("ERR", str(e))

    def _explore():
        root_path = document.project_root
        if not root_path or not os.path.isdir(root_path):
            log_fn("WARN", "No project folder")
            return
        try:
            if sys.platform.startswith("win"):
                os.startfile(root_path)
            elif sys.platform == "darwin":
                subprocess.Popen(["open", root_path])
            else:
                subprocess.Popen(["xdg-open", root_path])
        except OSError as e:
            log_fn("ERR", str(e))

    def _assign_com():
        if not document.has_project():
            log_fn("WARN", "Create a project first")
            return
        path, _ = QFileDialog.getOpenFileName(
            shell, "Import COM ARXML", "", "ARXML (*.arxml *.xml);;All (*)")
        if not path:
            return
        try:
            from _shared import arxmlparse
            model = arxmlparse.parse_arxml_model(path)
            document.apply_model(model)
            document.derive_ecuc()
            document.write_intermediates()
            log_fn("OK", "Imported COM → work/com.arxml")
            _refresh()
        except OSError as e:
            log_fn("ERR", str(e))

    def _import_dbc():
        path, _ = QFileDialog.getOpenFileName(
            shell, "Import DBC", "", "DBC (*.dbc);;All (*)")
        if not path:
            return
        try:
            n = document.import_dbc(path)
            log_fn("OK", "Imported DBC → %d PDUs; BSW synced" % n)
            if document.has_project():
                try:
                    document.write_intermediates()
                except (OSError, ValueError):
                    pass
            _refresh()
            shell.goto_page("editor")
        except (OSError, ValueError) as e:
            log_fn("ERR", str(e))

    name_ed.editingFinished.connect(_apply_meta)
    ecu_ed.editingFinished.connect(_apply_meta)
    new_btn.clicked.connect(_new)
    open_btn.clicked.connect(_open)
    save_btn.clicked.connect(_save)
    derive_btn.clicked.connect(_derive)
    write_btn.clicked.connect(_write)
    out_btn.clicked.connect(_out)
    explore_btn.clicked.connect(_explore)
    assign_btn.clicked.connect(_assign_com)
    dbc_btn.clicked.connect(_import_dbc)
    document.on_changed(_refresh)
    _refresh()
    root.refresh = _refresh
    return root
