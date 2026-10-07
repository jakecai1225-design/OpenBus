# -*- coding: utf-8 -*-
"""Export workspace — matrix formats + C codegen (VS Code–style layout)."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QSplitter,
    QTabWidget,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from pages import _ui
from _shared import codicons, plugin_shell, state_store, suite_chrome, vscode_theme
from core import codegen, export_matrix

PLUGIN_ID = "dbc-studio"

# Legacy combo labels → format ids
_LEGACY_FMT = {
    "CSV": "csv",
    "JSON": "json",
    "HTML": "html",
}


def _open_path(path: str) -> None:
    if not path or not os.path.isfile(path):
        return
    try:
        os.startfile(path)  # Windows
    except Exception:
        try:
            import subprocess
            subprocess.Popen(["xdg-open", path])  # noqa: S603
        except Exception:
            pass


def _build_matrix_pane(shell, document, log_fn) -> QWidget:
    """Left: format list · Right: description + options + export (VS Code picker)."""
    root = QWidget()
    outer = QVBoxLayout(root)
    suite_chrome.page_margins(outer)

    intro = QLabel(
        "Export the signal matrix for review, CI, or downstream tools. "
        "Choose a format on the left — same idea as VS Code “Save As” / export pickers."
    )
    intro.setWordWrap(True)
    intro.setObjectName("SuiteHint")
    intro.setStyleSheet("color: %s; font-size: 12px;" % vscode_theme.TEXT_DIM)
    outer.addWidget(intro)

    split = QSplitter(Qt.Orientation.Horizontal)
    split.setChildrenCollapsible(False)
    outer.addWidget(split, 1)

    # ---- Format list (left) ----
    left = QWidget()
    left.setObjectName("SuiteSideBar")
    ll = QVBoxLayout(left)
    ll.setContentsMargins(0, 0, 8, 0)
    ll.setSpacing(6)
    ll.addWidget(_ui.sidebar_header("Formats"))

    fmt_list = QListWidget()
    fmt_list.setObjectName("SuiteSideList")
    fmt_list.setUniformItemSizes(True)
    fmt_list.setSpacing(1)
    for fmt_id, label, _ext, _filt, desc in export_matrix.FORMATS:
        item = QListWidgetItem(label)
        item.setData(Qt.ItemDataRole.UserRole, fmt_id)
        item.setToolTip(desc)
        fmt_list.addItem(item)
    ll.addWidget(fmt_list, 1)
    split.addWidget(left)

    # ---- Options (right) ----
    right = QWidget()
    rl = QVBoxLayout(right)
    rl.setContentsMargins(8, 0, 0, 0)
    rl.setSpacing(10)

    title = QLabel("CSV")
    title.setObjectName("SuitePageTitle")
    title.setStyleSheet(
        "font-size: 15px; font-weight: 600; color: %s;" % vscode_theme.TEXT)
    rl.addWidget(title)

    desc_lbl = QLabel(export_matrix.FORMATS[0][4])
    desc_lbl.setWordWrap(True)
    desc_lbl.setStyleSheet(
        "color: %s; font-size: 12px; line-height: 1.4;" % vscode_theme.TEXT_DIM)
    rl.addWidget(desc_lbl)

    sep = QFrame()
    sep.setFrameShape(QFrame.Shape.HLine)
    sep.setStyleSheet("color: %s;" % vscode_theme.BORDER)
    rl.addWidget(sep)

    of = QFormLayout()
    vscode_theme.tune_form(of)
    opt_minmax = QCheckBox("Include min / max")
    opt_minmax.setChecked(True)
    opt_nodes = QCheckBox("Include receivers")
    opt_nodes.setChecked(True)
    opt_values = QCheckBox("Include value tables")
    opt_comment = QCheckBox("Include comments")
    open_after = QCheckBox("Open file after export")
    open_after.setChecked(True)
    open_after.setToolTip("Launch the exported file with the system default app")
    of.addRow(vscode_theme.field_label("Columns"), opt_minmax)
    of.addRow("", opt_nodes)
    of.addRow("", opt_values)
    of.addRow("", opt_comment)
    of.addRow(vscode_theme.field_label("After export"), open_after)
    vscode_theme.polish_form_labels(of)
    rl.addLayout(of)

    preview = QTextEdit()
    preview.setObjectName("SuiteCode")
    preview.setReadOnly(True)
    preview.setMaximumHeight(120)
    preview.setPlaceholderText("Format notes appear here")
    rl.addWidget(preview)

    mrow = QHBoxLayout()
    mrow.addStretch(1)
    export_btn = QPushButton("Export…")
    export_btn.setFixedHeight(_ui.CTRL_H)
    codicons.set_button(export_btn, "export", primary=True)
    mrow.addWidget(export_btn)
    rl.addLayout(mrow)
    rl.addStretch(1)
    split.addWidget(right)
    split.setStretchFactor(0, 0)
    split.setStretchFactor(1, 1)
    split.setSizes([200, 520])

    def _current_fmt() -> str:
        item = fmt_list.currentItem()
        if not item:
            return "csv"
        return item.data(Qt.ItemDataRole.UserRole) or "csv"

    def _opts():
        return {
            "minmax": opt_minmax.isChecked(),
            "nodes": opt_nodes.isChecked(),
            "values": opt_values.isChecked(),
            "comment": opt_comment.isChecked(),
        }

    def _refresh_detail():
        meta = export_matrix.format_meta(_current_fmt())
        if not meta:
            return
        fmt_id, label, ext, _filt, desc = meta
        title.setText("%s  (%s)" % (label, ext))
        desc_lbl.setText(desc)
        notes = [
            "Extension: %s" % ext,
            "Typical use: %s" % desc.split("—")[-1].strip()
            if "—" in desc else desc,
            "",
            "Optional columns are controlled by the checkboxes above.",
        ]
        if fmt_id in ("csv", "excel-csv", "tsv"):
            notes.append("Row = one signal; message fields repeat per signal.")
        elif fmt_id in ("json", "yaml", "xml"):
            notes.append("Tree: messages[] → signals[].")
        elif fmt_id in ("markdown", "html"):
            notes.append("Human-readable report for reviews and docs.")
        preview.setPlainText("\n".join(notes))

    def _on_export():
        fmt_id = _current_fmt()
        meta = export_matrix.format_meta(fmt_id)
        if not meta:
            return
        _fid, label, ext, filt, _desc = meta
        default = "dbc_matrix%s" % ext
        path, _ = QFileDialog.getSaveFileName(
            shell, "Export %s" % label, default, filt)
        if not path:
            return
        opts = _opts()
        try:
            export_matrix.export_to_path(path, document.db, opts, fmt_id)
        except OSError as exc:
            plugin_shell.set_status(shell, "Export failed: %s" % exc, 4000)
            log_fn("Export", "Failed %s: %s" % (label, exc))
            return
        state_store.save_state(PLUGIN_ID, {
            "export_format": fmt_id,
            "export_opts": opts,
            "open_after": open_after.isChecked(),
        }, "export.json")
        log_fn("Export", "Matrix %s → %s" % (label, path))
        plugin_shell.set_status(shell, "Exported %s" % label, 2500)
        if open_after.isChecked():
            _open_path(path)

    fmt_list.currentItemChanged.connect(lambda _c, _p: _refresh_detail())
    export_btn.clicked.connect(_on_export)

    # Restore state
    saved = state_store.load_state(PLUGIN_ID, "export.json") or {}
    raw_fmt = saved.get("export_format", "csv")
    if raw_fmt in _LEGACY_FMT:
        raw_fmt = _LEGACY_FMT[raw_fmt]
    for i in range(fmt_list.count()):
        it = fmt_list.item(i)
        if it and it.data(Qt.ItemDataRole.UserRole) == raw_fmt:
            fmt_list.setCurrentRow(i)
            break
    else:
        fmt_list.setCurrentRow(0)

    opts = saved.get("export_opts") or {}
    opt_minmax.setChecked(bool(opts.get("minmax", True)))
    opt_nodes.setChecked(bool(opts.get("nodes", True)))
    opt_values.setChecked(bool(opts.get("values", False)))
    opt_comment.setChecked(bool(opts.get("comment", False)))
    if "open_after" in saved:
        open_after.setChecked(bool(saved["open_after"]))

    _refresh_detail()
    return root


def _build_codegen_pane(shell, document, log_fn) -> QWidget:
    code = QWidget()
    cl = QVBoxLayout(code)
    suite_chrome.page_margins(cl)

    hint = QLabel(
        "Generate pack / unpack stubs from the loaded DBC. "
        "Preview first, then save `.h` / `.c` — similar to VS Code codegen extensions."
    )
    hint.setWordWrap(True)
    hint.setStyleSheet("color: %s; font-size: 12px;" % vscode_theme.TEXT_DIM)
    cl.addWidget(hint)

    crow = QHBoxLayout()
    crow.setSpacing(8)
    crow.addWidget(QLabel("Identifier style"))
    style = QComboBox()
    style.addItems(["keep", "upper"])
    style.setFixedHeight(_ui.CTRL_H)
    crow.addWidget(style)
    gen_btn = QPushButton("Generate")
    gen_btn.setFixedHeight(_ui.CTRL_H)
    codicons.set_button(gen_btn, "apply", primary=True)
    crow.addWidget(gen_btn)
    crow.addStretch(1)
    open_code = QCheckBox("Open after save")
    open_code.setChecked(True)
    crow.addWidget(open_code)
    save_h = QPushButton("Save .h")
    save_h.setObjectName("GhostButton")
    save_h.setFixedHeight(_ui.CTRL_H)
    codicons.set_button(save_h, "save")
    save_c = QPushButton("Save .c")
    save_c.setObjectName("GhostButton")
    save_c.setFixedHeight(_ui.CTRL_H)
    codicons.set_button(save_c, "save")
    crow.addWidget(save_h)
    crow.addWidget(save_c)
    cl.addLayout(crow)

    preview = QTextEdit()
    preview.setObjectName("SuiteCode")
    preview.setReadOnly(True)
    preview.setPlaceholderText("Generated C pack / unpack preview")
    cl.addWidget(preview, 1)

    generated = {"h": "", "c": ""}

    def _on_gen():
        h, c = codegen.generate_c(document.db, style.currentText())
        generated["h"], generated["c"] = h, c
        preview.setPlainText(
            "// ---- header ----\n" + h + "\n// ---- source ----\n" + c)
        log_fn("Export", "Generated C for %d messages" % len(
            document.db.messages))
        plugin_shell.set_status(shell, "C codegen ready", 2500)
        state_store.save_state(PLUGIN_ID, {
            "open_code": open_code.isChecked(),
            "codegen_style": style.currentText(),
        }, "export_codegen.json")

    def _save(which: str):
        if not generated[which]:
            _on_gen()
        default = "dbc_codec.h" if which == "h" else "dbc_codec.c"
        path, _ = QFileDialog.getSaveFileName(
            shell, "Save", default,
            "C Header (*.h)" if which == "h" else "C Source (*.c)")
        if not path:
            return
        with open(path, "w", encoding="utf-8") as f:
            f.write(generated[which])
        log_fn("Export", "Wrote %s" % path)
        if open_code.isChecked():
            _open_path(path)

    gen_btn.clicked.connect(_on_gen)
    save_h.clicked.connect(lambda: _save("h"))
    save_c.clicked.connect(lambda: _save("c"))

    saved = state_store.load_state(PLUGIN_ID, "export_codegen.json") or {}
    # migrate from old combined export.json
    legacy = state_store.load_state(PLUGIN_ID, "export.json") or {}
    if "open_code" in saved:
        open_code.setChecked(bool(saved["open_code"]))
    elif "open_code" in legacy:
        open_code.setChecked(bool(legacy["open_code"]))
    if saved.get("codegen_style") in ("keep", "upper"):
        style.setCurrentText(saved["codegen_style"])

    return code


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    tabs = QTabWidget()
    tabs.setObjectName("SuiteEditorTabs")
    tabs.setDocumentMode(True)
    layout.addWidget(tabs, 1)

    tabs.addTab(_build_matrix_pane(shell, document, log_fn), "Matrix")
    tabs.addTab(_build_codegen_pane(shell, document, log_fn), "C Codegen")
    return root
