# -*- coding: utf-8 -*-
"""COM workspace — Layout / Live / Pack as editor tabs."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QStackedWidget,
    QTabBar,
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
        pdu = self.session.active_pdu()
        self.grid.setRowCount(0)
        if pdu is None:
            return
        self.can_id.setValue(pdu.can_id)
        self.dlc.setValue(pdu.dlc)
        for sig in pdu.signals:
            row = self.grid.rowCount()
            self.grid.insertRow(row)
            self.grid.setItem(row, 0, QTableWidgetItem(sig.name))
            self.grid.setItem(row, 1, QTableWidgetItem(str(sig.start_bit)))
            self.grid.setItem(row, 2, QTableWidgetItem(str(sig.length)))
            self.grid.setCellWidget(row, 3, _endian_box(sig.endian))
            self.grid.setItem(row, 4, QTableWidgetItem("%s" % sig.factor))
            self.grid.setItem(row, 5, QTableWidgetItem("%s" % sig.offset))
            self.grid.setItem(row, 6, QTableWidgetItem(sig.unit))

    def _add(self):
        pdu = self.session.active_pdu()
        if pdu is None:
            return
        pdu.signals.append(Signal("NewSignal", 0, 8, "intel", 1.0, 0.0, ""))
        self._fill()

    def _remove(self):
        pdu = self.session.active_pdu()
        row = self.grid.currentRow()
        if pdu is None or row < 0 or row >= len(pdu.signals):
            return
        del pdu.signals[row]
        self._fill()

    def _cell(self, row: int, col: int, default: str = "") -> str:
        item = self.grid.item(row, col)
        return item.text().strip() if item and item.text() else default

    def _apply(self):
        pdu = self.session.active_pdu()
        if pdu is None:
            return
        pdu.can_id = self.can_id.value()
        pdu.dlc = self.dlc.value()
        sigs = []
        for row in range(self.grid.rowCount()):
            endian = "intel"
            box = self.grid.cellWidget(row, 3)
            if isinstance(box, QComboBox):
                endian = box.currentText()
            try:
                sigs.append(Signal(
                    self._cell(row, 0, "Signal"),
                    int(self._cell(row, 1, "0")),
                    max(1, int(self._cell(row, 2, "1"))),
                    endian,
                    float(self._cell(row, 4, "1")),
                    float(self._cell(row, 5, "0")),
                    self._cell(row, 6, ""),
                ))
            except ValueError:
                self.session.log("ERR", "-", b"", "Layout row %d is not numeric" % (row + 1))
                return
        pdu.signals = sigs
        self.session.notify()
        self.session.log("SYS", pdu.can_id, b"", "Layout applied: %s" % pdu.name)


class _Live(QWidget):
    def __init__(self, session):
        super().__init__()
        self.session = session
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 8, 12, 8)
        self.hint = QLabel("Waiting for the active I-PDU")
        self.hint.setObjectName("SuiteHint")
        root.addWidget(self.hint)
        self.grid = table(["Signal", "Raw", "Physical", "Unit"])
        root.addWidget(self.grid, 1)
        session.on_bus(self._on_bus)
        session.on_changed(self._reset)

    def _reset(self):
        self.grid.setRowCount(0)
        pdu = self.session.active_pdu()
        self.hint.setText(
            "Live  0x%X  %s" % (pdu.can_id, pdu.name) if pdu else "No I-PDU")

    def _on_bus(self, cid, data):
        pdu = self.session.active_pdu()
        if pdu is None or cid != pdu.can_id:
            return
        self.hint.setText("Live  0x%X  %s  (%d B)" % (cid, pdu.name, len(data)))
        self.grid.setRowCount(0)
        for sig in pdu.signals:
            raw, phys = unpack_signal(data, sig)
            row = self.grid.rowCount()
            self.grid.insertRow(row)
            self.grid.setItem(row, 0, _item(sig.name))
            self.grid.setItem(row, 1, _item(str(raw)))
            self.grid.setItem(row, 2, _item("%.6g" % phys))
            self.grid.setItem(row, 3, _item(sig.unit))


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
            edit.setFixedHeight(26)
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
    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    for name in ("Layout", "Live", "Pack"):
        bar.addTab(name)
    stack = QStackedWidget()
    stack.addWidget(_Layout(session))
    stack.addWidget(_Live(session))
    stack.addWidget(_Pack(session))
    bar.currentChanged.connect(stack.setCurrentIndex)
    bar.setCurrentIndex(1)
    parent._com_tabs = bar
    return stack
