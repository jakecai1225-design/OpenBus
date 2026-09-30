# -*- coding: utf-8 -*-
"""Editor — OD tree + definition form + FileInfo / DeviceInfo / Commissioning."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFormLayout,
    QFrame,
    QHBoxLayout,
    QLabel,
    QScrollArea,
    QSizePolicy,
    QSplitter,
    QStackedWidget,
    QTabBar,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import edsparse, suite_chrome, vscode_theme
from pages import _ui


def _code(table, label):
    for code, name in table:
        if label == name or label.lower() == code.lower():
            return code
    return label


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    # In-page Dictionary | Device segment (not chrome — leaves own the tab strip).
    bar = QTabBar()
    bar.setObjectName("SuiteSegmentTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    bar.setFixedHeight(_ui.TOOL_H)
    bar.setToolTip("Dictionary: OD tree · Device: FileInfo / Commissioning")
    for name in ("Dictionary", "Device"):
        bar.addTab(name)

    stack = QStackedWidget()
    bar.currentChanged.connect(stack.setCurrentIndex)
    layout.addWidget(bar)
    layout.addWidget(stack, 1)

    selected = {"key": None}
    mute = {"on": False}

    # ---- Dictionary ----
    dictionary = QWidget()
    dlay = QVBoxLayout(dictionary)
    dlay.setContentsMargins(0, 0, 0, 0)
    dlay.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    filt = _ui.line_edit("Filter by name or index (Ctrl+F)", "Filter…")
    filt.setMaximumWidth(180)
    count = _ui.count_label()
    add_obj = _ui.ghost_btn("Object", "Add a new index (manufacturer area)", "add")
    add_sub = _ui.ghost_btn("Sub", "Add a sub-index under the selection", "add")
    dup_btn = _ui.ghost_btn("Dup", "Duplicate selected object (next free index)", "add")
    remove_btn = _ui.ghost_btn("", "Remove selected object", "delete")
    apply_btn = _ui.primary_btn("Apply", "Write form into the selected entry", "apply")
    crow.addWidget(filt)
    crow.addWidget(count)
    crow.addStretch(1)
    crow.addWidget(add_obj)
    crow.addWidget(add_sub)
    crow.addWidget(dup_btn)
    crow.addWidget(remove_btn)
    crow.addWidget(apply_btn)
    dlay.addWidget(chrome)

    split = QSplitter(Qt.Orientation.Horizontal)
    split.setHandleWidth(1)
    split.setChildrenCollapsible(False)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Index", "Name", "Type", "Access"])
    _ui.style_tree(tree, stretch_col=1)
    tree.setMinimumWidth(280)
    tree.setToolTip(
        "Object dictionary by CiA range (CANeds-style hierarchy)")
    split.addWidget(tree)

    form_wrap = QWidget()
    form_wrap.setMaximumWidth(_ui.FORM_MAX_W + 40)
    form_wrap.setMinimumWidth(280)
    form_wrap.setSizePolicy(
        QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Expanding)
    fw = QVBoxLayout(form_wrap)
    fw.setContentsMargins(0, 0, 0, 0)
    fw.setSpacing(0)

    empty = _ui.empty_state(
        "Select an object",
        "Pick an index in the tree to edit its definition")
    fw.addWidget(empty)

    form_host = QWidget()
    form = QFormLayout(form_host)
    form.setSpacing(8)
    form.setContentsMargins(12, 10, 12, 10)
    vscode_theme.tune_form(form)

    idx_spin = _ui.spin_hex(0, 0xFFFF, 0x2000, "Object index")
    sub_spin = _ui.spin(0, 254, 0, "Sub-index (0 = object / entry count)", width=100)
    name_edit = _ui.line_edit("ParameterName")
    obj_type = _ui.combo([n for _c, n in edsparse.OBJECT_TYPES], "ObjectType")
    data_type = _ui.combo([n for _c, n in edsparse.DATA_TYPES], "DataType")
    access = _ui.combo(edsparse.ACCESS_TYPES, "AccessType")
    default_edit = _ui.line_edit("DefaultValue")
    param_edit = _ui.line_edit(
        "ParameterValue — DCF commissioning value (emotas-style)")
    pdo = _ui.combo(["No", "Yes"], "PDOMapping")
    low_edit = _ui.line_edit("LowLimit (optional)")
    high_edit = _ui.line_edit("HighLimit (optional)")
    to_param = _ui.ghost_btn(
        "→ Param", "Copy DefaultValue into ParameterValue (mark as DCF)", "")
    for label, w in (
        ("Index", idx_spin),
        ("Sub-index", sub_spin),
        ("Name", name_edit),
        ("Object type", obj_type),
        ("Data type", data_type),
        ("Access", access),
        ("Default", default_edit),
        ("Param value", param_edit),
        ("", to_param),
        ("PDO map", pdo),
        ("Low", low_edit),
        ("High", high_edit),
    ):
        form.addRow(label, w)

    form_scroll = QScrollArea()
    form_scroll.setWidgetResizable(True)
    form_scroll.setFrameShape(QFrame.Shape.NoFrame)
    form_scroll.setWidget(form_host)
    form_scroll.hide()
    fw.addWidget(form_scroll, 1)
    split.addWidget(form_wrap)
    split.setStretchFactor(0, 5)
    split.setStretchFactor(1, 2)
    dlay.addWidget(split, 1)
    stack.addWidget(dictionary)

    # ---- Device ----
    device = QWidget()
    device.setObjectName("SuiteContent")
    dvl = QVBoxLayout(device)
    suite_chrome.page_margins(dvl, top=16)
    dvl.setSpacing(12)

    meta_host = QWidget()
    meta_host.setMaximumWidth(_ui.FORM_MAX_W + 80)
    meta_form = QFormLayout(meta_host)
    vscode_theme.tune_form(meta_form)
    fi_name = _ui.line_edit("FileInfo.FileName")
    fi_ver = _ui.line_edit("FileInfo.FileVersion")
    fi_desc = _ui.line_edit("FileInfo.Description")
    di_vendor = _ui.line_edit("DeviceInfo.VendorName")
    di_product = _ui.line_edit("DeviceInfo.ProductName")
    dc_node = _ui.line_edit("DeviceCommissioning.NodeID (1–127)")
    dc_baud = _ui.line_edit("DeviceCommissioning.BaudRate (kbit/s)")
    dc_name = _ui.line_edit("DeviceCommissioning.NodeName")
    for label, w in (
        ("File name", fi_name),
        ("File version", fi_ver),
        ("Description", fi_desc),
        ("Vendor", di_vendor),
        ("Product", di_product),
        ("Node ID", dc_node),
        ("Baud rate", dc_baud),
        ("Node name", dc_name),
    ):
        meta_form.addRow(label, w)
    dvl.addWidget(meta_host)

    meta_apply = _ui.primary_btn(
        "Apply", "Write FileInfo / DeviceInfo / Commissioning", "apply")
    row = QHBoxLayout()
    row.addWidget(meta_apply)
    row.addStretch(1)
    dvl.addLayout(row)
    tip = _ui.quiet_label(
        "Commissioning fields mark the file as DCF when filled.")
    tip.setMaximumWidth(_ui.FORM_MAX_W + 80)
    dvl.addWidget(tip)
    dvl.addStretch(1)
    stack.addWidget(device)

    def _show_form(on: bool):
        empty.setVisible(not on)
        form_scroll.setVisible(on)

    def _autofit():
        tree.resizeColumnToContents(0)
        tree.resizeColumnToContents(2)

    def _fill_form(entry: edsparse.OdEntry):
        mute["on"] = True
        idx_spin.setValue(entry.index)
        sub_spin.setValue(entry.subindex)
        name_edit.setText(entry.name)
        obj_type.setCurrentText(edsparse.object_type_label(entry.object_type))
        data_type.setCurrentText(edsparse.data_type_label(entry.data_type))
        access.setCurrentText((entry.access_type or "rw").lower())
        default_edit.setText(entry.default_value)
        param_edit.setText(entry.parameter_value)
        pdo.setCurrentText(
            "Yes" if (entry.pdo_mapping or "").lower() in ("1", "yes", "true")
            else "No")
        low_edit.setText(entry.low_limit or entry.extra.get("LowLimit", ""))
        high_edit.setText(entry.high_limit or entry.extra.get("HighLimit", ""))
        mute["on"] = False
        _show_form(True)

    def _fill_meta():
        mute["on"] = True
        eds = document.eds
        fi_name.setText(eds.file_info.get("FileName", ""))
        fi_ver.setText(eds.file_info.get("FileVersion", ""))
        fi_desc.setText(eds.file_info.get("Description", ""))
        di_vendor.setText(eds.device_info.get("VendorName", ""))
        di_product.setText(eds.device_info.get("ProductName", ""))
        dc = eds.device_commissioning
        dc_node.setText(dc.get("NodeID") or dc.get("NodeId") or "")
        dc_baud.setText(dc.get("BaudRate", ""))
        dc_name.setText(dc.get("NodeName", ""))
        mute["on"] = False

    def _rebuild_tree(prefer=None):
        q = (filt.text() or "").strip().lower()
        tree.blockSignals(True)
        tree.clear()
        n = 0
        groups = {}
        for e in document.eds.entries:
            if q and q not in e.name.lower() and q not in e.display_index().lower():
                continue
            n += 1
            gname = edsparse.index_group(e.index)
            if gname not in groups:
                gitem = QTreeWidgetItem([gname, "", "", ""])
                gitem.setFlags(gitem.flags() & ~Qt.ItemFlag.ItemIsSelectable)
                tree.addTopLevelItem(gitem)
                groups[gname] = gitem
            parent = groups[gname]
            dtype = edsparse.data_type_label(e.data_type) if e.data_type else ""
            if e.subindex == 0:
                item = QTreeWidgetItem([
                    e.display_index(), e.name, dtype, e.access_type or ""])
                item.setData(0, Qt.ItemDataRole.UserRole, (e.index, e.subindex))
                parent.addChild(item)
            else:
                idx_item = None
                for i in range(parent.childCount()):
                    ch = parent.child(i)
                    key = ch.data(0, Qt.ItemDataRole.UserRole)
                    if key and key[0] == e.index and key[1] == 0:
                        idx_item = ch
                        break
                if idx_item is None:
                    idx_item = QTreeWidgetItem([
                        "0x%04X" % e.index, "", "", ""])
                    idx_item.setData(
                        0, Qt.ItemDataRole.UserRole, (e.index, 0))
                    parent.addChild(idx_item)
                item = QTreeWidgetItem([
                    e.display_index(), e.name, dtype, e.access_type or ""])
                item.setData(0, Qt.ItemDataRole.UserRole, (e.index, e.subindex))
                idx_item.addChild(item)
        # Expand CiA groups + indexes so subs are one click away (CANeds feel)
        tree.expandToDepth(1)
        _autofit()
        tree.blockSignals(False)
        total = len(document.eds.entries)
        count.setText("%d shown · %d total" % (n, total) if q else
                      "%d objects" % total)
        if prefer is not None:
            select_object(prefer[0], prefer[1])
        elif not selected["key"]:
            _show_form(False)
        _fill_meta()

    def select_object(index, subindex=0):
        if index is None:
            return
        bar.setCurrentIndex(0)
        target = (int(index), int(subindex or 0))

        def walk(item):
            for i in range(item.childCount()):
                ch = item.child(i)
                key = ch.data(0, Qt.ItemDataRole.UserRole)
                if key == target:
                    tree.setCurrentItem(ch)
                    tree.scrollToItem(ch)
                    return True
                if walk(ch):
                    return True
            return False

        for i in range(tree.topLevelItemCount()):
            if walk(tree.topLevelItem(i)):
                break


    def _on_select():
        item = tree.currentItem()
        if not item:
            _show_form(False)
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        if not key:
            _show_form(False)
            return
        selected["key"] = key
        document.set_focus(key[0], key[1])
        entry = edsparse.find_entry(document.eds.entries, key[0], key[1])
        if entry:
            _fill_form(entry)

    def _apply_form():
        if mute["on"]:
            return
        eds = document.clone_eds()
        idx = idx_spin.value()
        sub = sub_spin.value()
        entry = edsparse.find_entry(eds.entries, idx, sub)
        if entry is None:
            entry = edsparse.OdEntry(index=idx, subindex=sub)
            eds.entries.append(entry)
            eds.entries.sort(key=lambda e: (e.index, e.subindex))
        entry.name = name_edit.text().strip()
        entry.object_type = _code(
            edsparse.OBJECT_TYPES, obj_type.currentText())
        entry.data_type = _code(
            edsparse.DATA_TYPES, data_type.currentText())
        entry.access_type = access.currentText()
        entry.default_value = default_edit.text().strip()
        entry.parameter_value = param_edit.text().strip()
        if entry.parameter_value:
            eds.is_dcf = True
        entry.pdo_mapping = "1" if pdo.currentText() == "Yes" else "0"
        entry.low_limit = low_edit.text().strip()
        entry.high_limit = high_edit.text().strip()
        EdsDocument = type(document)
        if hasattr(EdsDocument, "_sync_sub_numbers"):
            EdsDocument._sync_sub_numbers(eds)
        document.apply_eds(eds)
        selected["key"] = (idx, sub)
        document.set_focus(idx, sub)
        log_fn("OK", "Updated %s" % entry.display_index())

    def _apply_meta():
        eds = document.clone_eds()
        eds.file_info["FileName"] = fi_name.text().strip()
        eds.file_info["FileVersion"] = fi_ver.text().strip()
        eds.file_info["Description"] = fi_desc.text().strip()
        eds.device_info["VendorName"] = di_vendor.text().strip()
        eds.device_info["ProductName"] = di_product.text().strip()
        if dc_node.text().strip() or dc_baud.text().strip() or dc_name.text().strip():
            eds.device_commissioning["NodeID"] = dc_node.text().strip()
            eds.device_commissioning["BaudRate"] = dc_baud.text().strip()
            eds.device_commissioning["NodeName"] = dc_name.text().strip()
            eds.is_dcf = True
        document.apply_eds(eds)
        log_fn("OK", "Device meta updated")

    def _add_object():
        eds = document.clone_eds()
        idx = 0x2000
        existing = {e.index for e in eds.entries}
        while idx in existing and idx < 0x5FFF:
            idx += 1
        eds.entries.append(edsparse.OdEntry(
            index=idx, name="New object", object_type="0x7",
            data_type="0x0007", access_type="rw"))
        eds.entries.sort(key=lambda e: (e.index, e.subindex))
        document.apply_eds(eds)
        select_object(idx, 0)
        log_fn("SYS", "Added 0x%04X" % idx)

    def _add_sub():
        key = selected["key"]
        if not key:
            log_fn("WARN", "Select an index first")
            return
        eds = document.clone_eds()
        idx = key[0]
        used = {e.subindex for e in eds.entries if e.index == idx}
        sub = 1
        while sub in used and sub < 254:
            sub += 1
        eds.entries.append(edsparse.OdEntry(
            index=idx, subindex=sub, name="Sub %d" % sub,
            object_type="0x7", data_type="0x0007", access_type="rw"))
        eds.entries.sort(key=lambda e: (e.index, e.subindex))
        type(document)._sync_sub_numbers(eds)
        document.apply_eds(eds)
        select_object(idx, sub)
        log_fn("SYS", "Added 0x%04X:%02X" % (idx, sub))

    def _duplicate():
        key = selected["key"]
        if not key:
            log_fn("WARN", "Select an object to duplicate")
            return
        eds = document.clone_eds()
        src = edsparse.find_entry(eds.entries, key[0], key[1])
        if src is None:
            return
        group = [e for e in eds.entries if e.index == key[0]]
        existing = {e.index for e in eds.entries}
        new_idx = 0x2000
        while new_idx in existing and new_idx < 0x5FFF:
            new_idx += 1
        import copy as _copy
        for e in group:
            ne = _copy.deepcopy(e)
            ne.index = new_idx
            if e.subindex == 0 and e.name:
                ne.name = e.name + " (copy)"
            eds.entries.append(ne)
        eds.entries.sort(key=lambda e: (e.index, e.subindex))
        type(document)._sync_sub_numbers(eds)
        document.apply_eds(eds)
        select_object(new_idx, key[1])
        log_fn("SYS", "Duplicated 0x%04X -> 0x%04X" % (key[0], new_idx))

    def _copy_default_to_param():
        if mute["on"]:
            return
        val = default_edit.text().strip()
        if not val:
            log_fn("WARN", "DefaultValue is empty")
            return
        param_edit.setText(val)
        _apply_form()
        log_fn("OK", "ParameterValue <- DefaultValue (DCF)")

    def _remove():
        key = selected["key"]
        if not key:
            return
        eds = document.clone_eds()
        eds.entries = [
            e for e in eds.entries
            if not (e.index == key[0] and e.subindex == key[1])]
        type(document)._sync_sub_numbers(eds)
        document.apply_eds(eds)
        selected["key"] = None
        _show_form(False)
        log_fn("SYS", "Removed 0x%04X:%02X" % key)

    def refresh():
        prefer = selected["key"]
        if prefer is None and document.focus_index is not None:
            prefer = (document.focus_index, document.focus_subindex)
        _rebuild_tree(prefer)

    def _on_focus():
        if document.focus_index is None:
            return
        select_object(document.focus_index, document.focus_subindex)

    def _ctx_menu(pos):
        item = tree.itemAt(pos)
        if item is None:
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        if not key:
            return
        from PyQt6.QtWidgets import QMenu
        menu = QMenu(tree)
        act_pdo = menu.addAction("Open PDO Map")
        act_val = menu.addAction("Open Validate")
        chosen = menu.exec(tree.viewport().mapToGlobal(pos))
        document.set_focus(key[0], key[1])
        if chosen is act_pdo and hasattr(shell, "goto_page"):
            shell.goto_page("pdo")
        elif chosen is act_val and hasattr(shell, "goto_page"):
            shell.goto_page("validate")

    def _on_doc():
        _rebuild_tree(selected["key"])

    tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    tree.customContextMenuRequested.connect(_ctx_menu)
    tree.itemSelectionChanged.connect(_on_select)
    filt.textChanged.connect(lambda *_: _rebuild_tree(selected["key"]))
    apply_btn.clicked.connect(_apply_form)
    meta_apply.clicked.connect(_apply_meta)
    add_obj.clicked.connect(_add_object)
    add_sub.clicked.connect(_add_sub)
    dup_btn.clicked.connect(_duplicate)
    to_param.clicked.connect(_copy_default_to_param)
    remove_btn.clicked.connect(_remove)
    for w in (name_edit, default_edit, param_edit, low_edit, high_edit):
        w.editingFinished.connect(_apply_form)
    for box in (obj_type, data_type, access, pdo):
        box.currentIndexChanged.connect(
            lambda *_: _apply_form() if not mute["on"] else None)
    document.on_changed(_on_doc)
    document.on_focus(_on_focus)
    _rebuild_tree()
    _show_form(False)

    root.select_object = select_object
    root.refresh = refresh
    return root
