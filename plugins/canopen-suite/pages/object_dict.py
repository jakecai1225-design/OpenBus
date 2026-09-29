# -*- coding: utf-8 -*-
"""Object Dictionary — live OD tree + SDO with EDS vs Live compare."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QBrush, QColor
from PyQt6.QtWidgets import (
    QFormLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QSplitter,
    QStackedWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from pages import _ui, interop

# CiA data-type → SDO transfer size in bytes (capped 1–4 for expedited)
_DTYPE_BYTES = {
    0x0001: 1, 0x0002: 1, 0x0003: 2, 0x0004: 4,
    0x0005: 1, 0x0006: 2, 0x0007: 4, 0x0008: 4,
    0x0010: 3, 0x0016: 3,
}


def _dtype_bytes(data_type: str) -> int:
    try:
        code = int((data_type or "").strip() or "0", 0) & 0xFFFF
    except ValueError:
        return 4
    return max(1, min(4, int(_DTYPE_BYTES.get(code, 4))))


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    outer = QVBoxLayout(root)
    outer.setContentsMargins(0, 0, 0, 0)
    outer.setSpacing(0)

    status = QLabel("")
    status.setObjectName("SuiteCount")
    read_btn = _ui.primary_btn("Read", "Expedited SDO upload", "arrow-right")
    read_btn.setMaximumWidth(88)
    write_btn = _ui.ghost_btn("Write", "Confirm before writing to the node", "edit")
    read_all = _ui.icon_tool(
        "sync", "SDO upload every draft/OD entry (EDS vs Live)")

    work = QWidget()
    body_lay = QVBoxLayout(work)
    body_lay.setContentsMargins(0, 0, 0, 0)
    body_lay.setSpacing(0)

    split = QSplitter(Qt.Orientation.Horizontal)

    left = QWidget()
    left_lay = QVBoxLayout(left)
    left_lay.setContentsMargins(0, 0, 0, 0)
    left_lay.setSpacing(0)
    left_lay.addWidget(_ui.panel_header("Live objects", status))

    draft_apply_btn = _ui.primary_btn(
        "Apply draft", "Copy EDS draft into Live OD", "apply")
    draft_map_btn = _ui.ghost_btn(
        "Map PDOs", "Open EDS PDO map", "flow")
    draft_next = _ui.next_step_bar(
        "Draft only — Apply to talk to the bus:",
        draft_apply_btn, draft_map_btn)
    draft_next.setVisible(False)
    draft_next.setToolTip(
        "Draft preview — Apply draft to enable Live OD on the bus.")
    left_lay.addWidget(draft_next)

    tree = interop.OdObjectTree()
    tree.setHeaderLabels(["Index", "Name", "Access", "EDS", "Live", "Match"])
    tree.setAlternatingRowColors(True)
    tree.setMinimumWidth(_ui.PANE_MIN)
    _ui.style_tree(tree, header_hidden=False)
    tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    _ui.configure_columns(
        tree, stretch=1,
        mins={0: 80, 1: 120, 2: 56, 3: 64, 4: 64, 5: 56})
    left_lay.addWidget(tree, 1)
    split.addWidget(left)

    right = QWidget()
    right.setObjectName("SuitePropPanel")
    right.setMinimumWidth(_ui.PANE_MIN_PROP)
    rv = QVBoxLayout(right)
    rv.setContentsMargins(0, 0, 0, 0)
    rv.setSpacing(0)
    rv.addWidget(_ui.panel_header("SDO", read_all, write_btn, read_btn))
    form = QFormLayout()
    form.setSpacing(8)
    form.setContentsMargins(_ui.PAD_X, 8, _ui.PAD_X, _ui.PAD_X)
    form.setFieldGrowthPolicy(
        QFormLayout.FieldGrowthPolicy.FieldsStayAtSizeHint)
    form.setLabelAlignment(
        Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
    idx_spin = _ui.suite_spin(
        0x1018, minimum=0x1000, maximum=0xFFFF, hex_mode=True,
        tip="Object index", width=140)
    sub_spin = _ui.suite_spin(
        1, minimum=0, maximum=255, tip="Sub-index", width=100)
    val_edit = QLineEdit("0")
    val_edit.setFixedHeight(_ui.CTRL_H)
    val_edit.setMinimumWidth(160)
    val_edit.setToolTip(
        "Write value (decimal or 0x hex). "
        "Select a row — Value and Size fill automatically.")
    size_spin = _ui.suite_spin(
        4, minimum=1, maximum=4, tip="SDO transfer size (bytes)", width=100)
    result_lbl = QLabel("-")
    result_lbl.setTextInteractionFlags(
        Qt.TextInteractionFlag.TextSelectableByMouse)
    form.addRow(_ui.field_label("Index"), idx_spin)
    form.addRow(_ui.field_label("Sub-index"), sub_spin)
    form.addRow(_ui.field_label("Value"), val_edit)
    form.addRow(_ui.field_label("Size"), size_spin)
    form.addRow(_ui.field_label("Result"), result_lbl)
    rv.addLayout(form)
    rv.addStretch(1)
    split.addWidget(right)
    _ui.configure_splitter(split, golden=True, master_left=True)
    body_lay.addWidget(split, 1)

    empty_open = _ui.primary_btn(
        "Open EDS…", "Load an EDS / DCF into the draft", "folder")
    empty_apply = _ui.ghost_btn(
        "Apply draft → OD", "Copy the EDS draft into Live OD", "apply")
    empty_scan = _ui.ghost_btn(
        "Scan bus", "Discover Node-IDs on the network", "search")
    empty = _ui.empty_state(
        "No Object Dictionary yet",
        "Open or create an EDS, insert Profiles, then Apply to Live OD.",
        actions=[empty_open, empty_apply, empty_scan])

    stack = QStackedWidget()
    stack.addWidget(empty)
    stack.addWidget(work)
    outer.addWidget(stack, 1)

    state = {"queue": [], "busy": False}

    def _entries() -> list:
        if session.od_entries:
            return session.od_entries
        if session.draft_entries:
            return session.draft_entries
        return []

    def _has_od() -> bool:
        return bool(session.od_entries or session.draft_entries)

    def _find_entry(idx: int, sub: int):
        for e in _entries():
            if e.index == idx and e.subindex == sub:
                return e
        return None

    def _norm(v: str) -> str:
        t = (v or "").strip().lower()
        if not t:
            return ""
        try:
            return "0x%x" % int(t, 0)
        except ValueError:
            return t

    def _match_label(eds: str, live: str) -> str:
        if not live:
            return "-"
        if not eds:
            return "?"
        return "OK" if _norm(eds) == _norm(live) else "DIFF"

    def _paint_match(item: QTreeWidgetItem, label: str):
        brush = QBrush()
        if label == "DIFF":
            brush = QBrush(QColor("#FFCDD2"))
        elif label == "OK":
            brush = QBrush(QColor("#C8E6C9"))
        for col in range(6):
            item.setBackground(col, brush)

    def refresh_tree():
        if not _has_od():
            stack.setCurrentWidget(empty)
            return
        stack.setCurrentWidget(work)
        tree.clear()
        parents = {}
        live_map = getattr(session, "live_values", {}) or {}
        diffs = 0
        for e in _entries():
            eds_val = e.effective_value() or e.default_value or ""
            live = live_map.get((e.index, e.subindex), "")
            match = _match_label(eds_val, live)
            if match == "DIFF":
                diffs += 1
            cols = [
                e.display_index(), e.name or "", e.access_type,
                eds_val, live, match,
            ]
            if e.subindex == 0:
                item = QTreeWidgetItem(cols)
                item.setData(0, Qt.ItemDataRole.UserRole, (e.index, e.subindex))
                _paint_match(item, match)
                tree.addTopLevelItem(item)
                parents[e.index] = item
            else:
                parent_item = parents.get(e.index)
                if parent_item is None:
                    parent_item = QTreeWidgetItem([
                        "0x%04X" % e.index, "", "", "", "", "",
                    ])
                    parent_item.setData(
                        0, Qt.ItemDataRole.UserRole, (e.index, 0))
                    tree.addTopLevelItem(parent_item)
                    parents[e.index] = parent_item
                child = QTreeWidgetItem(cols)
                child.setData(0, Qt.ItemDataRole.UserRole, (e.index, e.subindex))
                _paint_match(child, match)
                parent_item.addChild(child)
        session.debug_mismatch_count = diffs
        if session.od_entries:
            src = "Live OD"
            draft_next.setVisible(False)
        else:
            src = "Draft"
            draft_next.setVisible(True)
        status.setText(
            "Node %d · %s · %s · mismatches %d"
            % (session.node_id, src,
               (session.eds_path and session.eds_path.split("/")[-1].split("\\")[-1])
               or "(untitled)",
               diffs))
        status.setToolTip(
            "" if session.od_entries else
            "Draft preview — Apply draft to enable Live OD on the bus.")
        _ui.fit_columns(tree, stretch=1)

    def on_select():
        item = tree.currentItem()
        if not item:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        idx, sub = data
        if hasattr(session, "set_focus"):
            session.set_focus(idx, sub)
        idx_spin.setValue(idx)
        sub_spin.setValue(sub)
        entry = _find_entry(idx, sub)
        live_map = getattr(session, "live_values", {}) or {}
        live = live_map.get((idx, sub), "")
        eds_val = ""
        if entry is not None:
            eds_val = (
                entry.effective_value() or entry.default_value
                or entry.parameter_value or "")
            size_spin.setValue(_dtype_bytes(entry.data_type))
        # Prefer live value for Write, else EDS default
        pref = live or eds_val or "0"
        val_edit.setText(pref)
        sdo_hint.setText(
            "%s — Read from the bus, or Write the value below."
            % (entry.name if entry and entry.name else "0x%04X:%02X" % (idx, sub)))
        result_lbl.setText("-")

    def focus_object(index: int, subindex: int = 0):
        if hasattr(session, "set_focus"):
            session.set_focus(index, subindex)
        idx_spin.setValue(index)
        sub_spin.setValue(subindex)

        def walk(item):
            data = item.data(0, Qt.ItemDataRole.UserRole)
            if data == (index, subindex):
                tree.setCurrentItem(item)
                return True
            for i in range(item.childCount()):
                if walk(item.child(i)):
                    return True
            return False

        for i in range(tree.topLevelItemCount()):
            if walk(tree.topLevelItem(i)):
                break

    def _apply_session_focus(_=None):
        fi = getattr(session, "focus_index", 0) or 0
        if fi:
            focus_object(fi, getattr(session, "focus_subindex", 0) or 0)

    def _parse_value(text: str) -> int:
        t = (text or "").strip()
        if t.lower().startswith("0x"):
            return int(t, 16)
        return int(t, 0)

    def on_read():
        idx = idx_spin.value()
        sub = sub_spin.value()

        def done(ok, value, note):
            if ok:
                disp = "0x%X" % (value if value is not None else 0)
                result_lbl.setText("%s (%s)" % (disp, note))
                val_edit.setText(disp)
                if hasattr(session, "set_live_value"):
                    session.set_live_value(idx, sub, disp)
                else:
                    refresh_tree()
                plugin_shell.set_status(parent, "SDO read OK", 2500)
            else:
                result_lbl.setText("FAIL: %s" % note)
                plugin_shell.set_status(parent, "SDO read failed", 3000)

        if not session.sdo_upload(idx, sub, on_done=done):
            result_lbl.setText("Busy")

    def on_write():
        idx = idx_spin.value()
        sub = sub_spin.value()
        try:
            value = _parse_value(val_edit.text())
        except ValueError:
            QMessageBox.warning(
                parent, "Invalid value", "Enter a decimal or 0x hex value.")
            return
        size = size_spin.value()
        reply = QMessageBox.question(
            parent,
            "Confirm SDO write",
            "Write 0x%X to node %d object 0x%04X:%02X (%d bytes)?"
            % (value, session.node_id, idx, sub, size),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        def done(ok, _value, note):
            if ok:
                result_lbl.setText("Write OK")
                if hasattr(session, "set_live_value"):
                    session.set_live_value(idx, sub, "0x%X" % value)
                plugin_shell.set_status(parent, "SDO write OK", 2500)
            else:
                result_lbl.setText("FAIL: %s" % note)
                plugin_shell.set_status(parent, "SDO write failed", 3000)

        if not session.sdo_download(idx, sub, value, size, on_done=done):
            result_lbl.setText("Busy")

    def _pump_queue():
        if state["busy"]:
            return
        if not state["queue"]:
            if hasattr(session, "_recompute_mismatches"):
                session._recompute_mismatches()
            refresh_tree()
            plugin_shell.set_status(parent, "EDS read finished", 3000)
            return
        idx, sub = state["queue"].pop(0)
        state["busy"] = True

        def done(ok, value, note):
            state["busy"] = False
            if ok and hasattr(session, "set_live_value"):
                session.set_live_value(
                    idx, sub, "0x%X" % (value if value is not None else 0),
                    notify=False)
            _pump_queue()

        if not session.sdo_upload(idx, sub, on_done=done):
            state["busy"] = False
            _pump_queue()

    def on_read_all():
        entries = [
            e for e in _entries()
            if e.data_type or e.subindex > 0
        ]
        if not entries:
            QMessageBox.information(parent, "Read EDS", "No OD entries to read.")
            return
        state["queue"] = [(e.index, e.subindex) for e in entries]
        log_fn("SYS", "-", b"", "SDO read-all %d objects" % len(state["queue"]))
        plugin_shell.set_status(
            parent, "Reading %d objects…" % len(state["queue"]), 0)
        _pump_queue()

    tree.itemSelectionChanged.connect(on_select)
    read_btn.clicked.connect(on_read)
    write_btn.clicked.connect(on_write)
    read_all.clicked.connect(on_read_all)
    empty_open.clicked.connect(
        lambda: interop.shell_action(parent, "eds.open"))
    empty_apply.clicked.connect(
        lambda: interop.shell_action(parent, "eds.apply_od"))
    draft_apply_btn.clicked.connect(
        lambda: interop.shell_action(parent, "eds.apply_od"))
    draft_map_btn.clicked.connect(
        lambda: interop.shell_action(parent, "view.pdo_map"))
    empty_scan.clicked.connect(
        lambda: interop.shell_action(parent, "network.scan"))

    def _od_menu(pos):
        from PyQt6.QtWidgets import QMenu
        menu = QMenu(tree)
        item = tree.itemAt(pos)
        if item is None:
            interop.add_bridge_actions(menu, parent, include_blank=True)
        else:
            data = item.data(0, Qt.ItemDataRole.UserRole)
            menu.addAction("Read", on_read)
            menu.addAction("Write…", on_write)
            if isinstance(data, tuple) and len(data) >= 2:
                interop.add_bridge_actions(
                    menu, parent, index=int(data[0]), subindex=int(data[1]))
            menu.addSeparator()
            menu.addAction(
                "Apply EDS → OD",
                lambda: interop.shell_action(parent, "eds.apply_od"))
        menu.exec(tree.viewport().mapToGlobal(pos))

    tree.customContextMenuRequested.connect(_od_menu)
    tree.set_profile_drop_handler(
        lambda pid: interop.shell_action(
            parent, "profile.insert", profile_id=pid))
    tree.set_node_drop_handler(
        lambda n: interop.shell_action(parent, "network.use_node", node_id=n))

    def _on_od_changed():
        refresh_tree()
        _apply_session_focus()

    session.on_od_changed(_on_od_changed)
    if hasattr(session, "on_focus_changed"):
        session.on_focus_changed(_apply_session_focus)
    refresh_tree()
    _apply_session_focus()
    root.focus_object = focus_object  # type: ignore[attr-defined]
    root.read_selection = on_read  # type: ignore[attr-defined]
    root.read_all_eds = on_read_all  # type: ignore[attr-defined]
    return root
