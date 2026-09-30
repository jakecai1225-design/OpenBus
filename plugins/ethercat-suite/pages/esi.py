# -*- coding: utf-8 -*-
"""ESI editor — Tree | Validate | Save (vs TwinCAT / EC-Engineer description tooling).

Edits Vendor, Device, Rx/Tx PDO entries and CoE objects. Round-trips the ESI
subset we parse. Does not generate ENI or open a NIC.
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
    QTabBar,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.esi import CoeObject, Pdo, PdoEntry, serialize_esi, validate_esi
from widgets import ghost, primary, spin, spin_hex, table


def _ensure_device(session):
    slave = session.current()
    if slave is None:
        return None
    if slave.device is None:
        from core.esi import EsiDevice
        slave.device = EsiDevice(
            name=slave.name or "Slave",
            type_name="Slave",
            vendor_id=slave.vendor_id,
            vendor_name="",
            product_code=slave.product_code,
            revision=0,
        )
    return slave


def _tree_tab(session):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    row = QHBoxLayout()
    open_btn = ghost("Open", "folder", "Load EtherCATInfo XML")
    demo = ghost("Demo", "refresh", "Load Demo Drive ESI")
    add_entry = ghost("Add PDO entry", "add", "Append an entry on the selected PDO")
    add_obj = ghost("Add object", "add", "Append a CoE object")
    remove = ghost("Remove", "delete", "Remove selected entry or object")
    apply = primary("Apply", "Write the property form into the ESI model")
    for w in (open_btn, demo, add_entry, add_obj, remove, apply):
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
    vendor = spin(0, 0x7FFFFFFF, 0, "Vendor Id")
    product = spin_hex(0, 0x7FFFFFFF, 0, "Product code")
    index = spin_hex(0, 0xFFFF, 0x1600, "Index")
    sub = spin(0, 255, 0, "Subindex / object sub")
    bits = spin(1, 64, 16, "Bit length")
    type_ed = QLineEdit("UINT")
    type_ed.setFixedHeight(28)
    form.addRow("Name", name_ed)
    form.addRow("Vendor", vendor)
    form.addRow("Product", product)
    form.addRow("Index", index)
    form.addRow("Sub / bits", sub)
    form.addRow("BitLen", bits)
    form.addRow("Type", type_ed)
    split.addWidget(form_host, 1)
    root.addLayout(split, 1)

    selection = {"kind": "", "pdo_dir": "", "pdo_i": -1, "entry_i": -1, "obj_i": -1}

    def refill():
        tree.clear()
        slave = session.current()
        device = slave.device if slave else None
        if device is None:
            return
        top = QTreeWidgetItem([device.name, "0x%X / 0x%X" % (device.vendor_id, device.product_code)])
        top.setData(0, Qt.ItemDataRole.UserRole, ("device", "", -1, -1, -1))
        tree.addTopLevelItem(top)
        for direction, pdos in (("rx", device.rx_pdos), ("tx", device.tx_pdos)):
            for pi, pdo in enumerate(pdos):
                p_item = QTreeWidgetItem([
                    "%s %s" % (pdo.direction, pdo.name),
                    "0x%04X" % pdo.index,
                ])
                p_item.setData(0, Qt.ItemDataRole.UserRole, ("pdo", direction, pi, -1, -1))
                for ei, entry in enumerate(pdo.entries):
                    e_item = QTreeWidgetItem([
                        entry.name,
                        "0x%04X:%d  %d bit" % (entry.index, entry.subindex, entry.bit_len),
                    ])
                    e_item.setData(
                        0, Qt.ItemDataRole.UserRole, ("entry", direction, pi, ei, -1))
                    p_item.addChild(e_item)
                top.addChild(p_item)
                p_item.setExpanded(True)
        objs = QTreeWidgetItem(["Objects", str(len(device.objects))])
        objs.setData(0, Qt.ItemDataRole.UserRole, ("objects", "", -1, -1, -1))
        for oi, obj in enumerate(device.objects):
            o_item = QTreeWidgetItem([
                "0x%04X %s" % (obj.index, obj.name), obj.type_name])
            o_item.setData(0, Qt.ItemDataRole.UserRole, ("object", "", -1, -1, oi))
            objs.addChild(o_item)
        top.addChild(objs)
        top.setExpanded(True)

    def load_form():
        slave = _ensure_device(session)
        if slave is None or slave.device is None:
            return
        device = slave.device
        kind = selection["kind"]
        if kind == "device":
            name_ed.setText(device.name)
            vendor.setValue(device.vendor_id)
            product.setValue(device.product_code)
        elif kind == "pdo":
            pdos = device.rx_pdos if selection["pdo_dir"] == "rx" else device.tx_pdos
            pdo = pdos[selection["pdo_i"]]
            name_ed.setText(pdo.name)
            index.setValue(pdo.index)
        elif kind == "entry":
            pdos = device.rx_pdos if selection["pdo_dir"] == "rx" else device.tx_pdos
            entry = pdos[selection["pdo_i"]].entries[selection["entry_i"]]
            name_ed.setText(entry.name)
            index.setValue(entry.index)
            sub.setValue(entry.subindex)
            bits.setValue(entry.bit_len)
        elif kind == "object":
            obj = device.objects[selection["obj_i"]]
            name_ed.setText(obj.name)
            index.setValue(obj.index)
            bits.setValue(obj.bit_size)
            type_ed.setText(obj.type_name)

    def on_select():
        item = tree.currentItem()
        if not item:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        kind, direction, pi, ei, oi = data
        selection.update(kind=kind, pdo_dir=direction, pdo_i=pi, entry_i=ei, obj_i=oi)
        load_form()

    def do_apply():
        slave = _ensure_device(session)
        if slave is None:
            return
        device = slave.device
        kind = selection["kind"]
        if kind == "device":
            device.name = name_ed.text().strip() or device.name
            device.vendor_id = vendor.value()
            device.product_code = product.value()
            slave.name = device.name
            slave.vendor_id = device.vendor_id
            slave.product_code = device.product_code
        elif kind == "pdo":
            pdos = device.rx_pdos if selection["pdo_dir"] == "rx" else device.tx_pdos
            pdo = pdos[selection["pdo_i"]]
            pdo.name = name_ed.text().strip() or pdo.name
            pdo.index = index.value()
        elif kind == "entry":
            pdos = device.rx_pdos if selection["pdo_dir"] == "rx" else device.tx_pdos
            entry = pdos[selection["pdo_i"]].entries[selection["entry_i"]]
            entry.name = name_ed.text().strip() or entry.name
            entry.index = index.value()
            entry.subindex = sub.value()
            entry.bit_len = bits.value()
        elif kind == "object":
            obj = device.objects[selection["obj_i"]]
            obj.name = name_ed.text().strip() or obj.name
            obj.index = index.value()
            obj.bit_size = bits.value()
            obj.type_name = type_ed.text().strip() or obj.type_name
        session.notify()
        session.log("SYS", "-", b"", "ESI applied")

    def do_add_entry():
        slave = _ensure_device(session)
        if slave is None:
            return
        device = slave.device
        if not device.rx_pdos:
            device.rx_pdos.append(Pdo(0x1600, "RxPDO", "Rx", 2, []))
        target = device.rx_pdos[0]
        if selection["kind"] in ("pdo", "entry"):
            pdos = device.rx_pdos if selection["pdo_dir"] == "rx" else device.tx_pdos
            if 0 <= selection["pdo_i"] < len(pdos):
                target = pdos[selection["pdo_i"]]
        target.entries.append(PdoEntry(0x7000, len(target.entries) + 1, 16, "NewEntry"))
        session.notify()

    def do_add_obj():
        slave = _ensure_device(session)
        if slave is None:
            return
        slave.device.objects.append(
            CoeObject(0x2000 + len(slave.device.objects), "NewObject", "UDINT", 32))
        session.notify()

    def do_remove():
        slave = session.current()
        if slave is None or slave.device is None:
            return
        device = slave.device
        kind = selection["kind"]
        if kind == "entry":
            pdos = device.rx_pdos if selection["pdo_dir"] == "rx" else device.tx_pdos
            del pdos[selection["pdo_i"]].entries[selection["entry_i"]]
            session.notify()
        elif kind == "object":
            del device.objects[selection["obj_i"]]
            session.notify()

    def open_file():
        path, _ = QFileDialog.getOpenFileName(
            page, "Open ESI", "", "ESI (*.xml);;All (*.*)")
        if path:
            session.load_esi(path)

    open_btn.clicked.connect(open_file)
    demo.clicked.connect(session.load_sample)
    add_entry.clicked.connect(do_add_entry)
    add_obj.clicked.connect(do_add_obj)
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
    tip = QLabel("TwinCAT-style sanity: Vendor, PDO entries, 0x1000 presence.")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    root.addWidget(tip)
    run = primary("Validate", "Check the loaded ESI model")
    root.addWidget(run, 0, Qt.AlignmentFlag.AlignLeft)
    grid = table(["Level", "Where", "Message"])
    root.addWidget(grid, 1)

    def run_check():
        grid.setRowCount(0)
        slave = session.current()
        if slave is None or slave.device is None:
            session.log("ERR", "-", b"", "No ESI loaded")
            return
        for finding in validate_esi(slave.device):
            r = grid.rowCount()
            grid.insertRow(r)
            for c, text in enumerate((
                finding["level"], finding.get("where", ""), finding["message"],
            )):
                grid.setItem(r, c, QTableWidgetItem(text))

    run.clicked.connect(run_check)
    return page


def _save_tab(session):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    tip = QLabel(
        "Save EtherCATInfo XML for TwinCAT / EC-Engineer import of this subset. "
        "ENI generation is out of scope.")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    root.addWidget(tip)
    save = primary("Save ESI…", "Serialize Vendor / Device / PDO / Objects")
    root.addWidget(save, 0, Qt.AlignmentFlag.AlignLeft)
    root.addStretch(1)

    def do_save():
        slave = session.current()
        if slave is None or slave.device is None:
            session.log("ERR", "-", b"", "No ESI to save")
            return
        path, _ = QFileDialog.getSaveFileName(
            page, "Save ESI", slave.esi_path or "slave.xml", "ESI (*.xml)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(serialize_esi(slave.device))
        except OSError as exc:
            session.log("ERR", "-", b"", str(exc))
            return
        slave.esi_path = path
        session.log("SYS", "-", b"", "Saved ESI %s" % path)

    save.clicked.connect(do_save)
    return page


def build(parent, session, _log):
    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    for name in ("Tree", "Validate", "Save"):
        bar.addTab(name)
    stack = QStackedWidget()
    stack.addWidget(_tree_tab(session))
    stack.addWidget(_validate_tab(session))
    stack.addWidget(_save_tab(session))
    bar.currentChanged.connect(stack.setCurrentIndex)
    parent._esi_tabs = bar
    return stack
