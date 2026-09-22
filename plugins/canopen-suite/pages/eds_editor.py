# -*- coding: utf-8 -*-
"""EDS editor — Vector CANeds workbench.

Tabs (Dictionary | Device | Check) mount in the suite chrome row.
Dictionary: object tree by CiA index range + definition pane.
"""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QSplitter,
    QStackedWidget,
    QTabBar,
    QTableWidget,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from core.eds_parse import (
    ACCESS_TYPES,
    DATA_TYPES,
    OBJECT_TYPES,
    OdEntry,
    data_type_label,
    export_eds_text,
    index_group,
    object_type_label,
    validate_eds,
)


def _spin_hex(lo, hi, value, tip):
    box = QSpinBox()
    box.setObjectName("SuiteSpin")
    box.setRange(lo, hi)
    box.setDisplayIntegerBase(16)
    box.setPrefix("0x")
    box.setValue(value)
    box.setFixedHeight(28)
    box.setToolTip(tip)
    return box


def _line(text, tip):
    edit = QLineEdit(text)
    edit.setFixedHeight(28)
    edit.setToolTip(tip)
    return edit


def _combo(items, tip):
    box = QComboBox()
    box.addItems(list(items))
    box.setFixedHeight(28)
    box.setToolTip(tip)
    return box


def _ghost(text, tip):
    btn = QPushButton(text)
    btn.setObjectName("GhostButton")
    btn.setFixedHeight(28)
    btn.setCursor(Qt.CursorShape.PointingHandCursor)
    btn.setToolTip(tip)
    return btn


def _code(table, label):
    for code, name in table:
        if label == name or label.lower() == code.lower():
            return code
    return label


