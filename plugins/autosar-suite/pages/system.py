# -*- coding: utf-8 -*-
"""System / ARXML editor — Tree | Validate | Export (vs DaVinci / TSMaster COM).

Not a full AUTOSAR Classic toolchain. Round-trips the COM extract we parse:
I-SIGNAL, I-SIGNAL-I-PDU, CAN-FRAME, CAN-FRAME-TRIGGERING.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QStackedWidget,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.arxml_min import export_dbc, serialize_arxml, validate_ipdus
from core.ipdu import Ipdu, Signal
from widgets import combo, ghost, primary, spin, spin_hex, table


def _tree_tab(session, shell=None):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    row = QHBoxLayout()
    open_btn = ghost("Open", "folder", "Import ARXML COM extract")
    bsw_btn = ghost("BSW Config", "extensions", "Open BSW module configurator")
    demo = ghost("Demo", "refresh", "Load BodyStatus sample")
    add_pdu = ghost("Add PDU", "add", "Append an empty I-PDU")
    add_sig = ghost("Add signal", "add", "Append a signal on the selected PDU")
    remove = ghost("Remove", "delete", "Remove selected PDU or signal")
    apply = primary("Apply", "Write property form back into the session")
    for w in (open_btn, bsw_btn, demo, add_pdu, add_sig, remove, apply):
        row.addWidget(w)
    row.addStretch(1)
    root.addLayout(row)

    split = QHBoxLayout()
    tree = QTreeWidget()
    tree.setHeaderLabels(["Name", "Detail"])
    tree.setAlternatingRowColors(True)
    tree.header().setStretchLastSection(True)
    split.addWidget(tree, 2)

    form_host = QWidget()
    form_host.setMaximumWidth(360)
    form = QFormLayout(form_host)
    form.setSpacing(8)
    name_ed = QLineEdit()
    name_ed.setFixedHeight(28)
    can_id = spin_hex(0, 0x1FFFFFFF, 0, "CAN identifier")
    dlc = spin(0, 64, 8, "PDU length")
    start = spin(0, 512, 0, "Start bit")
    length = spin(1, 64, 8, "Bit length")
    endian = combo(["intel", "motorola"], "Packing byte order")
    factor = QLineEdit("1")
    factor.setFixedHeight(28)
    offset = QLineEdit("0")
    offset.setFixedHeight(28)
    unit = QLineEdit()
    unit.setFixedHeight(28)
    form.addRow("Name", name_ed)
    form.addRow("CAN id", can_id)
    form.addRow("DLC", dlc)
    form.addRow("Start bit", start)
    form.addRow("Length", length)
    form.addRow("Endian", endian)
    form.addRow("Factor", factor)
    form.addRow("Offset", offset)
    form.addRow("Unit", unit)
    split.addWidget(form_host, 1)
    root.addLayout(split, 1)

    selection = {"kind": "", "pdu": -1, "sig": -1}

    def refill():
        tree.clear()
        for i, pdu in enumerate(session.ipdus):
            top = QTreeWidgetItem([
                pdu.name, "0x%X  %d B  %d sig" % (pdu.can_id, pdu.dlc, len(pdu.signals))])
            top.setData(0, Qt.ItemDataRole.UserRole, ("pdu", i, -1))
            for j, sig in enumerate(pdu.signals):
                child = QTreeWidgetItem([
                    sig.name,
                    "bit %d len %d %s" % (sig.start_bit, sig.length, sig.endian),
                ])
                child.setData(0, Qt.ItemDataRole.UserRole, ("sig", i, j))
                top.addChild(child)
            tree.addTopLevelItem(top)
            top.setExpanded(True)

    def load_form():
        kind = selection["kind"]
        pi = selection["pdu"]
        if kind == "pdu" and 0 <= pi < len(session.ipdus):
            pdu = session.ipdus[pi]
            name_ed.setText(pdu.name)
            can_id.setValue(pdu.can_id)
            dlc.setValue(pdu.dlc)
            for w in (start, length, endian, factor, offset, unit):
                w.setEnabled(False)
            can_id.setEnabled(True)
            dlc.setEnabled(True)
        elif kind == "sig" and 0 <= pi < len(session.ipdus):
            sj = selection["sig"]
            sig = session.ipdus[pi].signals[sj]
            name_ed.setText(sig.name)
            start.setValue(sig.start_bit)
            length.setValue(sig.length)
            endian.setCurrentText(sig.endian if sig.endian in ("intel", "motorola") else "intel")
            factor.setText("%g" % sig.factor)
            offset.setText("%g" % sig.offset)
            unit.setText(sig.unit)
            for w in (start, length, endian, factor, offset, unit):
                w.setEnabled(True)
            can_id.setEnabled(False)
            dlc.setEnabled(False)
        else:
            name_ed.clear()

    def on_select():
        item = tree.currentItem()
        if not item:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        kind, pi, sj = data
        selection["kind"] = kind
        selection["pdu"] = pi
        selection["sig"] = sj
        session.active = pi
        load_form()

    def do_apply():
        kind = selection["kind"]
        pi = selection["pdu"]
        if kind == "pdu" and 0 <= pi < len(session.ipdus):
            pdu = session.ipdus[pi]
            pdu.name = name_ed.text().strip() or pdu.name
            pdu.can_id = can_id.value()
            pdu.dlc = dlc.value()
            session.notify()
            session.log("SYS", pdu.can_id, b"", "PDU updated: %s" % pdu.name)
        elif kind == "sig" and 0 <= pi < len(session.ipdus):
            sj = selection["sig"]
            sig = session.ipdus[pi].signals[sj]
            sig.name = name_ed.text().strip() or sig.name
            sig.start_bit = start.value()
            sig.length = length.value()
            sig.endian = endian.currentText()
            try:
                sig.factor = float(factor.text() or "1")
                sig.offset = float(offset.text() or "0")
            except ValueError:
                session.log("ERR", "-", b"", "Factor/offset must be numeric")
                return
            sig.unit = unit.text().strip()
            session.notify()
            session.log("SYS", session.ipdus[pi].can_id, b"", "Signal %s" % sig.name)

    def do_add_pdu():
        session.ipdus.append(Ipdu("NewPdu", 0x100 + len(session.ipdus), 8, []))
        session.active = len(session.ipdus) - 1
        session.notify()

    def do_add_sig():
        pi = selection["pdu"] if selection["pdu"] >= 0 else session.active
        if not session.ipdus:
            do_add_pdu()
            pi = 0
        session.ipdus[pi].signals.append(Signal("NewSignal", 0, 8, "intel", 1.0, 0.0, ""))
        session.active = pi
        session.notify()

    def do_remove():
        kind = selection["kind"]
        pi = selection["pdu"]
        if kind == "sig" and 0 <= pi < len(session.ipdus):
            sj = selection["sig"]
            del session.ipdus[pi].signals[sj]
            session.notify()
        elif kind == "pdu" and 0 <= pi < len(session.ipdus):
            del session.ipdus[pi]
            session.active = max(0, pi - 1)
            session.notify()

    def open_file():
        path, _ = QFileDialog.getOpenFileName(
            page, "Open ARXML", "", "ARXML (*.arxml *.xml);;All (*.*)")
        if path:
            session.load_arxml(path)
            session.arxml_path = path

    def open_bsw():
        if shell is not None and hasattr(shell, "goto_page"):
            shell.goto_page("bsw")
        else:
            session.log("SYS", "-", b"", "BSW page unavailable")

    open_btn.clicked.connect(open_file)
    bsw_btn.clicked.connect(open_bsw)
    demo.clicked.connect(session.load_sample)
    add_pdu.clicked.connect(do_add_pdu)
    add_sig.clicked.connect(do_add_sig)
    remove.clicked.connect(do_remove)
    apply.clicked.connect(do_apply)
    tree.itemSelectionChanged.connect(on_select)
    session.on_changed(refill)
    refill()
    return page


def _validate_tab(session):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    tip = QLabel("Checks for COM extract: names, DLC, overlaps, duplicate CAN ids.")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    root.addWidget(tip)
    run = primary("Validate", "Run layout checks")
    root.addWidget(run, 0, Qt.AlignmentFlag.AlignLeft)
    grid = table(["Level", "PDU", "Signal", "Message"])
    root.addWidget(grid, 1)

    def run_check():
        grid.setRowCount(0)
        for finding in validate_ipdus(session.ipdus):
            r = grid.rowCount()
            grid.insertRow(r)
            for c, text in enumerate((
                finding["level"], finding.get("pdu", ""),
                finding.get("signal", ""), finding["message"],
            )):
                grid.setItem(r, c, QTableWidgetItem(text))
        session.log("SYS", "-", b"", "ARXML validate: %d finding(s)" % (
            0 if (grid.rowCount() == 1 and grid.item(0, 0).text() == "info")
            else grid.rowCount()))

    run.clicked.connect(run_check)
    session.on_changed(lambda: None)
    return page


def _export_tab(session):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    tip = QLabel(
        "Save the COM extract as ARXML (reloadable here) or DBC (open in DBC Studio). "
        "Not a DaVinci full-project export.")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    root.addWidget(tip)
    row = QHBoxLayout()
    save_arxml = primary("Save ARXML…", "Serialize I-PDU model to AUTOSAR XML subset")
    save_dbc = ghost("Export DBC…", "export", "CANdb++ / DBC Studio handoff")
    row.addWidget(save_arxml)
    row.addWidget(save_dbc)
    row.addStretch(1)
    root.addLayout(row)
    root.addStretch(1)

    def do_arxml():
        path, _ = QFileDialog.getSaveFileName(
            page, "Save ARXML", session.arxml_path or "com_extract.arxml",
            "ARXML (*.arxml *.xml)")
        if not path:
            return
        try:
            text = serialize_arxml(session.ipdus)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
        except OSError as exc:
            session.log("ERR", "-", b"", str(exc))
            return
        session.arxml_path = path
        session.log("SYS", "-", b"", "Saved ARXML %s" % path)

    def do_dbc():
        path, _ = QFileDialog.getSaveFileName(
            page, "Export DBC", "com_extract.dbc", "DBC (*.dbc)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(export_dbc(session.ipdus))
        except OSError as exc:
            session.log("ERR", "-", b"", str(exc))
            return
        session.log("SYS", "-", b"", "Exported DBC %s" % path)

    save_arxml.clicked.connect(do_arxml)
    save_dbc.clicked.connect(do_dbc)
    return page


def build(parent, session, _log):
    """System page — Tree / Validate / Export via Side Bar (Bus → System)."""
    if not hasattr(session, "arxml_path"):
        session.arxml_path = ""
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)
    stack = QStackedWidget()
    stack.addWidget(_tree_tab(session, parent))
    stack.addWidget(_validate_tab(session))
    stack.addWidget(_export_tab(session))
    layout.addWidget(stack, 1)

    view_keys = ("system_tree", "system_validate", "system_export")

    def select_view(key_or_index):
        if isinstance(key_or_index, int):
            idx = key_or_index
        else:
            try:
                idx = view_keys.index(key_or_index)
            except ValueError:
                idx = 0
        if 0 <= idx < stack.count():
            stack.setCurrentIndex(idx)

    root.select_view = select_view
    root._system_stack = stack
    return root
