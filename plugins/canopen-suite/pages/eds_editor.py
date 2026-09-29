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
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSplitter,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from pages import _ui, interop
from core.eds_parse import (
    ACCESS_TYPES,
    DATA_TYPES,
    OBJECT_TYPES,
    EdsDocument,
    OdEntry,
    data_type_label,
    export_eds_text,
    index_group,
    object_type_label,
    validate_document,
)


def _line(text, tip):
    edit = QLineEdit(text)
    edit.setFixedHeight(_ui.CTRL_H)
    edit.setToolTip(tip)
    return edit


def _combo(items, tip):
    box = QComboBox()
    box.addItems(list(items))
    box.setFixedHeight(_ui.CTRL_H)
    box.setToolTip(tip)
    return box


def _ghost(text, tip, icon=""):
    return _ui.ghost_btn(text, tip, icon)


def _code(table, label):
    for code, name in table:
        if label == name or label.lower() == code.lower():
            return code
    return label


def build(parent, session, log_fn) -> QWidget:
    stack = QStackedWidget()
    stack.setObjectName("SuiteEditorStack")
    parent._eds_tabs = None
    parent._eds_feature_keys = [
        "eds_dict", "eds_device", "eds_check", "eds_pdo", "eds_codegen"]
    selected = {"key": None}
    mute = {"on": False}
    last_tab = {"i": 0}

    # ---- Dictionary ----
    dictionary = QWidget()
    dlay = QVBoxLayout(dictionary)
    dlay.setContentsMargins(0, 0, 0, 0)
    dlay.setSpacing(0)

    empty_new = _ui.primary_btn(
        "New EDS…", "Create a new EDS (Ctrl+N)", "add")
    empty_open = _ghost("Open EDS…", "Open .eds / .dcf (Ctrl+O)", "folder")
    empty_profile = _ghost(
        "Insert Profile", "Add CiA objects from Profiles", "database")
    empty_pdo = _ghost(
        "PDO map", "Map objects after you have a draft", "pdo")
    empty = _ui.empty_state(
        "No EDS yet",
        "Create or open a file, insert Profiles, map PDOs, then Validate.",
        actions=[empty_new, empty_open, empty_profile, empty_pdo],
    )
    empty.setVisible(False)
    dlay.addWidget(empty, 1)

    work = QWidget()
    work_lay = QVBoxLayout(work)
    work_lay.setContentsMargins(0, 0, 0, 0)
    work_lay.setSpacing(0)

    split = QSplitter(Qt.Orientation.Horizontal)

    left = QWidget()
    left_lay = QVBoxLayout(left)
    left_lay.setContentsMargins(0, 0, 0, 0)
    left_lay.setSpacing(0)

    tree = interop.OdObjectTree()
    tree.setHeaderLabels(["Index", "Name", "Access"])
    tree.setAlternatingRowColors(True)
    tree.setMinimumWidth(_ui.PANE_MIN)
    _ui.style_tree(tree, header_hidden=False)
    tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    # Index / Access keep floors so "rw"/"ro" never clips under the splitter.
    _ui.configure_columns(
        tree, stretch=1, mins={0: 80, 1: 140, 2: 56})
    left_lay.addWidget(tree, 1)
    split.addWidget(left)

    form_host = QWidget()
    form_host.setObjectName("SuitePropPanel")
    form_host.setMinimumWidth(_ui.PANE_MIN_PROP)
    form_wrap = QVBoxLayout(form_host)
    form_wrap.setContentsMargins(0, 0, 0, 0)
    form_wrap.setSpacing(0)

    add_obj = _ui.chip_btn("Object", "Add a new object index to the EDS", "add")
    add_sub = _ui.chip_btn(
        "Sub", "Add a sub-index under the selected object", "add")
    remove_btn = _ui.chip_btn("Remove", "Remove the selected object", "remove")
    apply_od = _ui.primary_btn(
        "Apply", "Copy this EDS draft into Live OD for SDO", "apply")
    device_btn = _ui.chip_btn(
        "Info", "File / Device metadata (FileInfo, DeviceInfo)", "device")
    map_btn = _ui.chip_btn(
        "Map", "Open PDO map with this object focused", "flow")
    form_wrap.addWidget(
        _ui.panel_header(
            "Definition", add_obj, add_sub, remove_btn, map_btn,
            device_btn, apply_od))
    form = QFormLayout()
    form.setSpacing(8)
    form.setContentsMargins(_ui.PAD_X, 8, _ui.PAD_X, _ui.PAD_X)
    form.setFieldGrowthPolicy(
        QFormLayout.FieldGrowthPolicy.FieldsStayAtSizeHint)
    form.setLabelAlignment(
        Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
    form.setFormAlignment(
        Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
    form_wrap.addLayout(form)
    idx_spin = _ui.suite_spin(
        0x2000, minimum=0, maximum=0xFFFF, hex_mode=True,
        tip="Object index", width=140)
    sub_spin = _ui.suite_spin(
        0, minimum=0, maximum=254,
        tip="Sub-index. 0 is the object itself or the entry count.",
        width=100)
    name_edit = _line("", "ParameterName")
    name_edit.setMinimumWidth(220)
    obj_type = _combo([n for _c, n in OBJECT_TYPES], "CiA object type")
    obj_type.setMinimumWidth(180)
    data_type = _combo([n for _c, n in DATA_TYPES], "CiA data type")
    data_type.setMinimumWidth(180)
    access = _combo(ACCESS_TYPES, "AccessType")
    access.setMinimumWidth(120)
    default_edit = _line("", "DefaultValue")
    default_edit.setMinimumWidth(220)
    param_edit = _line("", "ParameterValue (DCF actual value, optional)")
    param_edit.setMinimumWidth(220)
    pdo = _combo(["No", "Yes"], "PDOMapping")
    pdo.setMinimumWidth(100)
    low_edit = _line("", "LowLimit, optional")
    low_edit.setMinimumWidth(160)
    high_edit = _line("", "HighLimit, optional")
    high_edit.setMinimumWidth(160)
    live_btn = _ui.primary_btn(
        "Open Live OD",
        "Open Device → Live OD and select this index for SDO read/write",
        "device")
    form.addRow(_ui.field_label("Index"), idx_spin)
    form.addRow(_ui.field_label("Sub-index"), sub_spin)
    form.addRow(_ui.field_label("Name"), name_edit)
    form.addRow(_ui.field_label("Object type"), obj_type)
    form.addRow(_ui.field_label("Data type"), data_type)
    form.addRow(_ui.field_label("Access"), access)
    form.addRow(_ui.field_label("Default"), default_edit)
    form.addRow(_ui.field_label("Parameter"), param_edit)
    form.addRow(_ui.field_label("PDO map"), pdo)
    form.addRow(_ui.field_label("Low limit"), low_edit)
    form.addRow(_ui.field_label("High limit"), high_edit)
    form.addRow("", live_btn)
    split.addWidget(form_host)
    # OD tree (primary) | Definition form → golden major : minor
    _ui.configure_splitter(split, golden=True, master_left=True)
    work_lay.addWidget(split, 1)
    dlay.addWidget(work, 1)
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
    device_form.setContentsMargins(_ui.PAD_X * 2, 12, _ui.PAD_X * 2, 8)
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
    commission_keys = (
        ("NodeID", "Commissioned Node-ID (1–127)"),
        ("NodeName", "Commissioned node name"),
        ("BaudRate", "Commissioned bitrate (e.g. 500)"),
        ("NetNumber", "Network number"),
        ("LSS_SerialNumber", "LSS serial number"),
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
    _add_meta("Device commissioning")
    for key, tip in commission_keys:
        edit = _line("", tip)
        device_form.addRow(key, edit)
        device_edits[("comm", key)] = edit
    device_scroll.setWidget(device_inner)
    device_lay = QVBoxLayout(device)
    device_lay.setContentsMargins(0, 0, 0, 0)
    device_lay.addWidget(device_scroll)
    stack.addWidget(device)

    # ---- Check ----
    check = QWidget()
    clay = QVBoxLayout(check)
    clay.setContentsMargins(_ui.PAD_X, 6, _ui.PAD_X, 6)
    clay.setSpacing(6)
    check_tools = QHBoxLayout()
    check_tools.setSpacing(_ui.GAP)
    run_check = _ui.primary_btn("Check", "Deep validate + CiA profile coverage")
    profile_box = QComboBox()
    profile_box.addItems([
        "Auto (from 0x1000)", "(none)", "301", "302", "401", "402", "404", "406", "418",
    ])
    profile_box.setFixedHeight(_ui.CTRL_H)
    profile_box.setToolTip("Profile expected-object coverage (auto from Device type)")
    ready_lbl = QLabel("")
    ready_lbl.setObjectName("SuiteCount")
    check_tools.addWidget(run_check)
    check_tools.addWidget(_ui.field_label("Profile"))
    check_tools.addWidget(profile_box)
    check_tools.addStretch(1)
    check_tools.addWidget(ready_lbl)
    clay.addLayout(check_tools)
    next_host = QWidget()
    next_host.setVisible(False)
    next_lay = QHBoxLayout(next_host)
    next_lay.setContentsMargins(0, 0, 0, 0)
    next_lay.setSpacing(_ui.GAP)
    next_lab = QLabel("Next")
    next_lab.setObjectName("SuiteFieldLabel")
    save_next = _ui.primary_btn("Save", "Save EDS (Ctrl+S)", "save")
    apply_next = _ui.ghost_btn(
        "Apply → Live OD", "Copy draft into Live OD", "apply")
    codegen_next = _ui.ghost_btn(
        "Codegen", "Emit OD C/H from this draft", "play")
    next_lay.addWidget(next_lab)
    next_lay.addWidget(save_next)
    next_lay.addWidget(apply_next)
    next_lay.addWidget(codegen_next)
    next_lay.addStretch(1)
    clay.addWidget(next_host)
    grid = QTableWidget(0, 3)
    grid.setHorizontalHeaderLabels(["Level", "Index", "Message"])
    grid.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
    _ui.style_table(grid)
    _ui.configure_columns(grid, stretch=2)
    clay.addWidget(grid, 1)
    stack.addWidget(check)

    def _entries():
        return session.draft_entries

    def _store_meta_from_form():
        if not hasattr(session, "eds_device_commissioning"):
            session.eds_device_commissioning = {}
        for (kind, key), edit in device_edits.items():
            if kind == "file":
                bucket = session.eds_file_info
            elif kind == "device":
                bucket = session.eds_device_info
            else:
                bucket = session.eds_device_commissioning
            text = edit.text().strip()
            if text:
                bucket[key] = text
            elif key in bucket:
                bucket.pop(key, None)

    def _load_meta_form():
        for (kind, key), edit in device_edits.items():
            if kind == "file":
                bucket = session.eds_file_info
            elif kind == "device":
                bucket = session.eds_device_info
            else:
                bucket = getattr(session, "eds_device_commissioning", {}) or {}
            edit.blockSignals(True)
            edit.setText(str(bucket.get(key, "")))
            edit.blockSignals(False)

    def _rebuild():
        mute["on"] = True
        tree.blockSignals(True)
        query = ""
        tree.clear()
        entries = _entries()
        has_eds = bool(entries) or bool(getattr(session, "eds_path", None))
        empty.setVisible(not has_eds)
        work.setVisible(has_eds)
        groups = {}
        order = (
            "Communication profile",
            "PDO",
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
        for entry in entries:
            blob = "%s %s" % (entry.display_index(), entry.name)
            if query and query not in blob.lower():
                continue
            # Split PDO indexes into their own group (CANeds-friendly)
            if (0x1400 <= entry.index <= 0x1BFF):
                gname = "PDO"
            else:
                gname = index_group(entry.index)
            group = groups.get(gname) or groups["Other"]
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
        _ui.fit_columns(tree, stretch=1)

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
        param_edit.setText(entry.parameter_value or "")
        pdo.setCurrentText(
            "Yes" if str(entry.pdo_mapping).strip() in ("1", "yes", "Yes") else "No")
        low_edit.setText(entry.low_limit or entry.extra.get("LowLimit", ""))
        high_edit.setText(entry.high_limit or entry.extra.get("HighLimit", ""))
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
                if hasattr(session, "set_focus"):
                    session.set_focus(entry.index, entry.subindex)
                return

    def _jump_pdo_map():
        _flush_object()
        idx, sub = idx_spin.value(), sub_spin.value()
        if hasattr(session, "set_focus"):
            session.set_focus(idx, sub)
        if hasattr(parent, "goto_page"):
            parent.goto_page("eds_pdo")
            pdo_page = parent._pages.get("eds_pdo")
            if pdo_page is not None and hasattr(pdo_page, "focus_object"):
                pdo_page.focus_object(idx, sub)
        plugin_shell.set_status(
            parent, "PDO map · 0x%04X:%02X" % (idx, sub), 2500)

    def _write_current():
        key = selected["key"]
        low = low_edit.text().strip()
        high = high_edit.text().strip()
        entry = OdEntry(
            index=idx_spin.value(),
            subindex=sub_spin.value(),
            name=name_edit.text().strip() or ("Object 0x%04X" % idx_spin.value()),
            object_type=_code(OBJECT_TYPES, obj_type.currentText()),
            data_type=_code(DATA_TYPES, data_type.currentText()),
            access_type=access.currentText(),
            default_value=default_edit.text().strip(),
            parameter_value=param_edit.text().strip(),
            pdo_mapping="1" if pdo.currentText() == "Yes" else "0",
            low_limit=low,
            high_limit=high,
        )
        replaced = False
        rows = _entries()
        if key is not None:
            for i, old in enumerate(rows):
                if old.key == key:
                    entry.extra = dict(old.extra)
                    if low:
                        entry.extra["LowLimit"] = low
                    else:
                        entry.extra.pop("LowLimit", None)
                    if high:
                        entry.extra["HighLimit"] = high
                    else:
                        entry.extra.pop("HighLimit", None)
                    rows[i] = entry
                    replaced = True
                    break
        if not replaced:
            if low:
                entry.extra["LowLimit"] = low
            if high:
                entry.extra["HighLimit"] = high
            for i, old in enumerate(rows):
                if old.key == entry.key:
                    rows[i] = entry
                    replaced = True
                    break
        if not replaced:
            rows.append(entry)
        rows.sort(key=lambda e: (e.index, e.subindex))
        session.draft_entries = rows
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
        if hasattr(session, "refresh_dirty"):
            session.refresh_dirty()
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
        if hasattr(session, "refresh_dirty"):
            session.refresh_dirty()
        if hasattr(parent, "_sync_tab_titles"):
            parent._sync_tab_titles()
        if hasattr(parent, "dirty_label") and hasattr(session, "eds_dirty"):
            parent.dirty_label.setText("●" if session.eds_dirty else "")
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
        if hasattr(session, "refresh_dirty"):
            session.refresh_dirty()
        _rebuild()
        log_fn("RX", "-", b"", "Removed 0x%04X:%02X" % key)

    def _duplicate():
        key = selected["key"]
        if key is None:
            return
        src = next((e for e in _entries() if e.key == key), None)
        if src is None:
            return
        import copy as _copy
        clone = _copy.deepcopy(src)
        if src.subindex == 0:
            clone.index = _free_index()
            clone.name = (src.name or "Object") + " (copy)"
        else:
            used = {e.subindex for e in _entries() if e.index == src.index}
            sub = 1
            while sub in used and sub < 255:
                sub += 1
            clone.subindex = sub
            clone.name = (src.name or "Sub") + " (copy)"
        session.draft_entries = list(_entries()) + [clone]
        session.draft_entries.sort(key=lambda e: (e.index, e.subindex))
        selected["key"] = clone.key
        if hasattr(session, "refresh_dirty"):
            session.refresh_dirty()
        _rebuild()
        _select_key(clone.key)
        log_fn("RX", "-", b"", "Duplicated %s" % clone.display_index())

    def _import_profile():
        from PyQt6.QtWidgets import QDialog, QDialogButtonBox, QListWidget, QListWidgetItem
        from _shared.canopen_profiles import (
            PROFILE_CATALOG, missing_entries, objects_for,
        )
        dlg = QDialog(parent)
        dlg.setWindowTitle("Import Profile")
        dlg.setMinimumSize(480, 400)
        lay = QVBoxLayout(dlg)
        lay.addWidget(QLabel("Select a CiA profile or pack to merge into the draft."))
        mode = QComboBox()
        mode.addItems(["Missing only", "Overwrite existing", "Skip existing"])
        mode.setFixedHeight(_ui.CTRL_H)
        lay.addWidget(mode)
        lst = QListWidget()
        for pid, title, kind, _cat, blurb in PROFILE_CATALOG:
            item = QListWidgetItem(title)
            item.setData(Qt.ItemDataRole.UserRole, pid)
            item.setToolTip(blurb)
            lst.addItem(item)
        lay.addWidget(lst, 1)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel)
        buttons.button(QDialogButtonBox.StandardButton.Ok).setText("Import")
        lay.addWidget(buttons)
        buttons.rejected.connect(dlg.reject)

        def do_import():
            item = lst.currentItem()
            if not item:
                return
            pid = item.data(Qt.ItemDataRole.UserRole)
            mi = mode.currentIndex()
            if mi == 0:
                entries = missing_entries(pid, session.draft_entries)
                ow = False
            else:
                entries = objects_for(pid)
                ow = mi == 1
            stats = session.set_draft_from_library(
                entries, merge=True, overwrite=ow)
            log_fn(
                "RX", "-", b"",
                "Imported %s +%d · ~%d · skip %d"
                % (pid, stats.get("added", 0), stats.get("updated", 0),
                   stats.get("skipped", 0)))
            dlg.accept()
            _rebuild()

        buttons.accepted.connect(do_import)
        lst.itemDoubleClicked.connect(lambda *_: do_import())
        dlg.exec()

    def _context_menu(pos):
        from PyQt6.QtWidgets import QMenu
        menu = QMenu(tree)
        item = tree.itemAt(pos)
        key = None
        if item is not None:
            data = item.data(0, Qt.ItemDataRole.UserRole)
            if isinstance(data, tuple) and len(data) >= 2:
                key = (int(data[0]), int(data[1]))
        if item is None:
            menu.addAction("Add object", _add_object)
            interop.add_bridge_actions(menu, parent, include_blank=True)
        else:
            menu.addAction("Add object", _add_object)
            menu.addAction("Add sub-index", _add_sub)
            menu.addAction("Duplicate", _duplicate)
            menu.addAction("Remove", _remove)
            menu.addSeparator()
            menu.addAction("Import profile…", _import_profile)
            menu.addAction("Browse Profiles panel",
                           lambda: interop.shell_action(parent, "profile.browse"))
            if key:
                menu.addAction(
                    "Read on Device (SDO)",
                    lambda: interop.shell_action(
                        parent, "device.focus_read",
                        index=key[0], subindex=key[1]))
                menu.addAction("Jump to Live OD…", _jump_sdo)
            menu.addSeparator()
            menu.addAction(
                "Apply EDS → OD",
                lambda: interop.shell_action(parent, "eds.apply_od"))
            menu.addSeparator()
            menu.addAction("Open in EDS Studio…", _open_studio)
            menu.addAction("Attach to AI…", _attach_ai)
        menu.exec(tree.viewport().mapToGlobal(pos))

    def _on_profile_drop(pid: str):
        interop.shell_action(parent, "profile.insert", profile_id=pid)
        _rebuild()

    def _on_node_drop(nid: int):
        interop.shell_action(parent, "network.use_node", node_id=nid)

    tree.set_profile_drop_handler(_on_profile_drop)
    tree.set_node_drop_handler(_on_node_drop)

    def _text():
        _store_meta_from_form()
        base = os.path.basename(session.eds_path) if session.eds_path else "export.eds"
        return export_eds_text(
            session.draft_entries,
            file_name=base,
            file_info=session.eds_file_info,
            device_info=session.eds_device_info,
            other_meta=getattr(session, "eds_other_meta", None),
            device_commissioning=getattr(
                session, "eds_device_commissioning", None) or None,
        )

    def _write(path):
        _store_meta_from_form()
        if hasattr(session, "save_eds") and session.save_eds(path):
            plugin_shell.set_status(parent, "Saved %s" % path, 4000)
            if hasattr(parent, "_refresh_doc_chrome"):
                parent._refresh_doc_chrome()
            return
        try:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(_text())
        except OSError as exc:
            QMessageBox.warning(parent, "Save failed", str(exc))
            return
        session.eds_path = path
        if hasattr(session, "_mark_clean"):
            session._mark_clean()
        log_fn("RX", "-", b"", "Saved EDS %s" % path)
        plugin_shell.set_status(parent, "Saved %s" % path, 4000)
        if hasattr(parent, "_refresh_doc_chrome"):
            parent._refresh_doc_chrome()

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
        if hasattr(parent, "run_action"):
            parent.run_action("eds.apply_od")
            return
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

    def _jump_sdo():
        _flush_object()
        idx, sub = idx_spin.value(), sub_spin.value()
        try:
            if hasattr(session, "set_focus"):
                session.set_focus(idx, sub)
            parent.goto_page("od")
            od = parent._pages.get("od")
            if od is not None and hasattr(od, "focus_object"):
                od.focus_object(idx, sub)
            plugin_shell.set_status(
                parent, "OD 0x%04X:%02X" % (idx, sub), 2500)
        except Exception as exc:
            QMessageBox.information(
                parent, "SDO", "Could not open OD page: %s" % exc)

    def _check():
        _store_meta_from_form()
        _flush_object()
        grid.setRowCount(0)
        profile = profile_box.currentText()
        profile_id = None
        if profile.startswith("Auto"):
            try:
                from _shared.canopen_profiles import detect_device_profile
                profile_id = detect_device_profile(session.draft_entries)
            except Exception:
                profile_id = None
        elif profile not in ("", "(none)"):
            profile_id = profile
        doc = session.to_document(use_draft=True) if hasattr(
            session, "to_document") else EdsDocument(
                entries=list(session.draft_entries),
                file_info=dict(session.eds_file_info or {}),
                device_info=dict(session.eds_device_info or {}),
                device_commissioning=dict(
                    getattr(session, "eds_device_commissioning", {}) or {}),
                path=session.eds_path or "",
            )
        findings = validate_document(doc, deep=True, profile_id=profile_id)
        errors = 0
        for finding in findings:
            row = grid.rowCount()
            grid.insertRow(row)
            level = finding.get("level", "info")
            if str(level).lower() == "error":
                errors += 1
            for col, text in enumerate((
                level, finding.get("index", ""), finding.get("message", ""),
            )):
                grid.setItem(row, col, QTableWidgetItem(str(text)))
        _ui.fit_columns(grid, stretch=2)
        mismatches = getattr(session, "debug_mismatch_count", 0)
        if errors == 0:
            if hasattr(session, "mark_validated"):
                session.mark_validated(True)
            ready_lbl.setText(
                "OK · node %d · %s · live≠EDS %d"
                % (session.node_id,
                   os.path.basename(session.eds_path) or "(unsaved)",
                   mismatches))
            next_host.setVisible(True)
        else:
            if hasattr(session, "mark_validated"):
                session.mark_validated(False)
            ready_lbl.setText("%d error(s) — fix, then Check again" % errors)
            next_host.setVisible(False)
        log_fn("SYS", "-", b"", "EDS check (%d findings)" % grid.rowCount())
        if hasattr(parent, "_refresh_doc_chrome"):
            try:
                parent._refresh_doc_chrome()
            except Exception:
                pass

    def _on_finding_activated(row, _col):
        item = grid.item(row, 1)
        if item is None:
            return
        text = (item.text() or "").strip()
        if not text:
            return
        # formats: 0x1000 or 0x1000:01
        try:
            if ":" in text:
                a, b = text.split(":", 1)
                idx = int(a, 16) if a.lower().startswith("0x") else int(a, 0)
                sub = int(b, 16) if b.lower().startswith("0x") else int(b, 0)
            else:
                idx = int(text, 16) if text.lower().startswith("0x") else int(text, 0)
                sub = 0
        except ValueError:
            return
        select_view("eds_dict")
        _select_key((idx, sub))
        for e in _entries():
            if e.key == (idx, sub):
                _fill(e)
                break

    def _on_tab(index):
        stack.setCurrentIndex(index)
        if last_tab["i"] == 1 and index != 1:
            _store_meta_from_form()
        if index == 1:
            _load_meta_form()
        if index == 2:
            _check()
        last_tab["i"] = index

    def select_eds_view(index: int):
        if 0 <= index < stack.count():
            _on_tab(index)

    def select_view(key: str):
        idx = {
            "eds_dict": 0, "eds": 0, "dictionary": 0,
            "eds_device": 1, "device": 1,
            "eds_check": 2, "check": 2,
        }.get(key, 0)
        select_eds_view(idx)

    def focus_object(index: int, subindex: int = 0):
        if hasattr(session, "set_focus"):
            session.set_focus(index, subindex)
        select_view("eds_dict")
        _select_key((index, subindex))
        idx_spin.setValue(index)
        sub_spin.setValue(subindex)

    tree.itemSelectionChanged.connect(_on_select)
    tree.customContextMenuRequested.connect(_context_menu)
    add_obj.clicked.connect(_add_object)
    add_sub.clicked.connect(_add_sub)
    remove_btn.clicked.connect(_remove)
    apply_od.clicked.connect(_apply_od)
    map_btn.clicked.connect(_jump_pdo_map)
    device_btn.clicked.connect(lambda: select_view("eds_device"))
    empty_new.clicked.connect(
        lambda: parent.run_action("eds.new")
        if hasattr(parent, "run_action") else None)
    empty_open.clicked.connect(
        lambda: parent.run_action("eds.open")
        if hasattr(parent, "run_action") else None)
    empty_profile.clicked.connect(
        lambda: interop.shell_action(parent, "profile.browse"))
    empty_pdo.clicked.connect(
        lambda: parent.goto_page("eds_pdo")
        if hasattr(parent, "goto_page") else None)
    live_btn.clicked.connect(_jump_sdo)
    run_check.clicked.connect(_check)
    save_next.clicked.connect(_save)
    apply_next.clicked.connect(_apply_od)
    codegen_next.clicked.connect(
        lambda: parent.goto_page("eds_codegen")
        if hasattr(parent, "goto_page") else None)
    grid.cellDoubleClicked.connect(_on_finding_activated)
    name_edit.editingFinished.connect(_apply_fields)
    default_edit.editingFinished.connect(_apply_fields)
    param_edit.editingFinished.connect(_apply_fields)
    low_edit.editingFinished.connect(_apply_fields)
    high_edit.editingFinished.connect(_apply_fields)
    idx_spin.editingFinished.connect(_apply_fields)
    sub_spin.editingFinished.connect(_apply_fields)
    for box in (obj_type, data_type, access, pdo):
        box.currentIndexChanged.connect(lambda _i: _apply_fields())

    def _refresh(_=None):
        _rebuild()
        _load_meta_form()
        fi = getattr(session, "focus_index", 0) or 0
        if fi and stack.currentIndex() == 0:
            _select_key((fi, getattr(session, "focus_subindex", 0) or 0))
            idx_spin.setValue(fi)
            sub_spin.setValue(getattr(session, "focus_subindex", 0) or 0)

    session.on_od_changed(_refresh)
    _refresh()
    stack.select_eds_view = select_eds_view  # type: ignore[attr-defined]
    stack.select_view = select_view  # type: ignore[attr-defined]
    stack.save_document = _save  # type: ignore[attr-defined]
    stack.focus_object = focus_object  # type: ignore[attr-defined]
    return stack