def build(parent, session, log_fn) -> QWidget:
    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    for name in ("Dictionary", "Device", "Check"):
        bar.addTab(name)
    parent._eds_tabs = bar

    stack = QStackedWidget()
    bar.currentChanged.connect(stack.setCurrentIndex)
    selected = {"key": None}
    mute = {"on": False}
    last_tab = {"i": 0}

    # ---- Dictionary ----
    dictionary = QWidget()
    dlay = QVBoxLayout(dictionary)
    dlay.setContentsMargins(0, 0, 0, 0)
    dlay.setSpacing(0)

    tools_host = QWidget()
    tools = QHBoxLayout(tools_host)
    tools.setContentsMargins(8, 6, 8, 4)
    tools.setSpacing(6)
    filt = _line("", "Filter by name or index")
    filt.setPlaceholderText("Filter")
    filt.setMaximumWidth(180)
    add_obj = _ghost("Object", "Add an index")
    add_sub = _ghost("Sub-index", "Add a sub-object under the selected index")
    remove_btn = _ghost("Remove", "Remove the selected object")
    apply_od = QPushButton("Apply to OD")
    apply_od.setObjectName("PrimaryButton")
    apply_od.setFixedHeight(28)
    apply_od.setCursor(Qt.CursorShape.PointingHandCursor)
    apply_od.setToolTip("Copy this draft into the live object dictionary")
    save_btn = _ghost("Save", "Overwrite the loaded EDS")
    save_as = _ghost("Save As", "Write an EDS file")
    studio_btn = _ghost("EDS Studio", "Open full EDS Studio workbench")
    ai_btn = _ghost("Add to AI Chat", "Attach this EDS summary to the AI Agent")
    tools.addWidget(filt)
    tools.addWidget(add_obj)
    tools.addWidget(add_sub)
    tools.addWidget(remove_btn)
    tools.addStretch(1)
    tools.addWidget(apply_od)
    tools.addWidget(save_btn)
    tools.addWidget(save_as)
    tools.addWidget(studio_btn)
    tools.addWidget(ai_btn)
    dlay.addWidget(tools_host)

    split = QSplitter(Qt.Orientation.Horizontal)
    split.setHandleWidth(1)
    split.setChildrenCollapsible(False)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Index", "Name", "Access"])
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    tree.setMinimumWidth(280)
    split.addWidget(tree)

    form_host = QWidget()
    form_host.setMinimumWidth(300)
    form_host.setMaximumWidth(380)
    form = QFormLayout(form_host)
    form.setSpacing(8)
    form.setContentsMargins(10, 8, 10, 8)
    idx_spin = _spin_hex(0, 0xFFFF, 0x2000, "Object index")
    sub_spin = QSpinBox()
    sub_spin.setObjectName("SuiteSpin")
    sub_spin.setRange(0, 254)
    sub_spin.setFixedHeight(28)
    sub_spin.setToolTip("Sub-index. 0 is the object itself or the entry count.")
    name_edit = _line("", "ParameterName")
    obj_type = _combo([n for _c, n in OBJECT_TYPES], "CiA object type")
    data_type = _combo([n for _c, n in DATA_TYPES], "CiA data type")
    access = _combo(ACCESS_TYPES, "AccessType")
    default_edit = _line("", "DefaultValue")
    pdo = _combo(["No", "Yes"], "PDOMapping")
    low_edit = _line("", "LowLimit, optional")
    high_edit = _line("", "HighLimit, optional")
    form.addRow("Index", idx_spin)
    form.addRow("Sub-index", sub_spin)
    form.addRow("Name", name_edit)
    form.addRow("Object type", obj_type)
    form.addRow("Data type", data_type)
    form.addRow("Access", access)
    form.addRow("Default", default_edit)
    form.addRow("PDO mapping", pdo)
    form.addRow("Low limit", low_edit)
    form.addRow("High limit", high_edit)
    split.addWidget(form_host)
    split.setStretchFactor(0, 3)
    split.setStretchFactor(1, 1)
    split.setSizes([720, 340])
    dlay.addWidget(split, 1)
    stack.addWidget(dictionary)

    # ---- Device ----
    device = QWidget()
    device_scroll = QScrollArea()
    device_scroll.setWidgetResizable(True)
    device_scroll.setFrameShape(QScrollArea.Shape.NoFrame)
    device_inner = QWidget()
    device_inner.setMaximumWidth(560)
    device_form = QFormLayout(device_inner)
    device_form.setSpacing(8)
    device_form.setContentsMargins(16, 12, 16, 8)
    file_keys = (
        ("FileName", "File name stored in the EDS"),
        ("FileVersion", "File version"),
        ("FileRevision", "File revision"),
        ("EDSVersion", "EDS specification version, usually 4.0"),
        ("Description", "File description"),
        ("CreatedBy", "Author"),
        ("CreationDate", "Creation date"),
        ("ModifiedBy", "Last editor"),
    )
    device_keys = (
        ("VendorName", "Vendor name"),
        ("VendorNumber", "Vendor ID, often hex"),
        ("ProductName", "Product name"),
        ("ProductNumber", "Product code"),
        ("RevisionNumber", "Revision"),
        ("OrderCode", "Order code"),
        ("NrOfRXPDO", "Number of receive PDOs"),
        ("NrOfTXPDO", "Number of transmit PDOs"),
        ("BaudRate_10", "10 kbit/s supported (0 or 1)"),
        ("BaudRate_20", "20 kbit/s"),
        ("BaudRate_50", "50 kbit/s"),
        ("BaudRate_125", "125 kbit/s"),
        ("BaudRate_250", "250 kbit/s"),
        ("BaudRate_500", "500 kbit/s"),
        ("BaudRate_800", "800 kbit/s"),
        ("BaudRate_1000", "1 Mbit/s"),
        ("SimpleBootUpMaster", "Simple boot-up master (0 or 1)"),
        ("SimpleBootUpSlave", "Simple boot-up slave (0 or 1)"),
    )
    device_edits = {}

    def _add_meta(title):
        lab = QLabel(title)
        lab.setObjectName("SuiteEditorTitle")
        device_form.addRow(lab)

    _add_meta("File")
    for key, tip in file_keys:
        edit = _line("", tip)
        device_form.addRow(key, edit)
        device_edits[("file", key)] = edit
    _add_meta("Device")
    for key, tip in device_keys:
        edit = _line("", tip)
        device_form.addRow(key, edit)
        device_edits[("device", key)] = edit
    device_scroll.setWidget(device_inner)
    device_lay = QVBoxLayout(device)
    device_lay.setContentsMargins(0, 0, 0, 0)
    device_lay.addWidget(device_scroll)
    stack.addWidget(device)

    # ---- Check ----
    check = QWidget()
    clay = QVBoxLayout(check)
    clay.setContentsMargins(8, 6, 8, 6)
    clay.setSpacing(6)
    run_check = QPushButton("Check")
    run_check.setObjectName("PrimaryButton")
    run_check.setFixedHeight(28)
    run_check.setCursor(Qt.CursorShape.PointingHandCursor)
    run_check.setToolTip("Duplicate indexes, missing names, missing 0x1000 / 0x1018")
    clay.addWidget(run_check, 0, Qt.AlignmentFlag.AlignLeft)
    grid = QTableWidget(0, 3)
    grid.setHorizontalHeaderLabels(["Level", "Index", "Message"])
    grid.verticalHeader().setVisible(False)
    grid.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
    grid.setAlternatingRowColors(True)
    grid.horizontalHeader().setStretchLastSection(True)
    clay.addWidget(grid, 1)
    stack.addWidget(check)

    def _entries():
        return session.draft_entries

    def _store_meta_from_form():
        for (kind, key), edit in device_edits.items():
            bucket = session.eds_file_info if kind == "file" else session.eds_device_info
            text = edit.text().strip()
            if text:
                bucket[key] = text
            elif key in bucket:
                bucket.pop(key, None)

    def _load_meta_form():
        for (kind, key), edit in device_edits.items():
            bucket = session.eds_file_info if kind == "file" else session.eds_device_info
            edit.blockSignals(True)
            edit.setText(str(bucket.get(key, "")))
            edit.blockSignals(False)

    def _rebuild():
        mute["on"] = True
        tree.blockSignals(True)
        query = filt.text().strip().lower()
        tree.clear()
        groups = {}
        order = (
            "Communication profile",
            "Manufacturer",
            "Device profile",
            "Other",
        )
        for title in order:
            item = QTreeWidgetItem([title, "", ""])
            item.setData(0, Qt.ItemDataRole.UserRole, None)
            tree.addTopLevelItem(item)
            groups[title] = item
        parents = {}
        for entry in _entries():
            blob = "%s %s" % (entry.display_index(), entry.name)
            if query and query not in blob.lower():
                continue
            group = groups[index_group(entry.index)]
            if entry.subindex == 0:
                node = QTreeWidgetItem([
                    entry.display_index(), entry.name, entry.access_type])
                node.setData(0, Qt.ItemDataRole.UserRole, entry.key)
                group.addChild(node)
                parents[entry.index] = node
            else:
                parent = parents.get(entry.index)
                if parent is None:
                    parent = QTreeWidgetItem(["0x%04X" % entry.index, "", ""])
                    parent.setData(0, Qt.ItemDataRole.UserRole, (entry.index, 0))
                    group.addChild(parent)
                    parents[entry.index] = parent
                child = QTreeWidgetItem([
                    entry.display_index(), entry.name, entry.access_type])
                child.setData(0, Qt.ItemDataRole.UserRole, entry.key)
                parent.addChild(child)
        for item in groups.values():
            item.setExpanded(item.childCount() > 0 and item.childCount() < 80)
        tree.expandToDepth(1)
        tree.blockSignals(False)
        mute["on"] = False

    def _fill(entry: OdEntry):
        mute["on"] = True
        selected["key"] = entry.key
        idx_spin.setValue(entry.index)
        sub_spin.setValue(entry.subindex)
        name_edit.setText(entry.name)
        obj_type.setCurrentText(object_type_label(entry.object_type))
        data_type.setCurrentText(data_type_label(entry.data_type))
        if entry.access_type in ACCESS_TYPES:
            access.setCurrentText(entry.access_type)
        else:
            access.setCurrentText("rw")
        default_edit.setText(entry.default_value)
        pdo.setCurrentText(
            "Yes" if str(entry.pdo_mapping).strip() in ("1", "yes", "Yes") else "No")
        low_edit.setText(entry.extra.get("LowLimit", ""))
        high_edit.setText(entry.extra.get("HighLimit", ""))
        mute["on"] = False

    def _on_select():
        if mute["on"]:
            return
        item = tree.currentItem()
        if item is None:
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        if not key:
            return
        for entry in _entries():
            if entry.key == key:
                _fill(entry)
                return

    def _write_current():
        key = selected["key"]
        entry = OdEntry(
            index=idx_spin.value(),
            subindex=sub_spin.value(),
            name=name_edit.text().strip() or ("Object 0x%04X" % idx_spin.value()),
            object_type=_code(OBJECT_TYPES, obj_type.currentText()),
            data_type=_code(DATA_TYPES, data_type.currentText()),
            access_type=access.currentText(),
            default_value=default_edit.text().strip(),
            pdo_mapping="1" if pdo.currentText() == "Yes" else "0",
        )
        replaced = False
        rows = _entries()
        if key is not None:
            for i, old in enumerate(rows):
                if old.key == key:
                    entry.extra = dict(old.extra)
                    if low_edit.text().strip():
                        entry.extra["LowLimit"] = low_edit.text().strip()
                    else:
                        entry.extra.pop("LowLimit", None)
                    if high_edit.text().strip():
                        entry.extra["HighLimit"] = high_edit.text().strip()
                    else:
                        entry.extra.pop("HighLimit", None)
                    rows[i] = entry
                    replaced = True
                    break
        if not replaced:
            if low_edit.text().strip():
                entry.extra["LowLimit"] = low_edit.text().strip()
            if high_edit.text().strip():
                entry.extra["HighLimit"] = high_edit.text().strip()
            for i, old in enumerate(rows):
                if old.key == entry.key:
                    rows[i] = entry
                    replaced = True
                    break
        if not replaced:
            rows.append(entry)
        rows.sort(key=lambda e: (e.index, e.subindex))
        selected["key"] = entry.key
        return entry, replaced

    def _select_key(key):
        def walk(item):
            if item.data(0, Qt.ItemDataRole.UserRole) == key:
                tree.setCurrentItem(item)
                return True
            for i in range(item.childCount()):
                if walk(item.child(i)):
                    return True
            return False
        for i in range(tree.topLevelItemCount()):
            if walk(tree.topLevelItem(i)):
                return

    def _commit(_note):
        entry, replaced = _write_current()
        _rebuild()
        log_fn("RX", "-", b"", "%s %s" % (
            "Updated" if replaced else "Added", entry.display_index()))
        _select_key(entry.key)

    def _free_index():
        used = {e.index for e in _entries()}
        for idx in range(0x2000, 0x6000):
            if idx not in used:
                return idx
        return 0x2000

    def _apply_fields():
        if mute["on"] or selected["key"] is None:
            return
        old = selected["key"]
        entry, _replaced = _write_current()
        if entry.key != old:
            _rebuild()
            _select_key(entry.key)
            return
        item = tree.currentItem()
        if item is not None and item.data(0, Qt.ItemDataRole.UserRole) == entry.key:
            item.setText(0, entry.display_index())
            item.setText(1, entry.name)
            item.setText(2, entry.access_type)

    def _add_object():
        selected["key"] = None
        idx_spin.setValue(_free_index())
        sub_spin.setValue(0)
        name_edit.setText("New object")
        obj_type.setCurrentText("VAR")
        data_type.setCurrentText("UNSIGNED32")
        access.setCurrentText("rw")
        default_edit.clear()
        pdo.setCurrentText("No")
        low_edit.clear()
        high_edit.clear()
        _commit("Added")

    def _add_sub():
        index = idx_spin.value()
        used = {e.subindex for e in _entries() if e.index == index}
        sub = 1
        while sub in used and sub < 255:
            sub += 1
        selected["key"] = None
        sub_spin.setValue(sub)
        name_edit.setText("Sub %d" % sub)
        obj_type.setCurrentText("VAR")
        _commit("Added")

    def _remove():
        key = selected["key"]
        if key is None:
            return
        session.draft_entries = [e for e in _entries() if e.key != key]
        selected["key"] = None
        _rebuild()
        log_fn("RX", "-", b"", "Removed 0x%04X:%02X" % key)

    def _text():
        _store_meta_from_form()
        base = os.path.basename(session.eds_path) if session.eds_path else "export.eds"
        return export_eds_text(
            session.draft_entries,
            file_name=base,
            file_info=session.eds_file_info,
            device_info=session.eds_device_info,
            other_meta=getattr(session, "eds_other_meta", None),
        )

    def _write(path):
        try:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(_text())
        except OSError as exc:
            QMessageBox.warning(parent, "Save failed", str(exc))
            return
        session.eds_path = path
        log_fn("RX", "-", b"", "Saved EDS %s" % path)
        plugin_shell.set_status(parent, "Saved %s" % path, 4000)

    def _flush_object():
        if selected["key"] is not None:
            _write_current()

    def _save():
        _flush_object()
        if session.eds_path:
            _write(session.eds_path)
        else:
            _save_as()

    def _save_as():
        _flush_object()
        path, _ = QFileDialog.getSaveFileName(
            parent, "Save EDS", session.eds_path or "device.eds", "EDS (*.eds)")
        if path:
            _write(path)

    def _apply_od():
        _flush_object()
        session.sync_od_from_draft()
        log_fn("RX", "-", b"", "EDS draft applied (%d objects)" % len(session.od_entries))
        plugin_shell.set_status(parent, "Draft applied to object dictionary", 3000)

    def _attach_ai():
        entries = session.draft_entries or session.od_entries
        summary = {
            "object_count": len(entries),
            "node_id": session.node_id,
            "path": session.eds_path or "",
            "file_info": dict(session.eds_file_info or {}),
            "device_info": dict(session.eds_device_info or {}),
        }
        path = session.eds_path or ""
        try:
            import sin
            sin.ai.attach_eds(
                path, summary=summary,
                title=os.path.basename(path) or "EDS")
            plugin_shell.set_status(parent, "EDS attached to AI Chat", 3000)
        except Exception as exc:
            try:
                from _shared import ai_attach
                ai_attach.attach_eds(path, summary=summary)
                plugin_shell.set_status(parent, "EDS attached to AI inbox", 3000)
            except Exception:
                QMessageBox.information(
                    parent, "AI Agent",
                    "Could not attach EDS (%s). Is AI Agent available?" % exc)

    def _open_studio():
        """Hand off to file-centric EDS Studio (full validate / compare / export)."""
        path = session.eds_path or ""
        try:
            from _shared import state_store
            if path:
                state_store.save_state("eds-studio", {
                    "last_path": path,
                    "from_canopen": True,
                })
            import sin
            sin.commands.execute("edsStudio.open")
            plugin_shell.set_status(parent, "Opening EDS Studio…", 2500)
        except Exception as exc:
            QMessageBox.information(
                parent, "EDS Studio",
                "Could not open EDS Studio (%s).\n"
                "Install the eds-studio plugin from the marketplace." % exc)

    def _check():
        grid.setRowCount(0)
        for finding in validate_eds(session.draft_entries):
            row = grid.rowCount()
            grid.insertRow(row)
            for col, text in enumerate((
                finding["level"], finding.get("index", ""), finding["message"],
            )):
                grid.setItem(row, col, QTableWidgetItem(text))
        log_fn("SYS", "-", b"", "EDS check (%d)" % grid.rowCount())

    def _on_tab(index):
        stack.setCurrentIndex(index)
        if last_tab["i"] == 1 and index != 1:
            _store_meta_from_form()
        if index == 1:
            _load_meta_form()
        if index == 2:
            _check()
        last_tab["i"] = index

    tree.itemSelectionChanged.connect(_on_select)
    add_obj.clicked.connect(_add_object)
    add_sub.clicked.connect(_add_sub)
    remove_btn.clicked.connect(_remove)
    apply_od.clicked.connect(_apply_od)
    save_btn.clicked.connect(_save)
    save_as.clicked.connect(_save_as)
    studio_btn.clicked.connect(_open_studio)
    ai_btn.clicked.connect(_attach_ai)
    filt.textChanged.connect(lambda _t: _rebuild())
    run_check.clicked.connect(_check)
    bar.currentChanged.connect(_on_tab)
    name_edit.editingFinished.connect(_apply_fields)
    default_edit.editingFinished.connect(_apply_fields)
    low_edit.editingFinished.connect(_apply_fields)
    high_edit.editingFinished.connect(_apply_fields)
    idx_spin.editingFinished.connect(_apply_fields)
    sub_spin.editingFinished.connect(_apply_fields)
    for box in (obj_type, data_type, access, pdo):
        box.currentIndexChanged.connect(lambda _i: _apply_fields())

    def _refresh(_=None):
        _rebuild()
        _load_meta_form()

    session.on_od_changed(_refresh)
    _refresh()
    return stack
