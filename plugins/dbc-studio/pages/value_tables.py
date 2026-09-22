# -*- coding: utf-8 -*-
"""Value Tables workspace — VAL_TABLE_ manager (CANdb++ style)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QInputDialog,
    QLabel,
    QMessageBox,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, plugin_shell


def _parse_pairs(text: str) -> dict:
    out = {}
    for part in (text or "").replace(",", ";").split(";"):
        part = part.strip()
        if not part or "=" not in part:
            continue
        k, v = part.split("=", 1)
        try:
            out[int(k.strip(), 0)] = v.strip()
        except ValueError:
            continue
    return out


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome = QWidget()
    chrome.setObjectName("SuiteToolbar")
    crow = QHBoxLayout(chrome)
    crow.setContentsMargins(12, 6, 12, 6)
    crow.setSpacing(8)
    crow.addStretch(1)

    add_btn = QPushButton("Add")
    add_btn.setFixedHeight(28)
    codicons.set_button(add_btn, "add", primary=True)
    rename_btn = QPushButton("Rename")
    rename_btn.setObjectName("GhostButton")
    rename_btn.setFixedHeight(28)
    codicons.set_button(rename_btn, "edit")
    del_btn = QPushButton("Delete")
    del_btn.setObjectName("GhostButton")
    del_btn.setFixedHeight(28)
    codicons.set_button(del_btn, "delete")
    crow.addWidget(add_btn)
    crow.addWidget(rename_btn)
    crow.addWidget(del_btn)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    bl.setContentsMargins(16, 12, 16, 12)
    bl.setSpacing(10)

    hint = QLabel(
        "Named VAL_TABLE_ library — create once, assign to many signals "
        "(Editor picker or Assign below). Surpasses CANdb++ with live entry "
        "editing and one-click signal jump.")
    hint.setWordWrap(True)
    hint.setStyleSheet("color:#78909c;font-size:12px;")
    bl.addWidget(hint)

    split = QSplitter(Qt.Orientation.Horizontal)

    left = QWidget()
    ll = QVBoxLayout(left)
    ll.setContentsMargins(0, 0, 0, 0)
    ll.setSpacing(6)
    left_head = QLabel("Tables")
    left_head.setObjectName("SuiteSectionTitle")
    ll.addWidget(left_head)
    tables = QTableWidget(0, 2)
    tables.setObjectName("SuiteMatrix")
    tables.setHorizontalHeaderLabels(["Name", "Entries"])
    tables.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tables.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    tables.setAlternatingRowColors(True)
    tables.verticalHeader().setVisible(False)
    tables.verticalHeader().setDefaultSectionSize(28)
    tables.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    tables.setStyleSheet(
        "QTableWidget#SuiteMatrix { gridline-color: #EEEEEE; }"
        "QTableWidget#SuiteMatrix::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
    )
    ll.addWidget(tables, 1)
    tbl_status = QLabel("")
    tbl_status.setStyleSheet("color:#90A4AE;font-size:11px;")
    ll.addWidget(tbl_status)
    split.addWidget(left)

    right = QWidget()
    rl = QVBoxLayout(right)
    rl.setContentsMargins(8, 0, 0, 0)
    rl.setSpacing(6)
    right_head = QLabel("Entries")
    right_head.setObjectName("SuiteSectionTitle")
    rl.addWidget(right_head)

    entries = QTableWidget(0, 2)
    entries.setObjectName("SuiteMatrix")
    entries.setHorizontalHeaderLabels(["Value", "Text"])
    entries.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    entries.setAlternatingRowColors(True)
    entries.verticalHeader().setVisible(False)
    entries.verticalHeader().setDefaultSectionSize(28)
    entries.setStyleSheet(
        "QTableWidget#SuiteMatrix { gridline-color: #EEEEEE; }"
        "QTableWidget#SuiteMatrix::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
    )
    rl.addWidget(entries, 1)

    eb = QHBoxLayout()
    eb.setSpacing(8)
    add_row = QPushButton("Add row")
    add_row.setFixedHeight(28)
    codicons.set_button(add_row, "add")
    del_row = QPushButton("Remove row")
    del_row.setObjectName("GhostButton")
    del_row.setFixedHeight(28)
    codicons.set_button(del_row, "delete")
    save_entries = QPushButton("Save entries")
    save_entries.setFixedHeight(28)
    codicons.set_button(save_entries, "save", primary=True)
    eb.addWidget(add_row)
    eb.addWidget(del_row)
    eb.addStretch(1)
    eb.addWidget(save_entries)
    rl.addLayout(eb)

    assign_head = QLabel("Assign to signal")
    assign_head.setObjectName("SuiteSectionTitle")
    rl.addWidget(assign_head)
    assign_row = QHBoxLayout()
    assign_row.setSpacing(8)
    assign_msg = QComboBox()
    assign_msg.setFixedHeight(28)
    assign_msg.setMinimumWidth(160)
    assign_sig = QComboBox()
    assign_sig.setFixedHeight(28)
    assign_sig.setMinimumWidth(120)
    assign_btn = QPushButton("Assign")
    assign_btn.setFixedHeight(28)
    codicons.set_button(assign_btn, "apply", primary=True)
    assign_row.addWidget(assign_msg, 1)
    assign_row.addWidget(assign_sig, 1)
    assign_row.addWidget(assign_btn)
    rl.addLayout(assign_row)

    entry_status = QLabel("")
    entry_status.setStyleSheet("color:#90A4AE;font-size:11px;")
    rl.addWidget(entry_status)
    split.addWidget(right)
    split.setSizes([360, 560])
    bl.addWidget(split, 1)
    layout.addWidget(body, 1)

    state = {"name": None}

    def _refresh_tables():
        tables.setRowCount(0)
        for name, table in sorted(document.db.value_tables.items()):
            r = tables.rowCount()
            tables.insertRow(r)
            tables.setItem(r, 0, QTableWidgetItem(name))
            tables.setItem(r, 1, QTableWidgetItem("%d" % len(table)))
        tbl_status.setText("%d tables" % tables.rowCount())
        _fill_assigners()

    def _load_entries(name):
        state["name"] = name
        entries.setRowCount(0)
        table = document.db.value_tables.get(name) or {}
        for val, text in sorted(table.items()):
            r = entries.rowCount()
            entries.insertRow(r)
            entries.setItem(r, 0, QTableWidgetItem(str(val)))
            entries.setItem(r, 1, QTableWidgetItem(text))
        entry_status.setText(
            "Editing %s · %d entries" % (name, entries.rowCount()))

    def _on_select():
        row = tables.currentRow()
        if row < 0:
            state["name"] = None
            entries.setRowCount(0)
            entry_status.setText("")
            return
        _load_entries(tables.item(row, 0).text())

    def _on_add():
        name, ok = QInputDialog.getText(shell, "Value table", "Name:")
        if not ok or not name.strip():
            return
        name = name.strip()
        if name in document.db.value_tables:
            QMessageBox.warning(shell, "Value table", "Already exists")
            return
        raw, ok = QInputDialog.getText(
            shell, "Entries", "Optional pairs 0=Off; 1=On:")
        document.db.value_tables[name] = _parse_pairs(raw) if ok else {}
        document.mark_dirty(True)
        log_fn("ValueTables", "Added %s" % name)
        _refresh_tables()
        for r in range(tables.rowCount()):
            if tables.item(r, 0).text() == name:
                tables.selectRow(r)
                break
        plugin_shell.set_status(shell, "Added value table %s" % name, 2500)

    def _on_rename():
        row = tables.currentRow()
        if row < 0:
            return
        old = tables.item(row, 0).text()
        name, ok = QInputDialog.getText(shell, "Rename", "New name:", text=old)
        if not ok or not name.strip() or name.strip() == old:
            return
        name = name.strip()
        if name in document.db.value_tables:
            QMessageBox.warning(shell, "Value table", "Name in use")
            return
        document.db.value_tables[name] = document.db.value_tables.pop(old)
        for m in document.db.messages.values():
            for s in m.signals:
                if getattr(s, "value_table_name", "") == old:
                    s.value_table_name = name
        document.mark_dirty(True)
        state["name"] = name
        _refresh_tables()
        for r in range(tables.rowCount()):
            if tables.item(r, 0).text() == name:
                tables.selectRow(r)
                break

    def _on_delete():
        row = tables.currentRow()
        if row < 0:
            return
        name = tables.item(row, 0).text()
        document.db.value_tables.pop(name, None)
        document.mark_dirty(True)
        log_fn("ValueTables", "Deleted %s" % name)
        state["name"] = None
        _refresh_tables()
        entries.setRowCount(0)
        entry_status.setText("")

    def _on_add_row():
        r = entries.rowCount()
        entries.insertRow(r)
        entries.setItem(r, 0, QTableWidgetItem("0"))
        entries.setItem(r, 1, QTableWidgetItem(""))
        entries.editItem(entries.item(r, 1))

    def _on_del_row():
        r = entries.currentRow()
        if r >= 0:
            entries.removeRow(r)

    def _on_save_entries():
        name = state.get("name")
        if not name:
            return
        table = {}
        for r in range(entries.rowCount()):
            try:
                val = int(
                    (entries.item(r, 0).text() if entries.item(r, 0) else "0"),
                    0)
            except ValueError:
                continue
            text = entries.item(r, 1).text() if entries.item(r, 1) else ""
            table[val] = text
        document.db.value_tables[name] = table
        # Keep assigned signals in sync with library
        for m in document.db.messages.values():
            for s in m.signals:
                if getattr(s, "value_table_name", "") == name:
                    s.value_table = dict(table)
        document.mark_dirty(True)
        log_fn("ValueTables", "Saved entries for %s (%d)" % (name, len(table)))
        _refresh_tables()
        for r in range(tables.rowCount()):
            if tables.item(r, 0).text() == name:
                tables.selectRow(r)
                break
        entry_status.setText(
            "Saved %s · %d entries" % (name, len(table)))
        plugin_shell.set_status(
            shell, "Saved value table %s" % name, 2500)

    def _fill_assigners():
        assign_msg.blockSignals(True)
        cur = assign_msg.currentData()
        assign_msg.clear()
        for cid, m in document.db.messages.items():
            assign_msg.addItem("%s (0x%X)" % (m.name, cid), cid)
        if cur is not None:
            idx = assign_msg.findData(cur)
            if idx >= 0:
                assign_msg.setCurrentIndex(idx)
        assign_msg.blockSignals(False)
        _fill_sigs()

    def _fill_sigs():
        assign_sig.clear()
        cid = assign_msg.currentData()
        m = document.db.messages.get(cid) if cid is not None else None
        if not m:
            return
        for s in m.signals:
            label = s.name
            vt = getattr(s, "value_table_name", "") or ""
            if vt:
                label = "%s  [%s]" % (s.name, vt)
            assign_sig.addItem(label, s.name)

    def _on_assign():
        name = state.get("name")
        cid = assign_msg.currentData()
        sig_name = assign_sig.currentData() or assign_sig.currentText()
        if isinstance(sig_name, str) and "  [" in sig_name:
            sig_name = sig_name.split("  [", 1)[0]
        if not name or cid is None or not sig_name:
            return
        m = document.db.messages.get(cid)
        s = m.signal(sig_name) if m else None
        if not s:
            return
        table = document.db.value_tables.get(name) or {}
        s.value_table = dict(table)
        s.value_table_name = name
        document.mark_dirty(True)
        log_fn("ValueTables", "Assigned %s → %s / %s" % (name, m.name, sig_name))
        _fill_sigs()
        plugin_shell.set_status(
            shell, "Assigned value table %s" % name, 3000)

    add_btn.clicked.connect(_on_add)
    rename_btn.clicked.connect(_on_rename)
    del_btn.clicked.connect(_on_delete)
    add_row.clicked.connect(_on_add_row)
    del_row.clicked.connect(_on_del_row)
    save_entries.clicked.connect(_on_save_entries)
    assign_btn.clicked.connect(_on_assign)
    tables.itemSelectionChanged.connect(_on_select)
    assign_msg.currentIndexChanged.connect(lambda _i: _fill_sigs())
    document.on_changed(_refresh_tables)

    _refresh_tables()
    return root
