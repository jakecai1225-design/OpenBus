# -*- coding: utf-8 -*-
"""Profiles workspace — ECU profile / sequence file management."""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell, state_store
from core import (
    default_profile,
    load_profile_file,
    save_profile_file,
    load_sequence,
)

PLUGIN_ID = "uds-suite"


def build(parent, session, log_fn) -> QWidget:
    from _shared import vscode_theme, codicons

    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(16, 12, 16, 12)
    layout.setSpacing(14)

    cols = QHBoxLayout()
    cols.setSpacing(8)

    box, box_body = vscode_theme.block(
        "ECU profile",
        "Name, TX / Func / RX, notes. Apply writes these IDs into the shared connection.",
    )
    form_host = QWidget()
    form = QFormLayout(form_host)
    vscode_theme.tune_form(form)
    form.setContentsMargins(0, 0, 0, 0)

    name_edit = QLineEdit("Default ECU")
    from widgets.step_spin import StepSpin
    tx_spin = StepSpin(session.tx_id, minimum=1, maximum=0x7FF, hex_mode=True, width=110)
    func_spin = StepSpin(session.func_id, minimum=1, maximum=0x7FF, hex_mode=True, width=110)
    rx_spin = StepSpin(session.rx_id, minimum=1, maximum=0x7FF, hex_mode=True, width=110)
    did_edit = QLineEdit()
    did_edit.setPlaceholderText("Optional DID catalog path")
    notes_edit = QTextEdit()
    notes_edit.setMaximumHeight(80)
    notes_edit.setPlaceholderText("Notes")

    form.addRow(vscode_theme.field_label("Name"), name_edit)
    form.addRow(vscode_theme.field_label("TX ID"), tx_spin)
    form.addRow(vscode_theme.field_label("Func ID"), func_spin)
    form.addRow(vscode_theme.field_label("RX ID"), rx_spin)
    form.addRow(vscode_theme.field_label("DID catalog"), did_edit)
    form.addRow(vscode_theme.field_label("Notes"), notes_edit)
    vscode_theme.polish_form_labels(form)
    box_body.addWidget(form_host)

    btn_row = QHBoxLayout()
    btn_row.setSpacing(8)
    load_btn = QPushButton("Load")
    save_btn = QPushButton("Save")
    apply_btn = QPushButton("Apply")
    apply_btn.setObjectName("PrimaryButton")
    sync_btn = QPushButton("Sync")
    for w, ic, primary in (
        (load_btn, "load", False),
        (save_btn, "save", False),
        (apply_btn, "apply", True),
        (sync_btn, "sync", False),
    ):
        w.setFixedHeight(28)
        w.setToolTip({
            load_btn: "Load a profile file",
            save_btn: "Save this profile",
            apply_btn: "Write these IDs into the shared connection",
            sync_btn: "Copy IDs from the shared connection",
        }[w])
        codicons.set_button(w, ic, primary=primary)
        btn_row.addWidget(w)
    btn_row.addStretch()
    box_body.addLayout(btn_row)
    cols.addWidget(box, 1)

    seq_card, seq_body = vscode_theme.block(
        "Sequence file",
        "Preview a JSON or CSV sequence. Run it from the Batch page.",
    )
    seq_path = QLineEdit()
    seq_path.setPlaceholderText("Path to JSON or CSV sequence")
    seq_row = QHBoxLayout()
    seq_row.setSpacing(8)
    seq_browse = QPushButton("Browse")
    seq_browse.setFixedSize(96, 28)
    codicons.set_button(seq_browse, "browse")
    seq_preview = QPushButton("Preview")
    seq_preview.setFixedSize(96, 28)
    codicons.set_button(seq_preview, "search")
    seq_to_batch = QPushButton("Open Batch to run")
    seq_to_batch.setEnabled(False)
    seq_to_batch.setFixedHeight(28)
    codicons.set_button(seq_to_batch, "batch")
    seq_row.addWidget(seq_path, 1)
    seq_row.addWidget(seq_browse)
    seq_row.addWidget(seq_preview)
    seq_body.addLayout(seq_row)
    seq_info = QLabel("No sequence loaded")
    seq_info.setObjectName("SuiteStatusMuted")
    seq_info.setWordWrap(True)
    seq_info.setToolTip("Sequence preview summary")
    seq_body.addWidget(seq_info)
    seq_body.addWidget(seq_to_batch, 0, Qt.AlignmentFlag.AlignLeft)
    seq_body.addStretch(1)
    cols.addWidget(seq_card, 1)
    layout.addLayout(cols)
    layout.addStretch()

    def _plog(text):
        log_fn("RX", "-", b"", "[Profiles] %s" % text)

    def _current():
        p = default_profile()
        p["name"] = name_edit.text().strip() or "ECU"
        p["tx_id"] = tx_spin.value()
        p["func_id"] = func_spin.value()
        p["rx_id"] = rx_spin.value()
        p["did_catalog"] = did_edit.text().strip()
        p["notes"] = notes_edit.toPlainText()
        return p

    def _fill(p):
        if not p:
            return
        name_edit.setText(str(p.get("name", "")))
        tx_spin.setValue(int(p.get("tx_id", 0x7E0)))
        func_spin.setValue(int(p.get("func_id", 0x7DF)))
        rx_spin.setValue(int(p.get("rx_id", 0x7E8)))
        did_edit.setText(str(p.get("did_catalog", "")))
        notes_edit.setPlainText(str(p.get("notes", "")))

    def _on_sync():
        tx_spin.setValue(session.tx_id)
        func_spin.setValue(session.func_id)
        rx_spin.setValue(session.rx_id)

    def _on_apply():
        session.apply_ids(tx_spin.value(), rx_spin.value(), func_spin.value())
        state_store.save_state(PLUGIN_ID, _current(), "profile.json")
        _plog("Applied profile '%s' to connection" % name_edit.text())
        plugin_shell.set_status(parent.window(), "Profile applied", 3000)

    def _on_save():
        p = _current()
        path = state_store.save_state(PLUGIN_ID, p, "profile.json")
        also, _ = QFileDialog.getSaveFileName(
            root, "Export profile JSON", "ecu_profile.json", "JSON (*.json)")
        if also:
            save_profile_file(also, p)
            path = also
        _plog("Profile saved: %s" % path)
        QMessageBox.information(root, "Saved", "Profile saved:\n%s" % path)

    def _on_load():
        path, _ = QFileDialog.getOpenFileName(
            root, "Load ECU profile", "", "JSON (*.json)")
        p = None
        if path:
            p = load_profile_file(path)
        else:
            p = state_store.load_state(PLUGIN_ID, "profile.json")
        if not p:
            QMessageBox.information(root, "Profile", "No profile loaded")
            return
        _fill(p)
        _plog("Loaded profile: %s" % p.get("name", path or "state"))

    def _on_seq_browse():
        path, _ = QFileDialog.getOpenFileName(
            root, "Sequence file", "", "JSON/CSV (*.json *.csv)")
        if path:
            seq_path.setText(path)
            _preview()

    def _preview():
        path = seq_path.text().strip()
        if not path or not os.path.isfile(path):
            seq_info.setText("File not found")
            return
        try:
            steps = load_sequence(path)
            seq_info.setText("%d steps in %s" % (len(steps), os.path.basename(path)))
            state_store.save_state(PLUGIN_ID, {"last_sequence": path}, "profiles_meta.json")
            seq_to_batch.setEnabled(True)
            _plog("Sequence preview: %d steps" % len(steps))
        except (OSError, ValueError, KeyError) as e:
            seq_info.setText("Error: %s" % e)
            seq_to_batch.setEnabled(False)

    def _open_batch():
        shell = parent
        if hasattr(shell, "run_action"):
            shell.run_action("uds.goto", page="batch")
        elif hasattr(shell, "goto_page"):
            shell.goto_page("batch")

    load_btn.clicked.connect(_on_load)
    save_btn.clicked.connect(_on_save)
    apply_btn.clicked.connect(_on_apply)
    sync_btn.clicked.connect(_on_sync)
    seq_browse.clicked.connect(_on_seq_browse)
    seq_preview.clicked.connect(_preview)
    seq_to_batch.clicked.connect(_open_batch)

    saved = state_store.load_state(PLUGIN_ID, "profile.json")
    if saved:
        _fill(saved)
    else:
        _on_sync()

    meta = state_store.load_state(PLUGIN_ID, "profiles_meta.json") or {}
    if meta.get("last_sequence"):
        seq_path.setText(str(meta["last_sequence"]))

    session.on_ids_changed(_on_sync)
    from pages import _ui
    _ui.polish_work_surface(root)
    return root
