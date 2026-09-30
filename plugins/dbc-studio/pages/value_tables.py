# -*- coding: utf-8 -*-
"""Value Tables workspace — VAL_TABLE_ manager (CANdb++ style).

Linked to Messages focus: prefers the value table of the selected signal.
Context menus jump to Messages or assign the focused signal.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QInputDialog,
    QLabel,
    QLineEdit,
    QMenu,
    QMessageBox,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pages import _ui
from _shared import plugin_shell


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

    add_btn = _ui.primary_btn("Add", "Add a value table", "add")
    rename_btn = _ui.ghost_btn("Rename", "Rename selected table", "edit")
    del_btn = _ui.ghost_btn("Delete", "Delete selected table", "delete")
    goto_msg = _ui.ghost_btn(
        "Messages", "Open Messages on the first signal using this table",
        "edit")
    assign_focus = _ui.ghost_btn(
        "Assign focus",
        "Bind this table to the signal selected on Messages",
        "apply")
    search = QLineEdit()
    search.setPlaceholderText("Search tables…")
    search.setClearButtonEnabled(True)
    search.setFixedHeight(_ui.CTRL_H)
    search.setMinimumWidth(160)
    search.setToolTip("Filter by table name or usage (Msg.Sig)")
    entry_search = QLineEdit()
    entry_search.setPlaceholderText("Search entries…")
    entry_search.setClearButtonEnabled(True)
    entry_search.setFixedHeight(_ui.CTRL_H)
    entry_search.setMinimumWidth(120)
    entry_search.setToolTip("Filter value/text rows of the selected table")
    layout.addWidget(_ui.tool_strip(
        search, entry_search, add_btn, rename_btn, del_btn,
        goto_msg, assign_focus, stretch_at=2))

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    bl.setContentsMargins(16, 12, 16, 12)
    bl.setSpacing(10)

    split = QSplitter(Qt.Orientation.Horizontal)

    left = QWidget()
    ll = QVBoxLayout(left)
    ll.setContentsMargins(0, 0, 0, 0)
    ll.setSpacing(6)
    left_head = QLabel("Tables")
    left_head.setObjectName("SuiteSectionTitle")
    ll.addWidget(left_head)
    tables = QTableWidget(0, 3)
    tables.setObjectName("SuiteMatrix")
    tables.setHorizontalHeaderLabels(["Name", "Entries", "Used by"])
    tables.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    tables.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    tables.setAlternatingRowColors(True)
    tables.verticalHeader().setVisible(False)
    tables.verticalHeader().setDefaultSectionSize(28)
    tables.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    tables.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
    tables.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    _ui.style_table(tables)
    ll.addWidget(tables, 1)
    tbl_status = QLabel("")
    tbl_status.setObjectName("SuiteCount")
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
    entries.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    _ui.style_table(entries)
    rl.addWidget(entries, 1)

    eb = QHBoxLayout()
    eb.setSpacing(8)
    add_row = _ui.ghost_btn("Add row", "Append a value/text row", "add")
    del_row = _ui.ghost_btn("Remove row", "Remove selected entry rows", "delete")
    save_entries = _ui.primary_btn(
        "Save entries", "Write entries into the selected table", "save")
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
    assign_msg.setFixedHeight(_ui.CTRL_H)
    assign_msg.setMinimumWidth(160)
    assign_sig = QComboBox()
    assign_sig.setFixedHeight(_ui.CTRL_H)
    assign_sig.setMinimumWidth(120)
    assign_btn = _ui.primary_btn(
        "Assign", "Bind this value table to the signal", "apply")
    assign_row.addWidget(assign_msg, 1)
    assign_row.addWidget(assign_sig, 1)
    assign_row.addWidget(assign_btn)
    rl.addLayout(assign_row)

    entry_status = QLabel("")
    entry_status.setObjectName("SuiteCount")
    rl.addWidget(entry_status)
    split.addWidget(right)
    split.setSizes([360, 560])
    bl.addWidget(split, 1)
    layout.addWidget(body, 1)

    state = {"name": None, "usages": {}}

    def _usages_map() -> dict:
        usages = {}
        for cid, m in document.db.messages.items():
            for s in m.signals:
                vt = (getattr(s, "value_table_name", "") or "").strip()
                if not vt:
                    continue
                usages.setdefault(vt, []).append(
                    (cid, s.name, "%s.%s" % (m.name, s.name)))
        return usages

    def _refresh_tables():
        document.db.sync_value_tables_from_signals()
        usages = _usages_map()
        state["usages"] = usages
        q = (search.text() or "").strip().lower()
        keep = state.get("name")
        tables.setRowCount(0)
        shown = 0
        for name, table in sorted(document.db.value_tables.items()):
            used = usages.get(name) or []
            tip = ", ".join(u[2] for u in used[:8])
            if len(used) > 8:
                tip += " …"
            if q and q not in name.lower() and q not in tip.lower():
                continue
            r = tables.rowCount()
            tables.insertRow(r)
            tables.setItem(r, 0, QTableWidgetItem(name))
            tables.setItem(r, 1, QTableWidgetItem("%d" % len(table)))
            item = QTableWidgetItem(
                ("%d · %s" % (len(used), tip)) if used else "—")
            item.setToolTip(tip or "Not assigned to any signal yet")
            tables.setItem(r, 2, item)
            shown += 1
        tbl_status.setText(
            "%d shown · %d tables · %d assigned" % (
                shown,
                len(document.db.value_tables),
                sum(1 for u in usages.values() if u)))
        _fill_assigners()
        if keep:
            for r in range(tables.rowCount()):
                if tables.item(r, 0) and tables.item(r, 0).text() == keep:
                    tables.blockSignals(True)
                    tables.selectRow(r)
                    tables.blockSignals(False)
                    _load_entries(keep)
                    return
        _select_focused_table()

    def _select_focused_table():
        """Prefer the value table of the signal selected on Messages."""
        cid = getattr(document, "focus_can_id", None)
        sig_name = getattr(document, "focus_signal", "") or ""
        want = ""
        if cid is not None and sig_name:
            m = document.db.messages.get(cid)
            s = m.signal(sig_name) if m else None
            if s:
                want = (getattr(s, "value_table_name", "") or "").strip()
        if not want:
            return
        for r in range(tables.rowCount()):
            if tables.item(r, 0) and tables.item(r, 0).text() == want:
                tables.blockSignals(True)
                tables.selectRow(r)
                tables.blockSignals(False)
                _load_entries(want)
                return

    def select_table(name: str):
        if not name:
            return
        search.blockSignals(True)
        search.clear()
        search.blockSignals(False)
        _refresh_tables()
        for r in range(tables.rowCount()):
            if tables.item(r, 0) and tables.item(r, 0).text() == name:
                tables.selectRow(r)
                _load_entries(name)
                return
        plugin_shell.set_status(
            shell, "Value table not found: %s" % name, 2500)

    def _load_entries(name):
        state["name"] = name
        entries.setRowCount(0)
        table = document.db.value_tables.get(name) or {}
        q = (entry_search.text() or "").strip().lower()
        shown = 0
        for val, text in sorted(table.items()):
            if q and q not in str(val).lower() and q not in str(text).lower():
                continue
            r = entries.rowCount()
            entries.insertRow(r)
            entries.setItem(r, 0, QTableWidgetItem(str(val)))
            entries.setItem(r, 1, QTableWidgetItem(text))
            shown += 1
        used = state["usages"].get(name) or []
        entry_status.setText(
            "Editing %s · %d shown / %d entries · %d signal(s)" % (
                name, shown, len(table), len(used)))

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
        select_table(name)
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
        select_table(name)

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
        # Saving must see every row — drop entry filter first.
        if (entry_search.text() or "").strip():
            entry_search.blockSignals(True)
            entry_search.clear()
            entry_search.blockSignals(False)
            _load_entries(name)
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
        for m in document.db.messages.values():
            for s in m.signals:
                if getattr(s, "value_table_name", "") == name:
                    s.value_table = dict(table)
        document.mark_dirty(True)
        log_fn("ValueTables", "Saved entries for %s (%d)" % (name, len(table)))
        _refresh_tables()
        select_table(name)
        entry_status.setText(
            "Saved %s · %d entries" % (name, len(table)))
        plugin_shell.set_status(
            shell, "Saved value table %s" % name, 2500)

    def _fill_assigners():
        assign_msg.blockSignals(True)
        cur = assign_msg.currentData()
        focus_cid = getattr(document, "focus_can_id", None)
        assign_msg.clear()
        for cid, m in document.db.messages.items():
            assign_msg.addItem("%s (0x%X)" % (m.name, cid), cid)
        prefer = focus_cid if focus_cid is not None else cur
        if prefer is not None:
            idx = assign_msg.findData(prefer)
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
        focus_sig = getattr(document, "focus_signal", "") or ""
        pick = -1
        for i, s in enumerate(m.signals):
            label = s.name
            vt = getattr(s, "value_table_name", "") or ""
            if vt:
                label = "%s  [%s]" % (s.name, vt)
            assign_sig.addItem(label, s.name)
            if s.name == focus_sig:
                pick = i
        if pick >= 0:
            assign_sig.setCurrentIndex(pick)

    def _bind_table(name, cid, sig_name) -> bool:
        m = document.db.messages.get(cid)
        s = m.signal(sig_name) if m else None
        if not m or not s:
            return False
        table = document.db.value_tables.get(name) or {}
        s.value_table = dict(table)
        s.value_table_name = name
        document.mark_dirty(True)
        log_fn("ValueTables", "Assigned %s → %s / %s" % (name, m.name, sig_name))
        return True

    def _on_assign():
        name = state.get("name")
        cid = assign_msg.currentData()
        sig_name = assign_sig.currentData() or assign_sig.currentText()
        if isinstance(sig_name, str) and "  [" in sig_name:
            sig_name = sig_name.split("  [", 1)[0]
        if not name or cid is None or not sig_name:
            return
        if not _bind_table(name, cid, sig_name):
            return
        _fill_sigs()
        _refresh_tables()
        plugin_shell.set_status(
            shell, "Assigned value table %s" % name, 3000)

    def _on_assign_focus():
        name = state.get("name")
        cid = getattr(document, "focus_can_id", None)
        sig_name = getattr(document, "focus_signal", "") or ""
        if not name or cid is None or not sig_name:
            plugin_shell.set_status(
                shell, "Select a signal on Messages first", 2500)
            return
        if not _bind_table(name, cid, sig_name):
            return
        _refresh_tables()
        plugin_shell.set_status(
            shell, "Assigned %s to focused signal" % name, 3000)

    def _first_usage(name: str):
        used = state["usages"].get(name) or []
        return used[0] if used else None

    def _goto_messages():
        name = state.get("name")
        if not name:
            row = tables.currentRow()
            if row >= 0:
                name = tables.item(row, 0).text()
        usage = _first_usage(name) if name else None
        if usage is None:
            cid = getattr(document, "focus_can_id", None)
            sig = getattr(document, "focus_signal", "") or ""
            if cid is not None and hasattr(shell, "goto_editor_target"):
                shell.goto_editor_target(cid, sig or None)
                return
            plugin_shell.set_status(
                shell, "No signal uses this table yet", 2500)
            return
        cid, sig_name, _label = usage
        if hasattr(shell, "goto_editor_target"):
            shell.goto_editor_target(cid, sig_name)
        else:
            document.set_focus(cid, sig_name)
            shell.goto_page("editor")

    def _copy_text(text: str):
        if not text:
            return
        QApplication.clipboard().setText(text)
        plugin_shell.set_status(shell, "Copied", 1500)

    def _tables_menu(pos):
        row = tables.indexAt(pos).row()
        if row < 0:
            return
        name = tables.item(row, 0).text()
        tables.selectRow(row)
        _load_entries(name)
        menu = QMenu(tables)
        menu.addAction("Copy name", lambda: _copy_text(name))
        menu.addAction("Open Messages…", _goto_messages)
        menu.addAction("Assign to focused signal", _on_assign_focus)
        if hasattr(shell, "goto_attributes"):
            usage = _first_usage(name)
            if usage:
                cid, sig_name, _ = usage
                menu.addAction(
                    "Open Attributes…",
                    lambda: shell.goto_attributes(cid, sig_name))
        menu.addSeparator()
        menu.addAction("Rename…", _on_rename)
        menu.addAction("Delete", _on_delete)
        menu.exec(tables.viewport().mapToGlobal(pos))

    def _entries_menu(pos):
        row = entries.indexAt(pos).row()
        menu = QMenu(entries)
        if row >= 0:
            v = entries.item(row, 0).text() if entries.item(row, 0) else ""
            t = entries.item(row, 1).text() if entries.item(row, 1) else ""
            menu.addAction(
                "Copy pair",
                lambda: _copy_text("%s=%s" % (v, t)))
            menu.addSeparator()
        menu.addAction("Add row", _on_add_row)
        if row >= 0:
            menu.addAction("Remove row", _on_del_row)
        menu.addAction("Save entries", _on_save_entries)
        menu.exec(entries.viewport().mapToGlobal(pos))

    add_btn.clicked.connect(_on_add)
    rename_btn.clicked.connect(_on_rename)
    del_btn.clicked.connect(_on_delete)
    goto_msg.clicked.connect(_goto_messages)
    assign_focus.clicked.connect(_on_assign_focus)
    add_row.clicked.connect(_on_add_row)
    del_row.clicked.connect(_on_del_row)
    save_entries.clicked.connect(_on_save_entries)
    assign_btn.clicked.connect(_on_assign)
    tables.itemSelectionChanged.connect(_on_select)
    tables.customContextMenuRequested.connect(_tables_menu)
    entries.customContextMenuRequested.connect(_entries_menu)
    search.textChanged.connect(lambda _t: _refresh_tables())
    entry_search.textChanged.connect(
        lambda _t: _load_entries(state["name"]) if state.get("name") else None)
    assign_msg.currentIndexChanged.connect(lambda _i: _fill_sigs())
    document.on_changed(_refresh_tables)
    document.on_focus(lambda: (_fill_assigners(), _select_focused_table()))

    _refresh_tables()
    root.refresh = _refresh_tables
    root.select_table = select_table
    return root
