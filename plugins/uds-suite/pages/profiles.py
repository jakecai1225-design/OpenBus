# -*- coding: utf-8 -*-
"""Profiles workspace — ECU profile / sequence file management."""

from __future__ import annotations

import os

from PyQt6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSpinBox,
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
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    layout.addWidget(plugin_shell.help_label(
        "Save ECU connection profiles (TX/RX/Func IDs, notes, DID catalog path) "
        "and manage sequence files for Batch. Apply writes IDs into the shared strip."))

    # ---- Profile editor ----
    box = QGroupBox("ECU profile")
    form = QFormLayout(box)

    name_edit = QLineEdit("Default ECU")
    tx_spin = QSpinBox()
    tx_spin.setRange(1, 0x7FF)
    tx_spin.setDisplayIntegerBase(16)
    tx_spin.setPrefix("0x")
    tx_spin.setValue(session.tx_id)
    func_spin = QSpinBox()
    func_spin.setRange(1, 0x7FF)
    func_spin.setDisplayIntegerBase(16)
    func_spin.setPrefix("0x")
    func_spin.setValue(session.func_id)
    rx_spin = QSpinBox()
    rx_spin.setRange(1, 0x7FF)
    rx_spin.setDisplayIntegerBase(16)
    rx_spin.setPrefix("0x")
    rx_spin.setValue(session.rx_id)
    did_edit = QLineEdit()
    did_edit.setPlaceholderText("Optional DID catalog path")
    notes_edit = QTextEdit()
    notes_edit.setMaximumHeight(80)
    notes_edit.setPlaceholderText("Notes")

    form.addRow("Name:", name_edit)
    form.addRow("TX ID:", tx_spin)
    form.addRow("Func ID:", func_spin)
    form.addRow("RX ID:", rx_spin)
    form.addRow("DID catalog:", did_edit)
    form.addRow("Notes:", notes_edit)
    layout.addWidget(box)

    btn_row = QHBoxLayout()
    load_btn = QPushButton("Load profile…")
    save_btn = QPushButton("Save profile…")
    apply_btn = QPushButton("Apply to connection")
    sync_btn = QPushButton("Sync from connection")
    for w in (load_btn, save_btn, apply_btn, sync_btn):
        btn_row.addWidget(w)
    btn_row.addStretch()
    layout.addLayout(btn_row)

    # ---- Sequence files ----
    seq_box = QGroupBox("Sequence files")
    seq_l = QVBoxLayout(seq_box)
    seq_path = QLineEdit()
    seq_path.setPlaceholderText("Path to JSON/CSV sequence")
    seq_row = QHBoxLayout()
    seq_browse = QPushButton("Browse…")
    seq_preview = QPushButton("Preview")
    seq_to_batch = QPushButton("Note: open Batch to run")
    seq_to_batch.setEnabled(False)
    seq_row.addWidget(seq_path, 1)
    seq_row.addWidget(seq_browse)
    seq_row.addWidget(seq_preview)
    seq_l.addLayout(seq_row)
    seq_info = QLabel("No sequence loaded")
    seq_l.addWidget(seq_info)
    layout.addWidget(seq_box)
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
            _plog("Sequence preview: %d steps" % len(steps))
        except (OSError, ValueError, KeyError) as e:
            seq_info.setText("Error: %s" % e)

    load_btn.clicked.connect(_on_load)
    save_btn.clicked.connect(_on_save)
    apply_btn.clicked.connect(_on_apply)
    sync_btn.clicked.connect(_on_sync)
    seq_browse.clicked.connect(_on_seq_browse)
    seq_preview.clicked.connect(_preview)

    saved = state_store.load_state(PLUGIN_ID, "profile.json")
    if saved:
        _fill(saved)
    else:
        _on_sync()

    meta = state_store.load_state(PLUGIN_ID, "profiles_meta.json") or {}
    if meta.get("last_sequence"):
        seq_path.setText(str(meta["last_sequence"]))

    session.on_ids_changed(_on_sync)
    return root
