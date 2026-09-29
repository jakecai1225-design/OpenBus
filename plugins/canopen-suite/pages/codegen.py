# -*- coding: utf-8 -*-
"""Codegen — emit CANopenNode V4 / CanFestival OD C/H from EDS draft."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QLineEdit,
    QMessageBox,
    QStackedWidget,
    QTabWidget,
    QTableWidget,
    QTableWidgetItem,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from _shared.canopen_codegen import TARGETS, generate, preflight
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    target = QComboBox()
    for tid, label in TARGETS:
        target.addItem(label, tid)
    target.setFixedHeight(_ui.CTRL_H)
    target.setMinimumWidth(220)
    target.setToolTip(
        "Firmware stack OD format — generates C/H from the EDS draft "
        "(CANopenNode V4 or CanFestival)")
    node_edit = QLineEdit("Node")
    node_edit.setFixedHeight(_ui.CTRL_H)
    node_edit.setMaximumWidth(140)
    node_edit.setToolTip("C identifier for CanFestival node / comments")
    gen_btn = _ui.primary_btn(
        "Generate", "Emit OD sources for the selected target", "play")
    save_btn = _ui.ghost_btn("Save…", "Write generated files to disk", "save")
    copy_btn = _ui.ghost_btn("Copy", "Copy active file to clipboard", "file")
    tgt_lab = _ui.field_label("Target")
    node_lab = _ui.field_label("Node")
    tool = _ui.tool_strip(
        tgt_lab, target, node_lab, node_edit,
        gen_btn, save_btn, copy_btn, stretch_at=4)
    layout.addWidget(tool)

    work = QWidget()
    body_lay = QVBoxLayout(work)
    body_lay.setContentsMargins(0, 0, 0, 0)
    body_lay.setSpacing(0)

    issues = QTableWidget(0, 3)
    issues.setHorizontalHeaderLabels(["Level", "Index", "Message"])
    issues.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
    issues.setMaximumHeight(120)
    issues.setVisible(False)
    _ui.style_table(issues)
    _ui.configure_columns(issues, stretch=2)
    body_lay.addWidget(issues)
    fix_btn = _ui.ghost_btn(
        "Fix in Objects",
        "Open Dictionary on the selected issue index (or double-click a row)",
        "eds")
    issues_next = _ui.next_step_bar(
        "Preflight issues — double-click a row or:", fix_btn)
    issues_next.setVisible(False)
    body_lay.addWidget(issues_next)

    tabs = QTabWidget()
    editors: dict[str, QTextEdit] = {}
    body_lay.addWidget(tabs, 1)

    empty_open = _ui.primary_btn(
        "Open Dictionary", "Create or edit an EDS draft first", "eds")
    empty_new = _ui.ghost_btn("New EDS…", "Start a new EDS file", "add")
    empty = _ui.empty_state(
        "No EDS draft to generate from",
        "Open or create an EDS, then return here to emit C/H sources.",
        actions=[empty_open, empty_new])

    stack = QStackedWidget()
    stack.addWidget(empty)
    stack.addWidget(work)
    layout.addWidget(stack, 1)

    cache = {"files": {}}

    def _has_draft() -> bool:
        return bool(session.draft_entries or session.od_entries)

    def _sync_empty(_=None):
        stack.setCurrentWidget(work if _has_draft() else empty)
        tool.setEnabled(_has_draft())

    def _doc():
        if hasattr(session, "to_document"):
            return session.to_document(use_draft=True)
        from _shared.edsparse import EdsDocument
        return EdsDocument(
            entries=list(session.draft_entries or session.od_entries),
            file_info=dict(session.eds_file_info or {}),
            device_info=dict(session.eds_device_info or {}),
            device_commissioning=dict(
                getattr(session, "eds_device_commissioning", {}) or {}),
            path=session.eds_path or "",
        )

    def _show_issues(findings):
        issues.setRowCount(0)
        for f in findings:
            row = issues.rowCount()
            issues.insertRow(row)
            for col, key in enumerate(("level", "index", "message")):
                issues.setItem(
                    row, col, QTableWidgetItem(str(f.get(key, ""))))
        _ui.fit_columns(issues, stretch=2)
        has = bool(findings)
        issues.setVisible(has)
        issues_next.setVisible(has)

    def _jump_issue(row, _col):
        item = issues.item(row, 1)
        if item is None:
            return
        text = (item.text() or "").strip()
        if not text:
            return
        try:
            if ":" in text:
                a, b = text.split(":", 1)
                idx = int(a, 16) if a.lower().startswith("0x") else int(a, 0)
                sub = int(b, 16) if b.lower().startswith("0x") else int(b, 0)
            else:
                idx = int(text, 16) if text.lower().startswith("0x") else int(text, 0)
                sub = 0
        except ValueError:
            return
        if hasattr(parent, "run_action"):
            parent.run_action("eds.focus", index=idx, subindex=sub)
        elif hasattr(parent, "goto_page"):
            parent.goto_page("eds_dict")

    def _fix_selected_issue():
        row = issues.currentRow()
        if row < 0 and issues.rowCount() > 0:
            row = 0
        if row >= 0:
            _jump_issue(row, 0)
        elif hasattr(parent, "run_action"):
            parent.run_action("view.eds")

    def _fill_tabs(files: dict):
        while tabs.count():
            tabs.removeTab(0)
        editors.clear()
        cache["files"] = dict(files)
        for name, text in files.items():
            ed = QTextEdit()
            ed.setReadOnly(True)
            ed.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
            ed.setPlainText(text)
            font = ed.font()
            font.setFamily("Consolas")
            font.setPointSize(10)
            ed.setFont(font)
            tabs.addTab(ed, name)
            editors[name] = ed

    def on_generate():
        doc = _doc()
        findings = preflight(doc)
        _show_issues(findings)
        tid = target.currentData()
        name = node_edit.text().strip() or "Node"
        try:
            files = generate(doc, tid, node_name=name, strict=True)
        except ValueError as exc:
            QMessageBox.warning(parent, "Codegen blocked", str(exc))
            log_fn("ERR", "-", b"", "Codegen: %s" % exc)
            return
        _fill_tabs(files)
        log_fn(
            "SYS", "-", b"",
            "Codegen %s → %s" % (tid, ", ".join(files.keys())))
        plugin_shell.set_status(
            parent, "Generated %s" % ", ".join(files.keys()), 4000)

    def on_save():
        if not cache["files"]:
            on_generate()
        if not cache["files"]:
            return
        directory = QFileDialog.getExistingDirectory(
            parent, "Save generated C/H", "")
        if not directory:
            return
        written = []
        try:
            for name, text in cache["files"].items():
                path = os.path.join(directory, name)
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
                written.append(path)
        except OSError as exc:
            QMessageBox.warning(parent, "Save failed", str(exc))
            return
        log_fn("RX", "-", b"", "Saved codegen: %s" % "; ".join(written))
        plugin_shell.set_status(parent, "Saved to %s" % directory, 4000)

    def on_copy():
        if not cache["files"]:
            on_generate()
        if not cache["files"]:
            return
        from PyQt6.QtWidgets import QApplication
        w = tabs.currentWidget()
        text = w.toPlainText() if isinstance(w, QTextEdit) else ""
        if not text:
            text = "\n\n".join(
                "// ===== %s =====\n%s" % (k, v)
                for k, v in cache["files"].items())
        QApplication.clipboard().setText(text)
        plugin_shell.set_status(parent, "Copied to clipboard", 2500)

    gen_btn.clicked.connect(on_generate)
    save_btn.clicked.connect(on_save)
    copy_btn.clicked.connect(on_copy)
    issues.cellDoubleClicked.connect(_jump_issue)
    fix_btn.clicked.connect(_fix_selected_issue)
    empty_open.clicked.connect(
        lambda: parent.run_action("view.eds")
        if hasattr(parent, "run_action") else None)
    empty_new.clicked.connect(
        lambda: parent.run_action("eds.new")
        if hasattr(parent, "run_action") else None)
    session.on_od_changed(_sync_empty)
    _sync_empty()
    return root
