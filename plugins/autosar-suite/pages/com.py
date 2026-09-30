# -*- coding: utf-8 -*-
"""COM workspace — Layout / Live / Pack (Side Bar switches views)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QStackedWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.ipdu import Signal, blank_pdu, pack_signal, unpack_signal
from widgets import combo, ghost, primary, spin, table


def _item(text: str) -> QTableWidgetItem:
    cell = QTableWidgetItem(text)
    cell.setFlags(cell.flags() & ~Qt.ItemFlag.ItemIsEditable)
    return cell


def _endian_box(value: str) -> QComboBox:
    box = combo(["intel", "motorola"], "intel = LSB first; motorola = MSB first")
    box.setCurrentText(value if value in ("intel", "motorola") else "intel")
    return box


class _Layout(QWidget):
    def __init__(self, session):
        super().__init__()
        self.session = session
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 8, 12, 8)
        root.setSpacing(8)
        row = QHBoxLayout()
        self.pdu = combo([], "Active I-PDU")
        self.can_id = spin(0, 0x1FFFFFFF, 0, "CAN identifier")
        self.can_id.setDisplayIntegerBase(16)
        self.can_id.setPrefix("0x")
        self.dlc = spin(0, 64, 8, "PDU length in bytes")
        row.addWidget(self.pdu, 1)
        row.addWidget(self.can_id)
        row.addWidget(self.dlc)
        row.addWidget(ghost("Add", "add", "Add a signal to this I-PDU"))
        row.itemAt(row.count() - 1).widget().clicked.connect(self._add)
        row.addWidget(ghost("Remove", "delete", "Remove the selected signal"))
        row.itemAt(row.count() - 1).widget().clicked.connect(self._remove)
        row.addWidget(primary("Apply", "Write this layout into the shared session"))
        row.itemAt(row.count() - 1).widget().clicked.connect(self._apply)
        root.addLayout(row)
        self.grid = table(["Signal", "Start bit", "Length", "Endian", "Factor", "Offset", "Unit"])
        self.grid.setEditTriggers(
            QAbstractItemView.EditTrigger.DoubleClicked
            | QAbstractItemView.EditTrigger.EditKeyPressed)
        root.addWidget(self.grid, 1)
        self.pdu.currentIndexChanged.connect(self._pick)
        session.on_changed(self.reload)
        self.reload()

    def reload(self):
        self.pdu.blockSignals(True)
        self.pdu.clear()
        for pdu in self.session.ipdus:
            self.pdu.addItem("%s  0x%X" % (pdu.name, pdu.can_id), pdu)
        if self.session.ipdus:
            self.pdu.setCurrentIndex(self.session.active)
        self.pdu.blockSignals(False)
        self._fill()

    def _pick(self, index: int):
        if index < 0:
            return
        self.session.active = index
        self._fill()

    def _fill(self):
        self.grid.setRowCount(0)
        pdu = self.session.active_pdu()
        if pdu is None:
            return
        self.can_id.blockSignals(True)
        self.dlc.blockSignals(True)
        self.can_id.setValue(int(pdu.can_id))
        self.dlc.setValue(int(pdu.dlc))
        self.can_id.blockSignals(False)
        self.dlc.blockSignals(False)
        for sig in pdu.signals:
            row = self.grid.rowCount()
            self.grid.insertRow(row)
            self.grid.setItem(row, 0, QTableWidgetItem(sig.name))
            self.grid.setItem(row, 1, QTableWidgetItem(str(sig.start_bit)))
            self.grid.setItem(row, 2, QTableWidgetItem(str(sig.length)))
            self.grid.setCellWidget(row, 3, _endian_box(sig.endian))
            self.grid.setItem(row, 4, QTableWidgetItem(str(sig.factor)))
            self.grid.setItem(row, 5, QTableWidgetItem(str(sig.offset)))
            self.grid.setItem(row, 6, QTableWidgetItem(sig.unit))

    def _add(self):
        pdu = self.session.active_pdu()
        if pdu is None:
            return
        pdu.signals.append(Signal("Signal%d" % (len(pdu.signals) + 1), 0, 8))
        self._fill()

    def _remove(self):
        pdu = self.session.active_pdu()
        if pdu is None:
            return
        row = self.grid.currentRow()
        if 0 <= row < len(pdu.signals):
            del pdu.signals[row]
            self._fill()

    def _apply(self):
        pdu = self.session.active_pdu()
        if pdu is None:
            return
        pdu.can_id = int(self.can_id.value())
        pdu.dlc = int(self.dlc.value())
        sigs = []
        for row in range(self.grid.rowCount()):
            name = (self.grid.item(row, 0).text()
                    if self.grid.item(row, 0) else "Signal")
            try:
                start = int(self.grid.item(row, 1).text() or "0")
                length = int(self.grid.item(row, 2).text() or "1")
                factor = float(self.grid.item(row, 4).text() or "1")
                offset = float(self.grid.item(row, 5).text() or "0")
            except (TypeError, ValueError):
                self.session.log("ERR", "-", b"", "Bad layout row %d" % row)
                return
            box = self.grid.cellWidget(row, 3)
            endian = box.currentText() if box else "intel"
            unit = (self.grid.item(row, 6).text()
                    if self.grid.item(row, 6) else "")
            sigs.append(Signal(name, start, length, endian, factor, offset, unit))
        pdu.signals = sigs
        self.session.notify()
        self.session.log("OK", pdu.can_id, b"", "Layout applied %s" % pdu.name)


class _Live(QWidget):
    """Multi-PDU Live monitor — tracks all session I-PDUs (not only active)."""

    def __init__(self, session):
        super().__init__()
        self.session = session
        self._last: dict = {}  # can_id -> (data, ts note)
        self._group_on = True
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 8, 12, 8)
        root.setSpacing(6)
        row = QHBoxLayout()
        self.hint = QLabel("Waiting for bus frames matching project I-PDUs")
        self.hint.setObjectName("SuiteHint")
        row.addWidget(self.hint, 1)
        self.multi = QCheckBox("All PDUs")
        self.multi.setChecked(True)
        self.multi.setToolTip(
            "Monitor every I-PDU in the session (not only the active one)")
        self.group = QCheckBox("I-PDU group")
        self.group.setChecked(True)
        self.group.setToolTip(
            "When off, Live ignores frames (start/stop simulation lite)")
        row.addWidget(self.multi)
        row.addWidget(self.group)
        root.addLayout(row)
        self.grid = table([
            "PDU", "CAN ID", "Signal", "Raw", "Physical", "Unit", "Status"])
        root.addWidget(self.grid, 1)
        session.on_bus(self._on_bus)
        session.on_changed(self._reset)
        self.group.toggled.connect(self._on_group)
        self.multi.toggled.connect(lambda _c: self._paint())

    def _on_group(self, on: bool):
        self._group_on = bool(on)
        self.hint.setText(
            "I-PDU group stopped — Live paused"
            if not on else "Waiting for bus frames matching project I-PDUs")

    def _reset(self):
        self._last.clear()
        self._paint()

    def _on_bus(self, cid, data):
        if not self._group_on:
            return
        matched = [p for p in (self.session.ipdus or []) if p.can_id == cid]
        if not matched:
            return
        self._last[cid] = bytes(data)
        self._paint()

    def _paint(self):
        self.grid.setRowCount(0)
        pdus = list(self.session.ipdus or [])
        if not self.multi.isChecked():
            active = self.session.active_pdu()
            pdus = [active] if active else []
        seen = 0
        for pdu in pdus:
            data = self._last.get(pdu.can_id)
            status_pdu = "live" if data is not None else "timeout"
            if data is None:
                data = blank_pdu(pdu)
            else:
                seen += 1
            for sig in pdu.signals:
                raw, phys = unpack_signal(data, sig)
                # Timeout lite: no frame yet for this CAN id
                status = status_pdu
                if status_pdu == "live" and getattr(sig, "length", 0):
                    # Update-bit lite: if signal is 1-bit named *Update* show raw
                    if "update" in (sig.name or "").lower() and sig.length == 1:
                        status = "ub=%s" % raw
                row = self.grid.rowCount()
                self.grid.insertRow(row)
                self.grid.setItem(row, 0, _item(pdu.name))
                self.grid.setItem(row, 1, _item("0x%X" % pdu.can_id))
                self.grid.setItem(row, 2, _item(sig.name))
                self.grid.setItem(row, 3, _item(str(raw)))
                self.grid.setItem(row, 4, _item("%.6g" % phys))
                self.grid.setItem(row, 5, _item(sig.unit))
                self.grid.setItem(row, 6, _item(status))
        n = len(pdus)
        self.hint.setText(
            "Live  %d/%d PDU(s) with frames" % (seen, n)
            if n else "No I-PDU")


class _Pack(QWidget):
    def __init__(self, session):
        super().__init__()
        self.session = session
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 8, 12, 8)
        root.setSpacing(8)
        row = QHBoxLayout()
        row.addStretch(1)
        send = primary("Pack and send", "Pack physical values and send the active CAN id")
        send.clicked.connect(self._send)
        row.addWidget(send)
        root.addLayout(row)
        self.grid = table(["Signal", "Physical", "Unit"])
        root.addWidget(self.grid, 1)
        session.on_changed(self.reload)
        self.reload()

    def reload(self):
        self.grid.setRowCount(0)
        pdu = self.session.active_pdu()
        if pdu is None:
            return
        for sig in pdu.signals:
            row = self.grid.rowCount()
            self.grid.insertRow(row)
            self.grid.setItem(row, 0, _item(sig.name))
            edit = QLineEdit("0")
            edit.setFixedHeight(28)
            edit.setToolTip("Physical value (%s)" % (sig.unit or "raw scale"))
            self.grid.setCellWidget(row, 1, edit)
            self.grid.setItem(row, 2, _item(sig.unit))

    def _send(self):
        pdu = self.session.active_pdu()
        if pdu is None:
            return
        data = blank_pdu(pdu)
        for row, sig in enumerate(pdu.signals):
            widget = self.grid.cellWidget(row, 1)
            text = widget.text().strip() if widget else "0"
            try:
                phys = float(text or "0")
            except ValueError:
                self.session.log("ERR", "-", b"", "Bad value for %s" % sig.name)
                return
            data = pack_signal(data, sig, phys)
        self.session.send(pdu.can_id, data, "COM %s" % pdu.name)


def build(parent, session, _log):
    """COM workspace — Layout / Live / Pack (Side Bar switches the stack)."""
    stack = QStackedWidget()
    stack.setObjectName("SuiteEditorStack")
    stack.addWidget(_Layout(session))
    stack.addWidget(_Live(session))
    stack.addWidget(_Pack(session))
    stack.setCurrentIndex(1)  # Live default
    parent._com_stack = stack
    parent._com_tabs = None
    parent._com_feature_keys = ["com_layout", "com_live", "com_pack"]

    def select_com_view(index: int):
        if 0 <= index < stack.count():
            stack.setCurrentIndex(index)

    stack.select_com_view = select_com_view  # type: ignore[attr-defined]
    return stack
