# -*- coding: utf-8 -*-
"""CANdb++ style database editor.

Left: object tree (network / nodes / messages / signals) with a filter.
Center: definition of the selection.
Right: layout window (byte rows, bits 7..0). Click a cell to select the signal.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QStackedWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import dbcparse

from .bit_layout import BitLayout, _Draft


def _parse_value_table(text: str) -> dict:
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


def _fmt_value_table(vt: dict) -> str:
    if not vt:
        return ""
    return "; ".join("%d=%s" % (k, v) for k, v in sorted(vt.items()))


def _spin(lo, hi, value, tip):
    box = QSpinBox()
    box.setObjectName("SuiteSpin")
    box.setRange(lo, hi)
    box.setValue(int(value))
    box.setFixedHeight(28)
    box.setToolTip(tip)
    return box


def _line(text, tip):
    edit = QLineEdit(text)
    edit.setFixedHeight(28)
    edit.setToolTip(tip)
    return edit


class _NameIdDialog(QDialog):
    def __init__(self, parent, title, name="", ident="0x100"):
        super().__init__(parent)
        self.setWindowTitle(title)
        form = QFormLayout(self)
        self.name = _line(name, "Object name")
        self.ident = _line(ident, "CAN identifier, hex")
        form.addRow("Name", self.name)
        form.addRow("CAN ID", self.ident)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)


def build(shell, document, log_fn) -> QWidget:
    root = QWidget(shell)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(8, 6, 8, 6)
    layout.setSpacing(6)

    toolbar = QHBoxLayout()
    toolbar.setSpacing(6)
    filt = _line("", "Filter nodes, messages and signals")
    filt.setPlaceholderText("Filter")
    filt.setMaximumWidth(220)
    add_msg = QPushButton("Message")
    add_sig = QPushButton("Signal")
    add_node = QPushButton("Node")
    delete_btn = QPushButton("Delete")
    apply_btn = QPushButton("Apply")
    apply_btn.setObjectName("PrimaryButton")
    for b, tip in (
        (add_msg, "New message"),
        (add_sig, "New signal on the selected message"),
        (add_node, "New node"),
        (delete_btn, "Delete the selection"),
        (apply_btn, "Write the definition into the database"),
    ):
        b.setFixedHeight(28)
        b.setCursor(Qt.CursorShape.PointingHandCursor)
        b.setToolTip(tip)
        if b is not apply_btn:
            b.setObjectName("GhostButton")
    toolbar.addWidget(filt)
    toolbar.addWidget(add_msg)
    toolbar.addWidget(add_sig)
    toolbar.addWidget(add_node)
    toolbar.addWidget(delete_btn)
    toolbar.addStretch(1)
    toolbar.addWidget(apply_btn)
    layout.addLayout(toolbar)

    splitter = QSplitter(Qt.Orientation.Horizontal)
    tree = QTreeWidget()
    tree.setHeaderLabels(["Name", "ID / layout"])
    tree.setAlternatingRowColors(True)
    tree.setMinimumWidth(260)
    splitter.addWidget(tree)

    stack = QStackedWidget()
    empty = QLabel("Select a node, message or signal.")
    empty.setObjectName("SuiteHint")
    empty.setAlignment(Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeft)
    empty.setContentsMargins(12, 12, 12, 12)
    stack.addWidget(empty)

    msg_page = QWidget()
    msg_form = QFormLayout(msg_page)
    msg_form.setSpacing(8)
    msg_form.setContentsMargins(12, 8, 12, 8)
    m_name = _line("", "Message name")
    m_id = _line("", "CAN identifier")
    m_ext = QCheckBox("Extended (29-bit)")
    m_dlc = _spin(0, 64, 8, "Data length")
    m_sender = _line("", "Transmitter node")
    m_cycle = _spin(0, 65535, 0, "GenMsgCycleTime, milliseconds")
    m_comment = _line("", "Message comment")
    msg_form.addRow("Name", m_name)
    msg_form.addRow("CAN ID", m_id)
    msg_form.addRow("", m_ext)
    msg_form.addRow("DLC", m_dlc)
    msg_form.addRow("Transmitter", m_sender)
    msg_form.addRow("Cycle time", m_cycle)
    msg_form.addRow("Comment", m_comment)
    stack.addWidget(msg_page)

    sig_page = QWidget()
    sig_form = QFormLayout(sig_page)
    sig_form.setSpacing(8)
    sig_form.setContentsMargins(12, 8, 12, 8)
    s_name = _line("", "Signal name")
    s_start = _spin(0, 512, 0, "Start bit. Intel: LSB. Motorola: MSB. Bit 0 is the LSB of byte 0.")
    s_len = _spin(1, 64, 8, "Bit length")
    s_endian = QComboBox()
    s_endian.addItems(["Intel (little)", "Motorola (big)"])
    s_endian.setFixedHeight(28)
    s_endian.setToolTip("Byte order, CANdb++ @1 / @0")
    s_signed = QCheckBox("Signed")
    s_signed.setToolTip("Two's complement raw value")
    s_mux = QComboBox()
    s_mux.addItems(["None", "Multiplexor", "Multiplexed"])
    s_mux.setFixedHeight(28)
    s_mux.setToolTip("M = multiplexor, mN = multiplexed value")
    s_mux_val = _spin(0, 255, 0, "Multiplexer value")
    s_factor = _line("1", "Factor")
    s_offset = _line("0", "Offset")
    s_min = _line("0", "Minimum physical")
    s_max = _line("0", "Maximum physical")
    s_unit = _line("", "Unit")
    s_comment = _line("", "Signal comment")
    s_values = _line("", "Value descriptions, 0=Off; 1=On")
    s_values.setPlaceholderText("0=Off; 1=On")
    receivers = QListWidget()
    receivers.setToolTip("Receiver nodes")
    receivers.setMaximumHeight(120)
    sig_form.addRow("Name", s_name)
    sig_form.addRow("Start bit", s_start)
    sig_form.addRow("Length", s_len)
    sig_form.addRow("Byte order", s_endian)
    sig_form.addRow("", s_signed)
    sig_form.addRow("Multiplex", s_mux)
    sig_form.addRow("Mux value", s_mux_val)
    sig_form.addRow("Factor", s_factor)
    sig_form.addRow("Offset", s_offset)
    sig_form.addRow("Min", s_min)
    sig_form.addRow("Max", s_max)
    sig_form.addRow("Unit", s_unit)
    sig_form.addRow("Comment", s_comment)
    sig_form.addRow("Values", s_values)
    sig_form.addRow("Receivers", receivers)
    stack.addWidget(sig_page)

    node_page = QWidget()
    node_form = QFormLayout(node_page)
    node_form.setContentsMargins(12, 8, 12, 8)
    n_name = _line("", "Node name. Apply renames transmitter and receiver references.")
    node_form.addRow("Node", n_name)
    stack.addWidget(node_page)

    splitter.addWidget(stack)

    layout_host = QWidget()
    layout_col = QVBoxLayout(layout_host)
    layout_col.setContentsMargins(8, 4, 4, 4)
    layout_title = QLabel("Layout")
    layout_title.setObjectName("SuiteEditorTitle")
    layout_col.addWidget(layout_title)
    bit_view = BitLayout()
    layout_col.addWidget(bit_view, 1)
    splitter.addWidget(layout_host)
    splitter.setSizes([300, 420, 340])
    layout.addWidget(splitter, 1)

    state = {"kind": None, "can_id": None, "signal": None, "node": None}

    def _msg():
        cid = state["can_id"]
        if cid is None:
            return None
        return document.db.messages.get(cid)

    def _preview():
        msg = _msg()
        if msg is None:
            bit_view.clear()
            return
        if state["kind"] == "signal" and state["signal"]:
            draft = _Draft(
                state["signal"], s_start.value(), s_len.value(),
                s_endian.currentIndex() == 0)
            bit_view.set_message(msg, state["signal"], draft)
        else:
            bit_view.set_message(msg, state["signal"])

    def _fill_receivers(selected):
        receivers.clear()
        chosen = set(selected or [])
        for node in document.db.nodes:
            item = QListWidgetItem(node)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(
                Qt.CheckState.Checked if node in chosen else Qt.CheckState.Unchecked)
            receivers.addItem(item)

    def _checked_receivers():
        out = []
        for i in range(receivers.count()):
            item = receivers.item(i)
            if item.checkState() == Qt.CheckState.Checked:
                out.append(item.text())
        return out

    def _show_message(msg):
        state["kind"] = "message"
        state["can_id"] = msg.can_id
        state["signal"] = None
        state["node"] = None
        m_name.setText(msg.name)
        m_id.setText("0x%X" % msg.can_id)
        m_ext.setChecked(bool(msg.extended))
        m_dlc.setValue(int(msg.dlc))
        m_sender.setText(msg.sender or "")
        m_cycle.setValue(int(msg.cycle_time or 0))
        m_comment.setText(msg.comment or "")
        stack.setCurrentWidget(msg_page)
        _preview()

    def _show_signal(msg, sig):
        state["kind"] = "signal"
        state["can_id"] = msg.can_id
        state["signal"] = sig.name
        state["node"] = None
        s_name.setText(sig.name)
        s_start.setValue(int(sig.start_bit))
        s_len.setValue(int(sig.bit_length or 1))
        s_endian.setCurrentIndex(0 if sig.little_endian else 1)
        s_signed.setChecked(bool(sig.is_signed))
        if sig.mux_type == "multiplexor":
            s_mux.setCurrentIndex(1)
        elif sig.mux_type == "multiplexed":
            s_mux.setCurrentIndex(2)
        else:
            s_mux.setCurrentIndex(0)
        s_mux_val.setValue(int(sig.mux_value or 0))
        s_factor.setText("%s" % sig.factor)
        s_offset.setText("%s" % sig.offset)
        s_min.setText("%s" % sig.minimum)
        s_max.setText("%s" % sig.maximum)
        s_unit.setText(sig.unit or "")
        s_comment.setText(sig.comment or "")
        s_values.setText(_fmt_value_table(sig.value_table))
        _fill_receivers(sig.receivers)
        stack.setCurrentWidget(sig_page)
        _preview()

    def _show_node(name):
        state["kind"] = "node"
        state["node"] = name
        state["can_id"] = None
        state["signal"] = None
        n_name.setText(name)
        stack.setCurrentWidget(node_page)
        bit_view.clear()

    def _rebuild_tree(select_can_id=None, select_signal=None, select_node=None):
        query = filt.text().strip().lower()
        tree.blockSignals(True)
        tree.clear()
        db = document.db
        net = QTreeWidgetItem(["Network", document.display_name()])
        net.setData(0, Qt.ItemDataRole.UserRole, ("network", None, None))
        tree.addTopLevelItem(net)
        nodes_item = QTreeWidgetItem(["Nodes", "%d" % len(db.nodes)])
        nodes_item.setData(0, Qt.ItemDataRole.UserRole, ("nodes", None, None))
        net.addChild(nodes_item)
        select_item = None
        for n in db.nodes:
            if query and query not in n.lower():
                continue
            child = QTreeWidgetItem([n, ""])
            child.setData(0, Qt.ItemDataRole.UserRole, ("node", None, n))
            nodes_item.addChild(child)
            if select_node == n:
                select_item = child
        msgs_item = QTreeWidgetItem(["Messages", "%d" % len(db.messages)])
        msgs_item.setData(0, Qt.ItemDataRole.UserRole, ("messages", None, None))
        net.addChild(msgs_item)
        for cid in sorted(db.messages.keys()):
            m = db.messages[cid]
            detail = "0x%X   DLC %d   %s" % (cid, m.dlc, m.sender or "")
            visible_sigs = []
            for s in m.signals:
                if query and query not in s.name.lower() and query not in m.name.lower() and query not in detail.lower():
                    continue
                visible_sigs.append(s)
            if query and query not in m.name.lower() and query not in detail.lower() and not visible_sigs:
                continue
            mi = QTreeWidgetItem([m.name, detail])
            mi.setData(0, Qt.ItemDataRole.UserRole, ("message", cid, None))
            msgs_item.addChild(mi)
            if select_can_id == cid and not select_signal:
                select_item = mi
            for s in (m.signals if not query else visible_sigs):
                if query and query not in s.name.lower() and query not in m.name.lower():
                    continue
                sdetail = "%d|%d@%s%s" % (
                    s.start_bit, s.bit_length,
                    "1" if s.little_endian else "0",
                    "-" if s.is_signed else "+")
                si = QTreeWidgetItem([s.name, sdetail])
                si.setData(0, Qt.ItemDataRole.UserRole, ("signal", cid, s.name))
                mi.addChild(si)
                if select_can_id == cid and select_signal == s.name:
                    select_item = si
            mi.setExpanded(True)
        net.setExpanded(True)
        nodes_item.setExpanded(True)
        msgs_item.setExpanded(True)
        tree.blockSignals(False)
        if select_item is not None:
            tree.setCurrentItem(select_item)
            tree.scrollToItem(select_item)

    def _on_select():
        item = tree.currentItem()
        if item is None:
            stack.setCurrentWidget(empty)
            bit_view.clear()
            return
        kind, cid, name = item.data(0, Qt.ItemDataRole.UserRole) or (None, None, None)
        if kind == "message" and cid is not None:
            msg = document.db.messages.get(cid)
            if msg:
                _show_message(msg)
            return
        if kind == "signal" and cid is not None and name:
            msg = document.db.messages.get(cid)
            sig = msg.signal(name) if msg else None
            if msg and sig:
                _show_signal(msg, sig)
            return
        if kind == "node" and name:
            _show_node(name)
            return
        stack.setCurrentWidget(empty)
        bit_view.clear()

    def _f(edit, default=0.0):
        try:
            return float(edit.text().strip())
        except ValueError:
            return default

    def _on_apply():
        kind = state["kind"]
        if kind == "message":
            cid = state["can_id"]
            msg = document.db.messages.get(cid) if cid is not None else None
            if not msg:
                return
            new_name = m_name.text().strip() or msg.name
            try:
                new_id = int(m_id.text().strip().lower().replace("0x", ""), 16) & 0x1FFFFFFF
            except ValueError:
                QMessageBox.warning(root, "DBC", "Invalid CAN ID")
                return
            if new_id != cid and new_id in document.db.messages:
                QMessageBox.warning(root, "DBC", "CAN ID already exists")
                return
            msg.name = new_name
            msg.extended = m_ext.isChecked()
            msg.dlc = m_dlc.value()
            msg.sender = m_sender.text().strip() or "Vector__XXX"
            msg.cycle_time = m_cycle.value()
            msg.comment = m_comment.text().strip()
            if new_id != cid:
                del document.db.messages[cid]
                msg.can_id = new_id
                document.db.messages[new_id] = msg
                cid = new_id
            if msg.sender not in document.db.nodes:
                document.db.nodes.append(msg.sender)
            document.mark_dirty(True)
            log_fn("Editor", "Message %s  0x%X" % (msg.name, cid))
            _rebuild_tree(cid, None)
            return
        if kind == "signal":
            cid = state["can_id"]
            msg = document.db.messages.get(cid) if cid is not None else None
            sig = msg.signal(state["signal"]) if msg else None
            if not msg or not sig:
                return
            new_name = s_name.text().strip() or sig.name
            if new_name != sig.name and msg.signal(new_name):
                QMessageBox.warning(root, "DBC", "Signal name already exists")
                return
            sig.name = new_name
            sig.start_bit = s_start.value()
            sig.bit_length = s_len.value()
            sig.little_endian = s_endian.currentIndex() == 0
            sig.is_signed = s_signed.isChecked()
            mode = s_mux.currentIndex()
            sig.mux_type = ("", "multiplexor", "multiplexed")[mode]
            sig.mux_value = s_mux_val.value() if mode == 2 else None
            sig.factor = _f(s_factor, 1.0)
            sig.offset = _f(s_offset, 0.0)
            sig.minimum = _f(s_min, 0.0)
            sig.maximum = _f(s_max, 0.0)
            sig.unit = s_unit.text().strip()
            sig.comment = s_comment.text().strip()
            sig.value_table = _parse_value_table(s_values.text())
            sig.receivers = _checked_receivers() or ["Vector__XXX"]
            for r in sig.receivers:
                if r not in document.db.nodes:
                    document.db.nodes.append(r)
            document.mark_dirty(True)
            log_fn("Editor", "Signal %s / %s" % (msg.name, sig.name))
            state["signal"] = sig.name
            _rebuild_tree(cid, sig.name)
            return
        if kind == "node":
            old = state["node"]
            new = n_name.text().strip()
            if not old or not new or new == old:
                return
            if new in document.db.nodes:
                QMessageBox.warning(root, "DBC", "Node already exists")
                return
            document.db.nodes = [new if n == old else n for n in document.db.nodes]
            for msg in document.db.messages.values():
                if msg.sender == old:
                    msg.sender = new
                for sig in msg.signals:
                    sig.receivers = [new if r == old else r for r in sig.receivers]
            document.mark_dirty(True)
            log_fn("Editor", "Renamed node %s" % new)
            _rebuild_tree(select_node=new)

    def _on_add_message():
        dlg = _NameIdDialog(root, "New message")
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return
        name = dlg.name.text().strip()
        if not name:
            return
        try:
            cid = int(dlg.ident.text().strip().lower().replace("0x", ""), 16) & 0x1FFFFFFF
        except ValueError:
            QMessageBox.warning(root, "DBC", "Invalid CAN ID")
            return
        if cid in document.db.messages:
            QMessageBox.warning(root, "DBC", "CAN ID already exists")
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
        log_fn("Editor", "Added message %s" % name)
        _rebuild_tree(cid, None)

    def _selected_message_id():
        item = tree.currentItem()
        if item is None:
            return None
        kind, cid, _name = item.data(0, Qt.ItemDataRole.UserRole) or (None, None, None)
        if kind in ("message", "signal"):
            return cid
        return None

    def _on_add_signal():
        cid = _selected_message_id()
        if cid is None:
            QMessageBox.information(root, "DBC", "Select a message first")
            return
        msg = document.db.messages.get(cid)
        if not msg:
            return
        name, ok = _ask_name("New signal", "Signal")
        if not ok:
            return
        if msg.signal(name):
            QMessageBox.warning(root, "DBC", "Signal already exists")
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
        log_fn("Editor", "Added signal %s" % name)
        _rebuild_tree(cid, name)

    def _ask_name(title, label):
        dlg = QDialog(root)
        dlg.setWindowTitle(title)
        form = QFormLayout(dlg)
        edit = _line("", label)
        form.addRow(label, edit)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(dlg.accept)
        buttons.rejected.connect(dlg.reject)
        form.addRow(buttons)
        if dlg.exec() != QDialog.DialogCode.Accepted:
            return "", False
        return edit.text().strip(), bool(edit.text().strip())

    def _on_add_node():
        name, ok = _ask_name("New node", "Node")
        if not ok:
            return
        if name in document.db.nodes:
            QMessageBox.warning(root, "DBC", "Node already exists")
            return
        document.db.nodes.append(name)
        document.mark_dirty(True)
        log_fn("Editor", "Added node %s" % name)
        _rebuild_tree(select_node=name)

    def _on_delete():
        kind = state["kind"]
        if kind == "message" and state["can_id"] is not None:
            cid = state["can_id"]
            msg = document.db.messages.get(cid)
            label = msg.name if msg else str(cid)
            if QMessageBox.question(
                    root, "Delete message",
                    "Delete %s and its signals?" % label) != QMessageBox.StandardButton.Yes:
                return
            del document.db.messages[cid]
            document.mark_dirty(True)
            log_fn("Editor", "Deleted message 0x%X" % cid)
            state["kind"] = None
            _rebuild_tree()
            return
        if kind == "signal":
            msg = _msg()
            name = state["signal"]
            if not msg or not name:
                return
            if QMessageBox.question(
                    root, "Delete signal",
                    "Delete %s?" % name) != QMessageBox.StandardButton.Yes:
                return
            msg.signals = [s for s in msg.signals if s.name != name]
            document.mark_dirty(True)
            log_fn("Editor", "Deleted signal %s" % name)
            _rebuild_tree(msg.can_id, None)
            return
        if kind == "node" and state["node"]:
            name = state["node"]
            if QMessageBox.question(
                    root, "Delete node",
                    "Remove node %s? References become Vector__XXX." % name
            ) != QMessageBox.StandardButton.Yes:
                return
            document.db.nodes = [n for n in document.db.nodes if n != name]
            if "Vector__XXX" not in document.db.nodes:
                document.db.nodes.append("Vector__XXX")
            for msg in document.db.messages.values():
                if msg.sender == name:
                    msg.sender = "Vector__XXX"
                for sig in msg.signals:
                    sig.receivers = [
                        "Vector__XXX" if r == name else r for r in sig.receivers]
            document.mark_dirty(True)
            log_fn("Editor", "Deleted node %s" % name)
            _rebuild_tree()

    def _pick_signal(name):
        cid = state["can_id"]
        if cid is None:
            return
        _rebuild_tree(cid, name)

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

    bit_view.on_pick = _pick_signal
    for w in (s_start, s_len):
        w.valueChanged.connect(lambda _v: _preview())
    s_endian.currentIndexChanged.connect(lambda _i: _preview())
    tree.currentItemChanged.connect(lambda _c, _p: _on_select())
    filt.textChanged.connect(lambda _t: _rebuild_tree(
        state.get("can_id"), state.get("signal"), state.get("node")))
    apply_btn.clicked.connect(_on_apply)
    add_msg.clicked.connect(_on_add_message)
    add_sig.clicked.connect(_on_add_signal)
    add_node.clicked.connect(_on_add_node)
    delete_btn.clicked.connect(_on_delete)
    document.on_changed(lambda: _rebuild_tree(
        state.get("can_id"), state.get("signal"), state.get("node")))
    _rebuild_tree()
    root.select_target = select_target
    return root
