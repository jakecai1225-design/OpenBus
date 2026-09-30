# -*- coding: utf-8 -*-
"""Attributes workspace — BA_DEF_ / BA_DEF_DEF_ / BA_ (CANdb++ style).

Linked to Messages focus: selecting a message/signal there drives the
scope/target here. Context menus jump back to Messages.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QBrush
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QInputDialog,
    QLabel,
    QLineEdit,
    QMenu,
    QMessageBox,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pages import _ui
from _shared import dbcparse, plugin_shell, vscode_theme


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

_SCOPE_OBJ = {
    "Message": "BO_",
    "Signal": "SG_",
    "Node": "BU_",
    "Network": "",
}


def signal_attribute_view(db, can_id, sig_name):
    """Catalog rows for Attributes Values when a signal is selected.

    Returns list of (name, value, source, write_kind) where write_kind is
    SG_ (signal BA_) or BO_ (parent message BA_, Source=message).
    Includes GenSig* defaults so OEM DBCs with only GenMsg* still show rows.
    """
    db.ensure_default_attr_defs()
    msg = db.messages.get(can_id)
    sig = msg.signal(sig_name) if msg else None
    if not msg or not sig:
        return []
    sig_attrs = dict(sig.attributes or {})
    parent = dict(msg.attributes or {})
    if msg.cycle_time and "GenMsgCycleTime" not in parent:
        parent["GenMsgCycleTime"] = msg.cycle_time

    def _names(obj, attrs):
        names = [d.name for d in db.attribute_defs if d.object_type == obj]
        for name in attrs:
            if name not in names:
                names.append(name)
        return sorted(set(names))

    rows = []
    seen = set()
    for name in _names("SG_", sig_attrs):
        d = db.attr_def(name)
        default = "" if d is None or d.default is None else str(d.default)
        if name in sig_attrs:
            rows.append((name, str(sig_attrs[name]), "set", "SG_"))
        elif default != "":
            rows.append((name, default, "default", "SG_"))
        else:
            rows.append((name, "", "—", "SG_"))
        seen.add(name)
    for name in sorted(parent.keys()):
        if name in seen:
            continue
        rows.append((name, str(parent[name]), "message", "BO_"))
        seen.add(name)
    return rows


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    scope = QComboBox()
    scope.addItems(["Message", "Signal", "Node", "Network"])
    scope.setFixedHeight(_ui.CTRL_H)
    scope.setMinimumWidth(110)
    scope.setToolTip("Object scope for value assignment")

    target = QComboBox()
    target.setFixedHeight(_ui.CTRL_H)
    target.setMinimumWidth(200)
    target.setToolTip("Selected message, signal, node, or network")

    def_search = QLineEdit()
    def_search.setPlaceholderText("Search definitions…")
    def_search.setClearButtonEnabled(True)
    def_search.setFixedHeight(_ui.CTRL_H)
    def_search.setMinimumWidth(140)
    def_search.setToolTip("Filter definitions by name, object, or type")

    val_search = QLineEdit()
    val_search.setPlaceholderText("Search values…")
    val_search.setClearButtonEnabled(True)
    val_search.setFixedHeight(_ui.CTRL_H)
    val_search.setMinimumWidth(140)
    val_search.setToolTip("Filter attribute values by name or value")

    goto_msg = _ui.ghost_btn(
        "Messages", "Open Messages on the current target", "edit")
    add_def = _ui.primary_btn(
        "Add definition", "Add an attribute definition", "add")
    presets_btn = _ui.ghost_btn(
        "Presets",
        "Add common Vector Gen* / network attribute definitions in one click",
        "add")
    del_def = _ui.ghost_btn("Delete def", "Delete selected definition", "delete")
    layout.addWidget(_ui.tool_strip(
        scope, target, def_search, val_search, goto_msg,
        add_def, presets_btn, del_def, stretch_at=4))

    body = QWidget()
    body.setObjectName("SuiteContent")
    bl = QVBoxLayout(body)
    bl.setContentsMargins(16, 12, 16, 12)
    bl.setSpacing(10)

    split = QSplitter(Qt.Orientation.Horizontal)

    left = QWidget()
    left_l = QVBoxLayout(left)
    left_l.setContentsMargins(0, 0, 0, 0)
    left_l.setSpacing(6)
    left_head = QLabel("Definitions (all)")
    left_head.setObjectName("SuiteSectionTitle")
    left_l.addWidget(left_head)
    defs_table = QTableWidget(0, 6)
    defs_table.setObjectName("SuiteMatrix")
    defs_table.setHorizontalHeaderLabels(
        ["Name", "Object", "Type", "Range / Enum", "Default", "Used"])
    defs_table.setSelectionBehavior(
        QAbstractItemView.SelectionBehavior.SelectRows)
    defs_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    defs_table.setAlternatingRowColors(True)
    defs_table.verticalHeader().setVisible(False)
    defs_table.verticalHeader().setDefaultSectionSize(28)
    defs_table.horizontalHeader().setSectionResizeMode(
        0, QHeaderView.ResizeMode.Stretch)
    defs_table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    _ui.style_table(defs_table)
    left_l.addWidget(defs_table, 1)
    def_status = QLabel("")
    def_status.setObjectName("SuiteCount")
    left_l.addWidget(def_status)
    split.addWidget(left)

    right = QWidget()
    right_l = QVBoxLayout(right)
    right_l.setContentsMargins(8, 0, 0, 0)
    right_l.setSpacing(6)
    right_head = QLabel("Values on selection")
    right_head.setObjectName("SuiteSectionTitle")
    right_l.addWidget(right_head)

    vals_table = QTableWidget(0, 6)
    vals_table.setObjectName("SuiteMatrix")
    vals_table.setHorizontalHeaderLabels(
        ["Attribute", "Value", "Default", "Type", "Range", "Source"])
    vals_hdr = vals_table.horizontalHeader()
    vals_hdr.setMinimumSectionSize(56)
    vals_hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    vals_hdr.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    for col, width in ((2, 72), (3, 64), (4, 100), (5, 72)):
        vals_hdr.setSectionResizeMode(col, QHeaderView.ResizeMode.Interactive)
        vals_table.setColumnWidth(col, width)
    vals_table.setSelectionBehavior(
        QAbstractItemView.SelectionBehavior.SelectRows)
    vals_table.setAlternatingRowColors(True)
    vals_table.verticalHeader().setVisible(False)
    vals_table.verticalHeader().setDefaultSectionSize(28)
    vals_table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    _ui.style_table(vals_table)
    right_l.addWidget(vals_table, 1)

    edit_row = QHBoxLayout()
    edit_row.setSpacing(8)
    edit = QLineEdit()
    edit.setPlaceholderText("New value (ENUM name or number)")
    edit.setFixedHeight(_ui.CTRL_H)
    edit_row.addWidget(edit, 1)
    apply_val = _ui.primary_btn(
        "Apply", "Write value onto the selected attribute", "apply")
    use_def = _ui.ghost_btn(
        "Use default", "Copy definition default into the edit field", "sync")
    clear_val = _ui.ghost_btn(
        "Clear", "Clear attribute value on selection", "clear")
    edit_row.addWidget(apply_val)
    edit_row.addWidget(use_def)
    edit_row.addWidget(clear_val)
    right_l.addLayout(edit_row)

    val_status = QLabel("")
    val_status.setObjectName("SuiteCount")
    right_l.addWidget(val_status)
    split.addWidget(right)
    split.setSizes([520, 480])
    bl.addWidget(split, 1)
    layout.addWidget(body, 1)

    dim = QBrush(QColor(vscode_theme.TEXT_DIM))

    def _range_text(d) -> str:
        if d.value_type == "ENUM":
            return ", ".join(d.enum_values or [])
        if d.minimum is not None or d.maximum is not None:
            return "%s … %s" % (d.minimum, d.maximum)
        return ""

    def _obj_token() -> str:
        data = target.currentData()
        if data:
            return {"BO_": "BO_", "SG_": "SG_", "BU_": "BU_", "NET": ""}.get(
                data[0], "")
        return _SCOPE_OBJ.get(scope.currentText(), "")

    def _match(q: str, *parts) -> bool:
        if not q:
            return True
        blob = " ".join(str(p or "") for p in parts).lower()
        return q in blob

    def _attr_usage_count(name: str) -> int:
        n = 0
        if name in (document.db.network_attributes or {}):
            n += 1
        for attrs in (document.db.node_attributes or {}).values():
            if name in (attrs or {}):
                n += 1
        for m in document.db.messages.values():
            if name in (m.attributes or {}):
                n += 1
            elif name == "GenMsgCycleTime" and m.cycle_time:
                n += 1
            for s in m.signals:
                if name in (s.attributes or {}):
                    n += 1
        return n

    def _refresh_defs():
        document.db.ensure_default_attr_defs()
        q = (def_search.text() or "").strip().lower()
        defs_table.setRowCount(0)
        shown = 0
        for d in document.db.attribute_defs:
            rng = _range_text(d)
            default = "" if d.default is None else str(d.default)
            obj = d.object_type or "(network)"
            used = _attr_usage_count(d.name)
            if not _match(q, d.name, obj, d.value_type, rng, default, used):
                continue
            r = defs_table.rowCount()
            defs_table.insertRow(r)
            for c, text in enumerate(
                    (d.name, obj, d.value_type, rng, default, str(used))):
                defs_table.setItem(r, c, QTableWidgetItem(text))
            shown += 1
        def_status.setText(
            "%d shown · %d definitions" % (
                shown, len(document.db.attribute_defs)))

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
        _select_focused_target()

    def _select_focused_target():
        """Align Attributes scope/target with the Messages selection."""
        cid = getattr(document, "focus_can_id", None)
        sig_name = getattr(document, "focus_signal", "") or ""
        if cid is None:
            return
        want_kind = "Signal" if sig_name else "Message"
        if scope.currentText() != want_kind:
            scope.blockSignals(True)
            scope.setCurrentText(want_kind)
            scope.blockSignals(False)
            target.blockSignals(True)
            target.clear()
            if want_kind == "Message":
                for mid, m in document.db.messages.items():
                    target.addItem("%s (0x%X)" % (m.name, mid), ("BO_", mid))
            else:
                for mid, m in document.db.messages.items():
                    for s in m.signals:
                        target.addItem(
                            "%s / %s" % (m.name, s.name),
                            ("SG_", mid, s.name))
            target.blockSignals(False)
        want = (
            ("SG_", cid, sig_name) if want_kind == "Signal" and sig_name
            else ("BO_", cid))
        for i in range(target.count()):
            if target.itemData(i) == want:
                target.setCurrentIndex(i)
                return

    def _current_attrs() -> dict:
        data = target.currentData()
        if not data:
            return {}
        if data[0] == "BO_":
            m = document.db.messages.get(data[1])
            if not m:
                return {}
            attrs = dict(m.attributes or {})
            # Cycle time inspector field mirrors GenMsgCycleTime.
            if m.cycle_time and "GenMsgCycleTime" not in attrs:
                attrs["GenMsgCycleTime"] = m.cycle_time
            return attrs
        if data[0] == "SG_":
            m = document.db.messages.get(data[1])
            s = m.signal(data[2]) if m else None
            return dict(s.attributes or {}) if s else {}
        if data[0] == "BU_":
            return dict(document.db.node_attributes.get(data[1]) or {})
        return dict(document.db.network_attributes or {})

    def _names_for_object(obj: str, attrs: dict) -> list[str]:
        """Defs for this object type + any orphan BA_ values on the object."""
        names = []
        for d in document.db.attribute_defs:
            if d.object_type == obj:
                names.append(d.name)
            elif not d.object_type and obj == "" and d.name:
                names.append(d.name)
        for name in attrs:
            if name not in names:
                names.append(name)
        return sorted(set(names))

    def _row_meta(name: str, attrs: dict, source_override: str | None = None):
        d = document.db.attr_def(name)
        default = ""
        vtype = ""
        rng = ""
        if d is not None:
            default = "" if d.default is None else str(d.default)
            vtype = d.value_type or ""
            rng = _range_text(d)
        has = name in attrs
        if has:
            source = source_override or "set"
            value = str(attrs[name])
        elif default != "":
            source = source_override or "default"
            value = default
        else:
            source = source_override or "—"
            value = ""
        return name, value, default, vtype, rng, source, has

    def _value_entries():
        """Rows for Values panel: (name, value, default, type, range, source, has, write_kind)."""
        data = target.currentData()
        if not data:
            return []
        if data[0] == "SG_":
            entries = []
            for name, value, source, write_kind in signal_attribute_view(
                    document.db, data[1], data[2]):
                d = document.db.attr_def(name)
                default = ""
                vtype = ""
                rng = ""
                if d is not None:
                    default = "" if d.default is None else str(d.default)
                    vtype = d.value_type or ""
                    rng = _range_text(d)
                has = source in ("set", "message")
                entries.append(
                    (name, value, default, vtype, rng, source, has, write_kind))
            return entries
        entries = []
        obj = _obj_token()
        attrs = _current_attrs()
        write = data[0] if data[0] != "NET" else "NET"
        for name in _names_for_object(obj, attrs):
            meta = _row_meta(name, attrs)
            entries.append(meta + (write,))
        return entries

    def _refresh_vals():
        document.db.ensure_default_attr_defs()
        vals_table.setRowCount(0)
        q = (val_search.text() or "").strip().lower()
        set_count = 0
        shown = 0
        entries = _value_entries()
        for name, value, default, vtype, rng, source, has, write_kind in entries:
            if has and source in ("set", "message"):
                set_count += 1
            if not _match(q, name, value, default, vtype, rng, source):
                continue
            r = vals_table.rowCount()
            vals_table.insertRow(r)
            cells = (name, value, default, vtype, rng, source)
            for c, text in enumerate(cells):
                item = QTableWidgetItem(text)
                if source in ("default", "message") and c in (1, 2, 5):
                    item.setForeground(dim)
                if c == 0:
                    item.setData(Qt.ItemDataRole.UserRole, write_kind)
                vals_table.setItem(r, c, item)
            shown += 1
        val_status.setText(
            "%d shown · %d attributes · %d set · %d default" % (
                shown,
                len(entries),
                set_count,
                max(0, shown - set_count)))

    def _focus_can_sig():
        data = target.currentData()
        if not data:
            return None, ""
        if data[0] == "BO_":
            return data[1], ""
        if data[0] == "SG_":
            return data[1], data[2]
        return None, ""

    def _goto_messages():
        cid, sig = _focus_can_sig()
        if cid is None:
            plugin_shell.set_status(
                shell, "Select a message or signal target first", 2500)
            return
        if hasattr(shell, "goto_editor_target"):
            shell.goto_editor_target(cid, sig or None)
        else:
            document.set_focus(cid, sig)
            shell.goto_page("editor")

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
        if name in (
                "GenMsgCycleTime", "GenMsgSendType",
                "GenSigStartValue", "GenSigSendType"):
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

    def _set_attr_value(name: str, val: str, write_kind: str | None = None):
        data = target.currentData()
        if not data or not name:
            return False
        d = document.db.attr_def(name)
        if d and d.value_type == "ENUM" and d.enum_values and val not in d.enum_values:
            try:
                idx = int(val)
                if 0 <= idx < len(d.enum_values):
                    val = d.enum_values[idx]
            except ValueError:
                pass
        kind = write_kind or data[0]
        if kind == "BO_":
            cid = data[1] if data[0] in ("BO_", "SG_") else None
            if cid is None:
                return False
            m = document.db.messages.get(cid)
            if not m:
                return False
            m.attributes[name] = val
            if name == "GenMsgCycleTime":
                try:
                    m.cycle_time = int(float(val))
                except ValueError:
                    pass
        elif kind == "SG_":
            if data[0] != "SG_":
                return False
            m = document.db.messages.get(data[1])
            s = m.signal(data[2]) if m else None
            if not s:
                return False
            s.attributes[name] = val
        elif kind == "BU_":
            if data[0] != "BU_":
                return False
            document.db.node_attributes.setdefault(data[1], {})[name] = val
        else:
            document.db.network_attributes[name] = val
        document.mark_dirty(True)
        return True

    def _on_apply():
        row = vals_table.currentRow()
        if row < 0:
            return
        name_item = vals_table.item(row, 0)
        name = name_item.text()
        write_kind = name_item.data(Qt.ItemDataRole.UserRole)
        val = edit.text().strip()
        if not val:
            val = vals_table.item(row, 1).text()
        if not val:
            return
        if not _set_attr_value(name, val, write_kind):
            return
        log_fn("Attributes", "Set %s = %s" % (name, val))
        edit.clear()
        _refresh_vals()
        plugin_shell.set_status(shell, "Set %s = %s" % (name, val), 2500)

    def _on_use_default():
        row = vals_table.currentRow()
        if row < 0:
            return
        default = vals_table.item(row, 2)
        if default and default.text():
            edit.setText(default.text())
            _on_apply()

    def _on_clear():
        data = target.currentData()
        row = vals_table.currentRow()
        if not data or row < 0:
            return
        name_item = vals_table.item(row, 0)
        name = name_item.text()
        write_kind = name_item.data(Qt.ItemDataRole.UserRole) or data[0]
        if write_kind == "BO_":
            cid = data[1] if data[0] in ("BO_", "SG_") else None
            m = document.db.messages.get(cid) if cid is not None else None
            if m and name in (m.attributes or {}):
                del m.attributes[name]
        elif write_kind == "SG_":
            if data[0] != "SG_":
                return
            m = document.db.messages.get(data[1])
            s = m.signal(data[2]) if m else None
            if s and name in (s.attributes or {}):
                del s.attributes[name]
        elif write_kind == "BU_":
            if data[0] == "BU_":
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
        source = vals_table.item(row, 5)
        cur = vals_table.item(row, 1)
        default = vals_table.item(row, 2)
        if source and source.text() in ("set", "message") and cur and cur.text():
            edit.setText(cur.text())
            return
        if default and default.text():
            edit.setText(default.text())
        elif cur and cur.text():
            edit.setText(cur.text())

    def _copy_text(text: str):
        if not text:
            return
        QApplication.clipboard().setText(text)
        plugin_shell.set_status(shell, "Copied", 1500)

    def _defs_menu(pos):
        row = defs_table.indexAt(pos).row()
        if row < 0:
            return
        name = defs_table.item(row, 0).text()
        menu = QMenu(defs_table)
        menu.addAction("Copy name", lambda: _copy_text(name))
        menu.addAction(
            "Show on Values",
            lambda: (
                val_search.setText(name),
                _refresh_vals(),
                _select_val_row(name)))
        menu.addSeparator()
        menu.addAction("Open Messages…", _goto_messages)
        act_del = menu.addAction("Delete definition", _on_del_def)
        if name in ("GenMsgCycleTime", "GenMsgSendType"):
            act_del.setEnabled(False)
        menu.exec(defs_table.viewport().mapToGlobal(pos))

    def _select_val_row(name: str):
        for r in range(vals_table.rowCount()):
            if vals_table.item(r, 0) and vals_table.item(r, 0).text() == name:
                vals_table.selectRow(r)
                return

    def _vals_menu(pos):
        row = vals_table.indexAt(pos).row()
        if row < 0:
            return
        name = vals_table.item(row, 0).text()
        value = vals_table.item(row, 1).text() if vals_table.item(row, 1) else ""
        default = vals_table.item(row, 2).text() if vals_table.item(row, 2) else ""
        menu = QMenu(vals_table)
        menu.addAction("Copy name", lambda: _copy_text(name))
        if value:
            menu.addAction("Copy value", lambda: _copy_text(value))
        menu.addSeparator()
        if default:
            menu.addAction(
                "Apply default",
                lambda: (edit.setText(default), _on_apply()))
        menu.addAction("Clear value", _on_clear)
        menu.addSeparator()
        menu.addAction("Open Messages…", _goto_messages)
        menu.exec(vals_table.viewport().mapToGlobal(pos))

    add_def.clicked.connect(_on_add_def)
    presets_btn.clicked.connect(_on_presets)
    del_def.clicked.connect(_on_del_def)
    goto_msg.clicked.connect(_goto_messages)
    apply_val.clicked.connect(_on_apply)
    use_def.clicked.connect(_on_use_default)
    clear_val.clicked.connect(_on_clear)
    edit.returnPressed.connect(_on_apply)
    vals_table.itemSelectionChanged.connect(_on_val_selected)
    defs_table.customContextMenuRequested.connect(_defs_menu)
    vals_table.customContextMenuRequested.connect(_vals_menu)
    def_search.textChanged.connect(lambda _t: _refresh_defs())
    val_search.textChanged.connect(lambda _t: _refresh_vals())
    scope.currentIndexChanged.connect(
        lambda _i: (_fill_targets(), _refresh_vals()))
    target.currentIndexChanged.connect(lambda _i: _refresh_vals())
    document.on_changed(
        lambda: (_refresh_defs(), _fill_targets(), _refresh_vals()))
    document.on_focus(
        lambda: (_fill_targets(), _refresh_vals()))

    _refresh_defs()
    _fill_targets()
    _refresh_vals()

    def _refresh_all():
        _refresh_defs()
        _fill_targets()
        _refresh_vals()

    root.refresh = _refresh_all
    return root
