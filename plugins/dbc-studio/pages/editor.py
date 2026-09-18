# -*- coding: utf-8 -*-
"""Editor workspace — CANdb++ style tree + property form."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import dbcparse, plugin_shell


def _parse_value_table(text: str) -> dict:
    out = {}
    for part in (text or "").replace(",", ";").split(";"):
        part = part.strip()
        if not part or "=" not in part:
            continue
        k, v = part.split("=", 1)
        try:
            out[int(k.strip())] = v.strip()
        except ValueError:
            continue
    return out


def _fmt_value_table(vt: dict) -> str:
    if not vt:
        return ""
    return ";".join("%d=%s" % (k, v) for k, v in sorted(vt.items()))


def build(shell, document, log_fn) -> QWidget:
    root = QWidget(shell)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(4, 4, 4, 4)

    layout.addWidget(plugin_shell.help_label(
        "Browse Network / Nodes / Messages / Signals. Edit properties and Apply. "
        "Add or Delete messages and signals. Save writes UTF-8 .dbc via serialize."))

    toolbar = QHBoxLayout()
    add_msg_btn = QPushButton("Add Message")
    add_sig_btn = QPushButton("Add Signal")
    del_btn = QPushButton("Delete")
    refresh_btn = QPushButton("Refresh")
    for b in (add_msg_btn, add_sig_btn, del_btn, refresh_btn):
        toolbar.addWidget(b)
    toolbar.addStretch()
    layout.addLayout(toolbar)

    splitter = QSplitter(Qt.Orientation.Horizontal)
    tree = QTreeWidget()
    tree.setHeaderLabels(["Name", "Detail"])
    tree.setAlternatingRowColors(True)
    tree.setMinimumWidth(320)
    splitter.addWidget(tree)

    prop_box = QGroupBox("Properties")
    prop_layout = QVBoxLayout(prop_box)
    form = QFormLayout()
    prop_layout.addLayout(form)

    hint = QLabel("Select a message or signal to edit.")
    hint.setStyleSheet("color:#78909c;")
    prop_layout.addWidget(hint)

    apply_btn = QPushButton("Apply")
    apply_btn.setEnabled(False)
    prop_layout.addWidget(apply_btn)
    prop_layout.addStretch()
    splitter.addWidget(prop_box)
    splitter.setSizes([420, 520])
    layout.addWidget(splitter, 1)

    state = {"kind": None, "can_id": None, "signal": None, "widgets": {}}

    def _clear_form():
        while form.rowCount():
            form.removeRow(0)
        state["widgets"] = {}
        apply_btn.setEnabled(False)
        hint.show()

    def _add_line(key, label, value=""):
        w = QLineEdit(str(value) if value is not None else "")
        form.addRow(label, w)
        state["widgets"][key] = w
        return w

    def _add_spin(key, label, value, lo, hi):
        w = QSpinBox()
        w.setRange(lo, hi)
        w.setValue(int(value))
        form.addRow(label, w)
        state["widgets"][key] = w
        return w

    def _add_check(key, label, value):
        w = QCheckBox()
        w.setChecked(bool(value))
        form.addRow(label, w)
        state["widgets"][key] = w
        return w

    def _add_combo(key, label, items, current):
        w = QComboBox()
        w.addItems(items)
        idx = items.index(current) if current in items else 0
        w.setCurrentIndex(idx)
        form.addRow(label, w)
        state["widgets"][key] = w
        return w

    def _fill_message(msg):
        _clear_form()
        hint.hide()
        state["kind"] = "message"
        state["can_id"] = msg.can_id
        state["signal"] = None
        _add_line("name", "Name", msg.name)
        id_edit = _add_line("can_id", "CAN ID (hex)", "0x%X" % msg.can_id)
        id_edit.setPlaceholderText("0x100")
        _add_check("extended", "Extended", msg.extended)
        _add_spin("dlc", "DLC", msg.dlc, 0, 64)
        _add_line("sender", "Sender", msg.sender or "")
        _add_spin("cycle_time", "Cycle time (ms)", msg.cycle_time, 0, 65535)
        _add_line("comment", "Comment", msg.comment or "")
        apply_btn.setEnabled(True)

    def _fill_signal(msg, sig):
        _clear_form()
        hint.hide()
        state["kind"] = "signal"
        state["can_id"] = msg.can_id
        state["signal"] = sig.name
        _add_line("name", "Name", sig.name)
        _add_spin("start_bit", "Start bit", sig.start_bit, 0, 512)
        _add_spin("bit_length", "Bit length", sig.bit_length, 1, 64)
        _add_combo(
            "endian", "Endian", ["Intel (little)", "Motorola (big)"],
            "Intel (little)" if sig.little_endian else "Motorola (big)")
        _add_check("signed", "Signed", sig.is_signed)
        _add_line("factor", "Factor", sig.factor)
        _add_line("offset", "Offset", sig.offset)
        _add_line("minimum", "Min", sig.minimum)
        _add_line("maximum", "Max", sig.maximum)
        _add_line("unit", "Unit", sig.unit or "")
        _add_line("receivers", "Receivers (comma)", ",".join(sig.receivers))
        _add_line("comment", "Comment", sig.comment or "")
        _add_line("value_table", "Value table", _fmt_value_table(sig.value_table))
        state["widgets"]["value_table"].setPlaceholderText("0=Off;1=On")
        apply_btn.setEnabled(True)

    def _rebuild_tree(select_can_id=None, select_signal=None):
        tree.blockSignals(True)
        tree.clear()
        db = document.db

        net = QTreeWidgetItem(["Network", document.display_name()])
        net.setData(0, Qt.ItemDataRole.UserRole, ("network", None, None))
        tree.addTopLevelItem(net)

        nodes_item = QTreeWidgetItem(["Nodes", "%d" % len(db.nodes)])
        nodes_item.setData(0, Qt.ItemDataRole.UserRole, ("nodes", None, None))
        net.addChild(nodes_item)
        for n in db.nodes:
            child = QTreeWidgetItem([n, ""])
            child.setData(0, Qt.ItemDataRole.UserRole, ("node", None, n))
            nodes_item.addChild(child)

        msgs_item = QTreeWidgetItem(["Messages", "%d" % len(db.messages)])
        msgs_item.setData(0, Qt.ItemDataRole.UserRole, ("messages", None, None))
        net.addChild(msgs_item)

        select_item = None
        for cid in sorted(db.messages.keys()):
            m = db.messages[cid]
            detail = "0x%X  DLC %d  %s" % (cid, m.dlc, m.sender or "")
            mi = QTreeWidgetItem([m.name, detail])
            mi.setData(0, Qt.ItemDataRole.UserRole, ("message", cid, None))
            msgs_item.addChild(mi)
            if select_can_id == cid and not select_signal:
                select_item = mi
            for s in m.signals:
                sdetail = "%d|%d@%s%s" % (
                    s.start_bit, s.bit_length,
                    "1" if s.little_endian else "0",
                    "-" if s.is_signed else "+")
                si = QTreeWidgetItem([s.name, sdetail])
                si.setData(0, Qt.ItemDataRole.UserRole, ("signal", cid, s.name))
                mi.addChild(si)
                if select_can_id == cid and select_signal == s.name:
                    select_item = si

        net.setExpanded(True)
        nodes_item.setExpanded(False)
        msgs_item.setExpanded(True)
        tree.blockSignals(False)

        if select_item is not None:
            tree.setCurrentItem(select_item)
            tree.scrollToItem(select_item)
        elif tree.topLevelItemCount():
            tree.setCurrentItem(msgs_item)

    def _on_select():
        item = tree.currentItem()
        if item is None:
            _clear_form()
            return
        kind, cid, name = item.data(0, Qt.ItemDataRole.UserRole) or (None, None, None)
        if kind == "message" and cid is not None:
            msg = document.db.messages.get(cid)
            if msg:
                _fill_message(msg)
            return
        if kind == "signal" and cid is not None and name:
            msg = document.db.messages.get(cid)
            sig = msg.signal(name) if msg else None
            if msg and sig:
                _fill_signal(msg, sig)
            return
        _clear_form()
        if kind == "node":
            hint.setText("Node: %s (rename via message sender / receivers)" % name)
            hint.show()
        elif kind == "nodes":
            hint.setText("Nodes: %s" % ", ".join(document.db.nodes) or "(none)")
            hint.show()

    def _fval(key, default=0.0):
        w = state["widgets"].get(key)
        if w is None:
            return default
        try:
            return float(w.text().strip())
        except ValueError:
            return default

    def _on_apply():
        kind = state["kind"]
        cid = state["can_id"]
        if kind == "message" and cid is not None:
            msg = document.db.messages.get(cid)
            if not msg:
                return
            w = state["widgets"]
            new_name = w["name"].text().strip() or msg.name
            id_text = w["can_id"].text().strip().lower().replace("0x", "")
            try:
                new_id = int(id_text, 16)
            except ValueError:
                QMessageBox.warning(root, "Editor", "Invalid CAN ID")
                return
            new_id &= 0x1FFFFFFF
            if new_id != cid and new_id in document.db.messages:
                QMessageBox.warning(root, "Editor", "CAN ID already exists")
                return
            msg.name = new_name
            msg.extended = w["extended"].isChecked()
            msg.dlc = w["dlc"].value()
            msg.sender = w["sender"].text().strip() or "Vector__XXX"
            msg.cycle_time = w["cycle_time"].value()
            msg.comment = w["comment"].text().strip()
            if new_id != cid:
                del document.db.messages[cid]
                msg.can_id = new_id
                document.db.messages[new_id] = msg
                cid = new_id
            if msg.sender and msg.sender not in document.db.nodes:
                document.db.nodes.append(msg.sender)
            document.mark_dirty(True)
            log_fn("Editor", "Updated message %s (0x%X)" % (msg.name, cid))
            _rebuild_tree(cid, None)
            return

        if kind == "signal" and cid is not None:
            msg = document.db.messages.get(cid)
            old_name = state["signal"]
            sig = msg.signal(old_name) if msg else None
            if not msg or not sig:
                return
            w = state["widgets"]
            new_name = w["name"].text().strip() or sig.name
            if new_name != old_name and msg.signal(new_name):
                QMessageBox.warning(root, "Editor", "Signal name already exists")
                return
            sig.name = new_name
            sig.start_bit = w["start_bit"].value()
            sig.bit_length = w["bit_length"].value()
            sig.little_endian = w["endian"].currentIndex() == 0
            sig.is_signed = w["signed"].isChecked()
            sig.factor = _fval("factor", 1.0)
            sig.offset = _fval("offset", 0.0)
            sig.minimum = _fval("minimum", 0.0)
            sig.maximum = _fval("maximum", 0.0)
            sig.unit = w["unit"].text().strip()
            sig.receivers = [
                r.strip() for r in w["receivers"].text().split(",") if r.strip()]
            sig.comment = w["comment"].text().strip()
            sig.value_table = _parse_value_table(w["value_table"].text())
            for r in sig.receivers:
                if r not in document.db.nodes:
                    document.db.nodes.append(r)
            document.mark_dirty(True)
            log_fn("Editor", "Updated signal %s / %s" % (msg.name, sig.name))
            _rebuild_tree(cid, sig.name)

    def _on_add_message():
        name, ok = QInputDialog.getText(root, "Add Message", "Message name:")
        if not ok or not name.strip():
            return
        name = name.strip()
        id_text, ok = QInputDialog.getText(
            root, "Add Message", "CAN ID (hex):", text="0x100")
        if not ok:
            return
        try:
            cid = int(id_text.strip().lower().replace("0x", ""), 16) & 0x1FFFFFFF
        except ValueError:
            QMessageBox.warning(root, "Editor", "Invalid CAN ID")
            return
        if cid in document.db.messages:
            QMessageBox.warning(root, "Editor", "CAN ID already exists")
            return
        msg = dbcparse.Message()
        msg.can_id = cid
        msg.name = name
        msg.dlc = 8
        msg.sender = "Vector__XXX"
        document.db.messages[cid] = msg
        if "Vector__XXX" not in document.db.nodes:
            document.db.nodes.append("Vector__XXX")
        document.mark_dirty(True)
        log_fn("Editor", "Added message %s (0x%X)" % (name, cid))
        _rebuild_tree(cid, None)

    def _on_add_signal():
        item = tree.currentItem()
        cid = None
        if item is not None:
            kind, cid, _name = item.data(0, Qt.ItemDataRole.UserRole) or (None, None, None)
            if kind == "signal":
                pass
            elif kind != "message":
                cid = None
        if cid is None:
            QMessageBox.information(root, "Editor", "Select a message first")
            return
        msg = document.db.messages.get(cid)
        if not msg:
            return
        name, ok = QInputDialog.getText(root, "Add Signal", "Signal name:")
        if not ok or not name.strip():
            return
        name = name.strip()
        if msg.signal(name):
            QMessageBox.warning(root, "Editor", "Signal already exists")
            return
        sig = dbcparse.Signal()
        sig.name = name
        sig.start_bit = 0
        sig.bit_length = 8
        sig.little_endian = True
        sig.factor = 1.0
        sig.receivers = ["Vector__XXX"]
        msg.signals.append(sig)
        document.mark_dirty(True)
        log_fn("Editor", "Added signal %s under %s" % (name, msg.name))
        _rebuild_tree(cid, name)

    def _on_delete():
        item = tree.currentItem()
        if item is None:
            return
        kind, cid, name = item.data(0, Qt.ItemDataRole.UserRole) or (None, None, None)
        if kind == "message" and cid is not None:
            msg = document.db.messages.get(cid)
            label = msg.name if msg else str(cid)
            ans = QMessageBox.question(
                root, "Delete Message",
                "Delete message %s (0x%X) and all signals?" % (label, cid),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            if ans != QMessageBox.StandardButton.Yes:
                return
            del document.db.messages[cid]
            document.mark_dirty(True)
            log_fn("Editor", "Deleted message 0x%X" % cid)
            _clear_form()
            _rebuild_tree()
        elif kind == "signal" and cid is not None and name:
            msg = document.db.messages.get(cid)
            if not msg:
                return
            ans = QMessageBox.question(
                root, "Delete Signal",
                "Delete signal %s from %s?" % (name, msg.name),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            if ans != QMessageBox.StandardButton.Yes:
                return
            msg.signals = [s for s in msg.signals if s.name != name]
            document.mark_dirty(True)
            log_fn("Editor", "Deleted signal %s" % name)
            _clear_form()
            _rebuild_tree(cid, None)

    def select_target(can_id=None, signal=None):
        if can_id is None:
            _rebuild_tree()
            return
        try:
            can_id = int(can_id)
        except (TypeError, ValueError):
            _rebuild_tree()
            return
        _rebuild_tree(can_id, signal)

    tree.currentItemChanged.connect(lambda _c, _p: _on_select())
    apply_btn.clicked.connect(_on_apply)
    add_msg_btn.clicked.connect(_on_add_message)
    add_sig_btn.clicked.connect(_on_add_signal)
    del_btn.clicked.connect(_on_delete)
    refresh_btn.clicked.connect(lambda: _rebuild_tree(state.get("can_id"), state.get("signal")))

    document.on_changed(lambda: _rebuild_tree(
        state.get("can_id") if state.get("kind") in ("message", "signal") else None,
        state.get("signal") if state.get("kind") == "signal" else None,
    ))

    _rebuild_tree()
    root.select_target = select_target
    return root
