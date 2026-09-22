# -*- coding: utf-8 -*-
"""Attributes workspace — BA_DEF_ / BA_DEF_DEF_ / BA_ (CANdb++ style)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QInputDialog,
    QLabel,
    QLineEdit,
    QMenu,
    QMessageBox,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, dbcparse, plugin_shell


# Vector-style Gen* / network presets — one click past CANdb++ default dialogs.
_PRESETS = (
    ("GenMsgCycleTime", "BO_", "INT", 0, 65535, 0, None),
    ("GenMsgSendType", "BO_", "ENUM", None, None, "Cyclic",
     ["Cyclic", "Event", "NotUsed", "IfActive"]),
    ("GenMsgDelayTime", "BO_", "INT", 0, 65535, 0, None),
    ("GenMsgStartDelayTime", "BO_", "INT", 0, 65535, 0, None),
    ("GenMsgNrOfRepetition", "BO_", "INT", 0, 999999, 0, None),
    ("GenSigStartValue", "SG_", "INT", 0, 65535, 0, None),
    ("GenSigSendType", "SG_", "ENUM", None, None, "Cyclic",
     ["Cyclic", "OnChange", "OnWrite", "IfActive", "OnChangeWithRepetition",
      "OnWriteWithRepetition", "NoSigSendType"]),
    ("BusType", "", "STRING", None, None, "CAN", None),
    ("DBName", "", "STRING", None, None, "", None),
    ("ProtocolType", "", "STRING", None, None, "StandardCAN", None),
)

# Common J1939 / SAE attribute names used with DBC overlays.
_J1939_PRESETS = (
    ("VFrameFormat", "BO_", "ENUM", None, None, "J1939PG",
     ["StandardCAN", "ExtendedCAN", "reserved", "J1939PG"]),
    ("GenMsgILSupport", "BO_", "ENUM", None, None, "Yes", ["No", "Yes"]),
    ("GenSigInactiveValue", "SG_", "INT", 0, 65535, 0, None),
    ("SPN", "SG_", "INT", 0, 524287, 0, None),
    ("NmStationAddress", "BU_", "INT", 0, 253, 0, None),
    ("NmJ1939AAC", "BU_", "INT", 0, 1, 0, None),
    ("NmJ1939IndustryGroup", "BU_", "INT", 0, 7, 0, None),
    ("NmJ1939System", "BU_", "INT", 0, 127, 0, None),
    ("NmJ1939SystemInstance", "BU_", "INT", 0, 15, 0, None),
    ("NmJ1939Function", "BU_", "INT", 0, 255, 0, None),
    ("NmJ1939FunctionInstance", "BU_", "INT", 0, 7, 0, None),
    ("NmJ1939ECUInstance", "BU_", "INT", 0, 3, 0, None),
    ("NmJ1939ManufacturerCode", "BU_", "INT", 0, 2047, 0, None),
    ("NmJ1939IdentityNumber", "BU_", "INT", 0, 2097151, 0, None),
)


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome = QWidget()
    chrome.setObjectName("SuiteToolbar")
    crow = QHBoxLayout(chrome)
    crow.setContentsMargins(12, 6, 12, 6)
    crow.setSpacing(8)

    scope = QComboBox()
    scope.addItems(["Message", "Signal", "Node", "Network"])
    scope.setFixedHeight(28)
    scope.setMinimumWidth(110)
    scope.setToolTip("Object scope for value assignment")
    crow.addWidget(scope)

    target = QComboBox()
    target.setFixedHeight(28)
    target.setMinimumWidth(200)
    target.setToolTip("Selected message, signal, node, or network")
    crow.addWidget(target, 1)

    add_def = QPushButton("Add definition")
    add_def.setFixedHeight(28)
    codicons.set_button(add_def, "add", primary=True)
    crow.addWidget(add_def)

    presets_btn = QPushButton("Presets")
    presets_btn.setObjectName("GhostButton")
    presets_btn.setFixedHeight(28)
    presets_btn.setToolTip(
        "Add common Vector Gen* / network attribute definitions in one click")
    codicons.set_button(presets_btn, "add")
    crow.addWidget(presets_btn)

    del_def = QPushButton("Delete def")
    del_def.setObjectName("GhostButton")
    del_def.setFixedHeight(28)
    codicons.set_button(del_def, "delete")
    crow.addWidget(del_def)
    layout.addWidget(chrome)

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    bl.setContentsMargins(16, 12, 16, 12)
    bl.setSpacing(10)

    hint = QLabel(
        "Attribute definitions and values — same job as CANdb++ View|Attribute "
        "Definitions, with live scope filters and defaults applied on save.")
    hint.setWordWrap(True)
    hint.setStyleSheet("color:#78909c;font-size:12px;")
    bl.addWidget(hint)

    split = QSplitter(Qt.Orientation.Horizontal)

    left = QWidget()
    left_l = QVBoxLayout(left)
    left_l.setContentsMargins(0, 0, 0, 0)
    left_l.setSpacing(6)
    left_head = QLabel("Definitions")
    left_head.setObjectName("SuiteSectionTitle")
    left_l.addWidget(left_head)
    defs_table = QTableWidget(0, 5)
    defs_table.setObjectName("SuiteMatrix")
    defs_table.setHorizontalHeaderLabels(
        ["Name", "Object", "Type", "Range / Enum", "Default"])
    defs_table.setSelectionBehavior(
        QAbstractItemView.SelectionBehavior.SelectRows)
    defs_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    defs_table.setAlternatingRowColors(True)
    defs_table.verticalHeader().setVisible(False)
    defs_table.verticalHeader().setDefaultSectionSize(28)
    defs_table.horizontalHeader().setSectionResizeMode(
        0, QHeaderView.ResizeMode.Stretch)
    defs_table.setStyleSheet(
        "QTableWidget#SuiteMatrix { gridline-color: #EEEEEE; }"
        "QTableWidget#SuiteMatrix::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
    )
    left_l.addWidget(defs_table, 1)
    def_status = QLabel("")
    def_status.setStyleSheet("color:#90A4AE;font-size:11px;")
    left_l.addWidget(def_status)
    split.addWidget(left)

    right = QWidget()
    right_l = QVBoxLayout(right)
    right_l.setContentsMargins(8, 0, 0, 0)
    right_l.setSpacing(6)
    right_head = QLabel("Values on selection")
    right_head.setObjectName("SuiteSectionTitle")
    right_l.addWidget(right_head)

    vals_table = QTableWidget(0, 2)
    vals_table.setObjectName("SuiteMatrix")
    vals_table.setHorizontalHeaderLabels(["Attribute", "Value"])
    vals_table.horizontalHeader().setSectionResizeMode(
        1, QHeaderView.ResizeMode.Stretch)
    vals_table.setSelectionBehavior(
        QAbstractItemView.SelectionBehavior.SelectRows)
    vals_table.setAlternatingRowColors(True)
    vals_table.verticalHeader().setVisible(False)
    vals_table.verticalHeader().setDefaultSectionSize(28)
    vals_table.setStyleSheet(
        "QTableWidget#SuiteMatrix { gridline-color: #EEEEEE; }"
        "QTableWidget#SuiteMatrix::item:selected {"
        " background: #E3F2FD; color: #0D47A1; }"
    )
    right_l.addWidget(vals_table, 1)

    edit_row = QHBoxLayout()
    edit_row.setSpacing(8)
    edit = QLineEdit()
    edit.setPlaceholderText("New value (ENUM name or number)")
    edit.setFixedHeight(28)
    edit_row.addWidget(edit, 1)
    apply_val = QPushButton("Apply")
    apply_val.setFixedHeight(28)
    codicons.set_button(apply_val, "apply", primary=True)
    clear_val = QPushButton("Clear")
    clear_val.setObjectName("GhostButton")
    clear_val.setFixedHeight(28)
    codicons.set_button(clear_val, "clear")
    edit_row.addWidget(apply_val)
    edit_row.addWidget(clear_val)
    right_l.addLayout(edit_row)

    val_status = QLabel("")
    val_status.setStyleSheet("color:#90A4AE;font-size:11px;")
    right_l.addWidget(val_status)
    split.addWidget(right)
    split.setSizes([540, 420])
    bl.addWidget(split, 1)
    layout.addWidget(body, 1)

    def _refresh_defs():
        document.db.ensure_default_attr_defs()
        defs_table.setRowCount(0)
        for d in document.db.attribute_defs:
            r = defs_table.rowCount()
            defs_table.insertRow(r)
            rng = ""
            if d.value_type == "ENUM":
                rng = ", ".join(d.enum_values or [])
            elif d.minimum is not None or d.maximum is not None:
                rng = "%s … %s" % (d.minimum, d.maximum)
            for c, text in enumerate((
                d.name, d.object_type or "(network)", d.value_type, rng,
                "" if d.default is None else str(d.default),
            )):
                defs_table.setItem(r, c, QTableWidgetItem(text))
        def_status.setText("%d definitions" % defs_table.rowCount())

    def _fill_targets():
        target.blockSignals(True)
        target.clear()
        kind = scope.currentText()
        if kind == "Message":
            for cid, m in document.db.messages.items():
                target.addItem("%s (0x%X)" % (m.name, cid), ("BO_", cid))
        elif kind == "Signal":
            for cid, m in document.db.messages.items():
                for s in m.signals:
                    target.addItem(
                        "%s / %s" % (m.name, s.name), ("SG_", cid, s.name))
        elif kind == "Node":
            for n in document.db.nodes:
                if n and n != "Vector__XXX":
                    target.addItem(n, ("BU_", n))
        else:
            target.addItem("(network)", ("NET",))
        target.blockSignals(False)

    def _current_attrs():
        data = target.currentData()
        if not data:
            return {}
        if data[0] == "BO_":
            m = document.db.messages.get(data[1])
            return dict(m.attributes or {}) if m else {}
        if data[0] == "SG_":
            m = document.db.messages.get(data[1])
            s = m.signal(data[2]) if m else None
            return dict(s.attributes or {}) if s else {}
        if data[0] == "BU_":
            return dict(document.db.node_attributes.get(data[1]) or {})
        return dict(document.db.network_attributes or {})

    def _refresh_vals():
        vals_table.setRowCount(0)
        attrs = _current_attrs()
        data = target.currentData()
        obj = ""
        if data:
            obj = {"BO_": "BO_", "SG_": "SG_", "BU_": "BU_", "NET": ""}.get(
                data[0], "")
        names = []
        for d in document.db.attribute_defs:
            if not d.object_type or d.object_type == obj:
                names.append(d.name)
        set_count = 0
        for name in sorted(set(list(attrs.keys()) + names)):
            r = vals_table.rowCount()
            vals_table.insertRow(r)
            vals_table.setItem(r, 0, QTableWidgetItem(name))
            has = name in attrs
            if has:
                set_count += 1
            vals_table.setItem(
                r, 1, QTableWidgetItem("" if not has else str(attrs[name])))
        val_status.setText(
            "%d attributes · %d set" % (vals_table.rowCount(), set_count))

    def _on_add_def():
        name, ok = QInputDialog.getText(shell, "Attribute", "Name:")
        if not ok or not name.strip():
            return
        name = name.strip()
        if document.db.attr_def(name):
            QMessageBox.warning(shell, "Attribute", "Already exists: %s" % name)
            return
        obj, ok = QInputDialog.getItem(
            shell, "Object type", "Applies to:",
            ["BO_", "SG_", "BU_", "(network)"], 0, False)
        if not ok:
            return
        vtype, ok = QInputDialog.getItem(
            shell, "Type", "Value type:",
            ["INT", "FLOAT", "STRING", "ENUM", "HEX"], 0, False)
        if not ok:
            return
        d = dbcparse.AttrDef(
            name, "" if obj == "(network)" else obj, vtype)
        if vtype == "ENUM":
            raw, ok = QInputDialog.getText(
                shell, "ENUM", "Comma-separated values:")
            if ok and raw.strip():
                d.enum_values = [x.strip() for x in raw.split(",") if x.strip()]
                if d.enum_values:
                    d.default = d.enum_values[0]
        elif vtype in ("INT", "HEX", "FLOAT"):
            d.minimum, d.maximum = 0, 65535 if vtype != "FLOAT" else 1.0e9
            d.default = 0
        document.db.attribute_defs.append(d)
        document.mark_dirty(True)
        log_fn("Attributes", "Added definition %s" % name)
        _refresh_defs()
        _refresh_vals()
        plugin_shell.set_status(shell, "Added attribute %s" % name, 2500)

    def _add_preset(spec):
        name, obj, vtype, lo, hi, default, enums = spec
        if document.db.attr_def(name):
            plugin_shell.set_status(
                shell, "Already present: %s" % name, 2500)
            return
        d = dbcparse.AttrDef(name, obj, vtype)
        if enums:
            d.enum_values = list(enums)
        if lo is not None:
            d.minimum = lo
        if hi is not None:
            d.maximum = hi
        if default is not None and default != "":
            d.default = default
        document.db.attribute_defs.append(d)
        document.mark_dirty(True)
        log_fn("Attributes", "Preset %s" % name)
        _refresh_defs()
        _refresh_vals()
        plugin_shell.set_status(shell, "Added preset %s" % name, 2500)

    def _on_presets():
        menu = QMenu(shell)
        menu.addAction("Add all Gen* + network…", _add_all_presets)
        menu.addAction("Add all J1939 pack…", _add_all_j1939)
        menu.addSeparator()
        gen_m = menu.addMenu("Gen* / network")
        for spec in _PRESETS:
            name = spec[0]
            act = gen_m.addAction(name)
            act.setEnabled(document.db.attr_def(name) is None)
            act.triggered.connect(
                lambda _checked=False, s=spec: _add_preset(s))
        j_m = menu.addMenu("J1939")
        for spec in _J1939_PRESETS:
            name = spec[0]
            act = j_m.addAction(name)
            act.setEnabled(document.db.attr_def(name) is None)
            act.triggered.connect(
                lambda _checked=False, s=spec: _add_preset(s))
        menu.exec(presets_btn.mapToGlobal(presets_btn.rect().bottomLeft()))

    def _add_all_presets():
        _add_preset_batch(_PRESETS)

    def _add_all_j1939():
        _add_preset_batch(_J1939_PRESETS)

    def _add_preset_batch(specs):
        added = []
        for spec in specs:
            name = spec[0]
            if document.db.attr_def(name) is not None:
                continue
            name, obj, vtype, lo, hi, default, enums = spec
            d = dbcparse.AttrDef(name, obj, vtype)
            if enums:
                d.enum_values = list(enums)
            if lo is not None:
                d.minimum = lo
            if hi is not None:
                d.maximum = hi
            if default is not None and default != "":
                d.default = default
            document.db.attribute_defs.append(d)
            added.append(name)
        if not added:
            plugin_shell.set_status(shell, "All presets already present", 2500)
            return
        document.mark_dirty(True)
        log_fn("Attributes", "Presets: %s" % ", ".join(added))
        _refresh_defs()
        _refresh_vals()
        plugin_shell.set_status(
            shell, "Added %d preset definitions" % len(added), 3000)

    def _on_del_def():
        row = defs_table.currentRow()
        if row < 0:
            return
        name = defs_table.item(row, 0).text()
        if name in ("GenMsgCycleTime", "GenMsgSendType"):
            QMessageBox.information(
                shell, "Attribute",
                "Built-in Gen* attributes are kept for CANdb++ compatibility.")
            return
        document.db.attribute_defs = [
            d for d in document.db.attribute_defs if d.name != name]
        document.mark_dirty(True)
        log_fn("Attributes", "Deleted definition %s" % name)
        _refresh_defs()
        _refresh_vals()

    def _on_apply():
        data = target.currentData()
        row = vals_table.currentRow()
        if not data or row < 0:
            return
        name = vals_table.item(row, 0).text()
        val = edit.text().strip()
        if not val:
            val = vals_table.item(row, 1).text()
        if not val:
            return
        d = document.db.attr_def(name)
        if d and d.value_type == "ENUM" and d.enum_values and val not in d.enum_values:
            try:
                idx = int(val)
                if 0 <= idx < len(d.enum_values):
                    val = d.enum_values[idx]
            except ValueError:
                pass
        if data[0] == "BO_":
            m = document.db.messages.get(data[1])
            if not m:
                return
            m.attributes[name] = val
            if name == "GenMsgCycleTime":
                try:
                    m.cycle_time = int(float(val))
                except ValueError:
                    pass
        elif data[0] == "SG_":
            m = document.db.messages.get(data[1])
            s = m.signal(data[2]) if m else None
            if not s:
                return
            s.attributes[name] = val
        elif data[0] == "BU_":
            document.db.node_attributes.setdefault(data[1], {})[name] = val
        else:
            document.db.network_attributes[name] = val
        document.mark_dirty(True)
        log_fn("Attributes", "Set %s = %s" % (name, val))
        edit.clear()
        _refresh_vals()
        plugin_shell.set_status(shell, "Set %s = %s" % (name, val), 2500)

    def _on_clear():
        data = target.currentData()
        row = vals_table.currentRow()
        if not data or row < 0:
            return
        name = vals_table.item(row, 0).text()
        if data[0] == "BO_":
            m = document.db.messages.get(data[1])
            if m and name in (m.attributes or {}):
                del m.attributes[name]
        elif data[0] == "SG_":
            m = document.db.messages.get(data[1])
            s = m.signal(data[2]) if m else None
            if s and name in (s.attributes or {}):
                del s.attributes[name]
        elif data[0] == "BU_":
            attrs = document.db.node_attributes.get(data[1]) or {}
            attrs.pop(name, None)
        else:
            document.db.network_attributes.pop(name, None)
        document.mark_dirty(True)
        _refresh_vals()

    def _on_val_selected():
        row = vals_table.currentRow()
        if row < 0:
            return
        cur = vals_table.item(row, 1)
        if cur and cur.text():
            edit.setText(cur.text())

    add_def.clicked.connect(_on_add_def)
    presets_btn.clicked.connect(_on_presets)
    del_def.clicked.connect(_on_del_def)
    apply_val.clicked.connect(_on_apply)
    clear_val.clicked.connect(_on_clear)
    edit.returnPressed.connect(_on_apply)
    vals_table.itemSelectionChanged.connect(_on_val_selected)
    scope.currentIndexChanged.connect(
        lambda _i: (_fill_targets(), _refresh_vals()))
    target.currentIndexChanged.connect(lambda _i: _refresh_vals())
    document.on_changed(
        lambda: (_refresh_defs(), _fill_targets(), _refresh_vals()))

    _refresh_defs()
    _fill_targets()
    _refresh_vals()
    return root
