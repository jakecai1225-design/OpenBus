# -*- coding: utf-8 -*-
"""dbc-merge — merge multiple DBC files with ID conflict policy.

Policies: skip (keep first), rename (later name + keep later), prefer-A (keep first).
"""

from __future__ import annotations

import copy
import os
import sys

_PLUGIN_DIR = os.path.dirname(os.path.abspath(__file__))
_PLUGINS_ROOT = os.path.dirname(_PLUGIN_DIR)
if _PLUGINS_ROOT not in sys.path:
    sys.path.insert(0, _PLUGINS_ROOT)

from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "dbc-merge"

# Conflict policies (combo indices)
POLICY_SKIP = 0
POLICY_RENAME = 1
POLICY_PREFER_A = 2

_win = None


def _clone_message(m):
    return copy.deepcopy(m)


def merge_dbc(
    base: "dbcparse.DbcFile",
    incoming: "dbcparse.DbcFile",
    source_name: str,
    policy: int,
    conflicts: list,
) -> None:
    """Merge incoming into base; append conflict tuples (cid, kept, other, source, action)."""
    for cid, m in incoming.messages.items():
        msg = _clone_message(m)
        if cid not in base.messages:
            base.messages[cid] = msg
            continue
        existing = base.messages[cid]
        if policy == POLICY_SKIP or policy == POLICY_PREFER_A:
            action = "skip" if policy == POLICY_SKIP else "prefer-A"
            conflicts.append((cid, existing.name, msg.name, source_name, action))
            continue
        # rename: keep later content, rename message with ID suffix
        new_name = "%s_%X" % (msg.name[:40], cid)
        conflicts.append((cid, new_name, existing.name, source_name, "rename"))
        msg.name = new_name
        base.messages[cid] = msg

    for n in incoming.nodes:
        if n not in base.nodes:
            base.nodes.append(n)


def activate(context):
    global _win
    import sin

    try:
        from PyQt6.QtWidgets import (
            QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
            QTreeWidget, QTreeWidgetItem, QFileDialog, QMessageBox,
            QHeaderView, QComboBox, QTextEdit, QListWidget,
        )
    except ImportError:
        sin.output.append("dbc-merge requires PyQt6: pip install PyQt6")
        return

    win = sin.ui.create_window("DBC Merge")
    win.resize(980, 660)
    plugin_shell.attach_status_bar(win, "Ready — add DBC files to merge")
    _win = win

    merged = dbcparse.DbcFile()
    merged.version = "merged-by-openbus"
    conflicts: list = []
    loaded_files: list[str] = []

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    top = QHBoxLayout()
    add_btn = QPushButton("Add DBC…")
    strategy_label = QLabel("Conflict policy:")
    strategy = QComboBox()
    strategy.addItems([
        "Skip (keep first)",
        "Rename (suffix ID, keep later)",
        "Prefer A (keep first)",
    ])
    state_label = QLabel("Merged: 0 files, 0 messages")
    top.addWidget(add_btn)
    top.addWidget(strategy_label)
    top.addWidget(strategy)
    top.addStretch(1)
    top.addWidget(state_label)
    layout.addLayout(top)

    layout.addWidget(plugin_shell.help_label(
        "Add DBC files in order (first file is A). On ID conflict: Skip / Prefer A "
        "keep the first message; Rename keeps the later message under a name_ID. "
        "Export a conflict CSV or save the merged DBC."))

    mid = QHBoxLayout()
    file_list = QListWidget()
    file_list.setMaximumHeight(90)
    mid.addWidget(file_list, 1)
    layout.addLayout(mid)

    btns = QHBoxLayout()
    save_btn = QPushButton("Save merged DBC…")
    export_conf_btn = QPushButton("Export conflict CSV")
    clear_btn = QPushButton("Clear")
    btns.addStretch(1)
    btns.addWidget(save_btn)
    btns.addWidget(export_conf_btn)
    btns.addWidget(clear_btn)
    layout.addLayout(btns)

    empty = plugin_shell.empty_state_label(
        "No files merged yet.\nAdd one or more DBC files from the workspace or disk.")
    layout.addWidget(empty)

    tree = QTreeWidget()
    tree.setHeaderLabels(["ID", "Name", "DLC", "Signals", "Cycle", "Sender"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tree.hide()
    layout.addWidget(tree, 1)

    conf_view = QTextEdit()
    conf_view.setReadOnly(True)
    conf_view.setPlaceholderText("Conflict log")
    conf_view.setMaximumHeight(140)
    layout.addWidget(conf_view)

    def _persist():
        return state_store.save_state(PLUGIN_ID, {
            "inputs": list(loaded_files),
            "policy_index": strategy.currentIndex(),
        })

    def _refresh_conf():
        if not conflicts:
            conf_view.setPlainText("No conflicts")
            return
        lines = []
        for cid, kept, other, src, action in conflicts:
            lines.append(
                "ID 0x%X [%s]: kept %s | other %s | from %s"
                % (cid, action, kept, other, src))
        conf_view.setPlainText("\n".join(lines))

    def _refresh():
        tree.clear()
        file_list.clear()
        for p in loaded_files:
            file_list.addItem(p)
        for cid, m in merged.messages.items():
            tree.addTopLevelItem(QTreeWidgetItem([
                "0x%X" % cid, m.name, str(m.dlc), str(len(m.signals)),
                str(m.cycle_time), m.sender or ""]))
        state_label.setText(
            "Merged: %d files, %d messages, %d conflicts"
            % (len(loaded_files), len(merged.messages), len(conflicts)))
        if merged.messages:
            empty.hide()
            tree.show()
        else:
            tree.hide()
            empty.show()
        _refresh_conf()

    def _reset_merge():
        merged.messages.clear()
        merged.nodes.clear()
        conflicts.clear()

    def _rebuild_from_files():
        """Re-merge all loaded files with current policy (for restore)."""
        _reset_merge()
        policy = strategy.currentIndex()
        for path in list(loaded_files):
            try:
                db = dbcparse.parse_file(path)
            except OSError:
                continue
            if not db.messages:
                continue
            merge_dbc(merged, db, os.path.basename(path), policy, conflicts)

    def _on_add():
        path = dbc_picker.pick_dbc(win, "Add DBC to merge")
        if not path:
            return
        if path in loaded_files:
            QMessageBox.information(win, "DBC Merge", "Already in the list")
            return
        try:
            db = dbcparse.parse_file(path)
        except OSError as e:
            QMessageBox.warning(win, "DBC Merge", str(e))
            return
        if not db.messages:
            QMessageBox.warning(win, "DBC Merge", "No messages: %s" % path)
            return
        loaded_files.append(path)
        merge_dbc(
            merged, db, os.path.basename(path),
            strategy.currentIndex(), conflicts)
        _refresh()
        _persist()
        plugin_shell.set_status(
            win, "Added %s (%d messages)" % (os.path.basename(path), len(db.messages)),
            4000)

    def _on_save():
        if not merged.messages:
            QMessageBox.information(win, "DBC Merge", "Nothing to save")
            return
        path, _ = QFileDialog.getSaveFileName(
            win, "Save merged DBC", "merged.dbc", "DBC (*.dbc)")
        if not path:
            return
        try:
            text = dbcparse.serialize(merged)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            plugin_shell.set_status(
                win, "Saved %d messages to %s" % (len(merged.messages), path), 5000)
        except OSError as e:
            QMessageBox.warning(win, "DBC Merge", str(e))

    def _on_export_conf():
        if not conflicts:
            QMessageBox.information(win, "DBC Merge", "No conflicts to export")
            return
        path = plugin_shell.export_csv(
            win,
            ["id", "kept", "other", "source", "action"],
            [
                ["0x%X" % cid, kept, other, src, action]
                for cid, kept, other, src, action in conflicts
            ],
            "merge_conflicts.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported %s" % path, 4000)

    def _on_clear():
        loaded_files.clear()
        _reset_merge()
        _refresh()
        _persist()
        plugin_shell.set_status(win, "Cleared", 2000)

    def _on_policy_changed(_i: int):
        if not loaded_files:
            _persist()
            return
        _rebuild_from_files()
        _refresh()
        _persist()
        plugin_shell.set_status(win, "Re-merged with new policy", 3000)

    add_btn.clicked.connect(_on_add)
    save_btn.clicked.connect(_on_save)
    export_conf_btn.clicked.connect(_on_export_conf)
    clear_btn.clicked.connect(_on_clear)
    strategy.currentIndexChanged.connect(_on_policy_changed)
    plugin_shell.bind_shortcut(win, "Ctrl+O", _on_add)
    plugin_shell.bind_shortcut(win, "Ctrl+S", _on_save)

    context.register_command(
        "dbcMerge.open", plugin_shell.bind_raise(win), "Database: DBC Merge")

    saved = state_store.load_state(PLUGIN_ID, default={}) or {}
    if isinstance(saved.get("policy_index"), int):
        strategy.blockSignals(True)
        strategy.setCurrentIndex(max(0, min(2, saved["policy_index"])))
        strategy.blockSignals(False)
    inputs = saved.get("inputs") or []
    if isinstance(inputs, list):
        for p in inputs:
            if not p or not os.path.isfile(p) or p in loaded_files:
                continue
            try:
                db = dbcparse.parse_file(p)
            except OSError:
                continue
            if not db.messages:
                continue
            loaded_files.append(p)
            merge_dbc(
                merged, db, os.path.basename(p),
                strategy.currentIndex(), conflicts)
        if loaded_files:
            _refresh()

    win.show()
    sin.output.append("dbc-merge loaded (multi-DBC + conflict policy + CSV)")


def deactivate():
    global _win
    _win = None
    try:
        import sin
        sin.output.append("dbc-merge deactivated")
    except Exception:
        pass
