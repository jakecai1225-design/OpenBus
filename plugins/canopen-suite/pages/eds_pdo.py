# -*- coding: utf-8 -*-
"""EDS PDO Map — pick OD objects into mapping slots (foolproof).

Left: PDO map tree. Right: Object Dictionary picker.
Select a slot → click an OD object → mapped (index/sub/bits filled + saved).
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QSplitter,
    QStackedWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.eds_parse import OdEntry
from pages import _ui


# CiA 301 data-type code → bit length for PDO mapping dword
_DTYPE_BITS = {
    0x0001: 1,   # BOOLEAN
    0x0002: 8,   # INTEGER8
    0x0003: 16,  # INTEGER16
    0x0004: 32,  # INTEGER32
    0x0005: 8,   # UNSIGNED8
    0x0006: 16,  # UNSIGNED16
    0x0007: 32,  # UNSIGNED32
    0x0008: 32,  # REAL32
    0x0010: 24,  # INTEGER24
    0x0011: 64,  # REAL64
    0x0012: 40,
    0x0013: 48,
    0x0014: 56,
    0x0015: 64,
    0x0016: 24,  # UNSIGNED24
    0x0018: 40,
    0x0019: 48,
    0x001A: 56,
    0x001B: 64,
}


def _parse_map(text: str):
    s = (text or "").strip().lower().replace("0x", "")
    if not s:
        return 0, 0, 0
    try:
        raw = int(s, 16)
    except ValueError:
        return 0, 0, 0
    return (raw >> 16) & 0xFFFF, (raw >> 8) & 0xFF, raw & 0xFF


def _pack_map(index: int, sub: int, bits: int) -> str:
    raw = ((index & 0xFFFF) << 16) | ((sub & 0xFF) << 8) | (bits & 0xFF)
    return "0x%08X" % raw


def _find(entries, index, sub):
    for e in entries:
        if e.index == index and e.subindex == sub:
            return e
    return None


def _classify(index: int):
    """Return (family, slot, role) e.g. ('RPDO', 1, 'map') or None."""
    if 0x1400 <= index <= 0x15FF:
        return "RPDO", (index - 0x1400) + 1, "comm"
    if 0x1600 <= index <= 0x17FF:
        return "RPDO", (index - 0x1600) + 1, "map"
    if 0x1800 <= index <= 0x19FF:
        return "TPDO", (index - 0x1800) + 1, "comm"
    if 0x1A00 <= index <= 0x1BFF:
        return "TPDO", (index - 0x1A00) + 1, "map"
    return None


def _parse_dtype_code(text: str) -> int:
    s = (text or "").strip()
    if not s:
        return 0
    try:
        return int(s, 0) & 0xFFFF
    except ValueError:
        return 0


def _bits_for_entry(entry: OdEntry) -> int:
    code = _parse_dtype_code(getattr(entry, "data_type", "") or "")
    return int(_DTYPE_BITS.get(code, 16))


def _pdo_flag(entry: OdEntry) -> bool:
    v = str(getattr(entry, "pdo_mapping", "") or "").strip().lower()
    return v in ("1", "yes", "true", "optional")


def _is_pdo_area(index: int) -> bool:
    return 0x1400 <= index <= 0x1BFF


def _mappable_entries(entries) -> list:
    """OD entries suitable for PDO mapping (exclude PDO shell objects)."""
    out = []
    for e in entries or ():
        if _is_pdo_area(e.index):
            continue
        # Prefer concrete VAR / array elements (skip empty ARRAY/RECORD shells)
        ot = str(getattr(e, "object_type", "") or "").lower()
        if ot in ("0x8", "0x9", "array", "record") and e.subindex == 0:
            continue
        if not (e.name or e.data_type):
            continue
        out.append(e)
    # PDO-mappable first, then by index
    out.sort(key=lambda e: (0 if _pdo_flag(e) else 1, e.index, e.subindex))
    return out


def _entry_matches_filter(entry, needle: str) -> bool:
    """Loose match: name tokens, hex index, optional 0x / sub-index."""
    n = (needle or "").strip().lower()
    if not n:
        return True
    n_compact = n.replace(" ", "")
    n_hex = n_compact.replace("0x", "")
    name = (getattr(entry, "name", None) or "").lower()
    name_compact = name.replace(" ", "")
    idx = "%04x" % int(entry.index)
    idx_sub = "%04x:%02x" % (int(entry.index), int(entry.subindex))
    hay = "0x%s:%02x %s" % (idx, int(entry.subindex), name)
    if n in hay or n_compact in hay.replace(" ", ""):
        return True
    if n_compact in name_compact or n in name:
        return True
    if n_hex and (n_hex in idx or n_hex in idx_sub or idx.startswith(n_hex)):
        return True
    try:
        if int(n_hex, 16) == int(entry.index):
            return True
    except ValueError:
        pass
    return False


def map_budget(entries, map_index: int, *, exclude_sub: int | None = None):
    """Return ``(slot_count, used_bits)`` for one PDO mapping object.

    ``exclude_sub`` omits that mapping entry (use when replacing a slot).
    """
    used = 0
    slots = 0
    for e in entries or ():
        if e.index != map_index or e.subindex == 0:
            continue
        if exclude_sub is not None and e.subindex == exclude_sub:
            continue
        _mi, _ms, mb = _parse_map(
            getattr(e, "parameter_value", None)
            or getattr(e, "default_value", None))
        if mb:
            used += int(mb)
            slots += 1
    return slots, used


def would_exceed_pdo_bits(
        entries, map_index: int, slot_sub: int, new_bits: int) -> bool:
    """True if writing ``new_bits`` into ``slot_sub`` would exceed 64 bits."""
    _slots, used = map_budget(entries, map_index, exclude_sub=slot_sub)
    return (used + max(0, int(new_bits or 0))) > 64


def format_budget_text(slots: int, used: int) -> str:
    free = max(0, 64 - int(used))
    return "%d slot(s) · %d / 64 bits · %d free" % (slots, used, free)


def format_status_line(fam: str, sub: int, used: int) -> str:
    """Compact header status: family, slot, free bits."""
    free = max(0, 64 - int(used))
    return "%s #%d · %d free" % (fam, sub, free)


def first_ready_map_slot(entries):
    """Prefer first empty map sub-entry; else first map #1; else None."""
    map_idxs = sorted({
        e.index for e in (entries or ())
        if (_c := _classify(e.index)) is not None and _c[2] == "map"
    })
    empty = None
    first = None
    for idx in map_idxs:
        for e in sorted(
                (x for x in entries if x.index == idx and x.subindex > 0),
                key=lambda x: x.subindex):
            if first is None:
                first = (idx, e.subindex)
            mi, _ms, _mb = _parse_map(
                getattr(e, "parameter_value", None)
                or getattr(e, "default_value", None))
            if not mi:
                empty = (idx, e.subindex)
                break
        if empty:
            break
    return empty or first


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    # ---- Left: Slots ----
    left = QWidget()
    left.setMinimumWidth(_ui.PANE_MIN)
    left_lay = QVBoxLayout(left)
    left_lay.setContentsMargins(0, 0, 0, 0)
    left_lay.setSpacing(0)

    count_lab = QLabel("0")
    count_lab.setObjectName("SuiteCount")
    add_btn = _ui.icon_tool("add", "Add mapping slot")
    remove_btn = _ui.icon_tool("remove", "Remove selected mapping slot")
    import_btn = _ui.icon_tool(
        "database", "Import RPDO1+TPDO1 pack if none exist")
    left_lay.addWidget(
        _ui.panel_header("Slots", count_lab, add_btn, remove_btn, import_btn))

    tree = QTreeWidget()
    tree.setHeaderLabels(["Entry", "Mapped", "Bits"])
    tree.setToolTip(
        "Select a map entry (#1…).\n"
        "Then click an object on the right to map it.")
    _ui.style_tree(tree, header_hidden=False)
    tree.setAlternatingRowColors(True)
    _ui.configure_columns(tree, stretch=1, mins={0: 100, 1: 80, 2: 40})
    left_lay.addWidget(tree, 1)

    goto_check = _ui.ghost_btn(
        "Validate", "Check the EDS before save / apply", "validate")
    goto_apply = _ui.primary_btn(
        "Apply → Live OD", "Copy draft into Live OD for SDO", "apply")
    next_host = _ui.next_step_bar(
        "Mapped — next:", goto_check, goto_apply)
    next_host.setVisible(False)
    left_lay.addWidget(next_host)

    # ---- Right: Objects (click to map) ----
    right = QWidget()
    right.setObjectName("SuitePropPanel")
    right.setMinimumWidth(_ui.PANE_MIN_PROP)
    right_lay = QVBoxLayout(right)
    right_lay.setContentsMargins(0, 0, 0, 0)
    right_lay.setSpacing(0)

    status_lab = QLabel("Select a slot, then click an object")
    status_lab.setObjectName("SuiteCount")
    status_lab.setContentsMargins(_ui.PAD_X, 0, _ui.PAD_X, 0)
    status_lab.setFixedHeight(_ui.CTRL_H)
    status_lab.setToolTip("Active slot and bit budget (max 64)")
    right_lay.addWidget(_ui.panel_header("Objects"))
    right_lay.addWidget(status_lab)

    od_filter = QLineEdit()
    od_filter.setPlaceholderText("Filter…")
    od_filter.setClearButtonEnabled(True)
    od_filter.setToolTip("Filter Object Dictionary")
    right_lay.addWidget(_ui.inline_filter(od_filter))

    od_list = QListWidget()
    od_list.setAlternatingRowColors(True)
    od_list.setUniformItemSizes(True)
    od_list.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
    od_list.setToolTip("Click an object to map it into the selected slot")
    od_list.setHorizontalScrollBarPolicy(
        Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
    od_list.setVerticalScrollBarPolicy(
        Qt.ScrollBarPolicy.ScrollBarAsNeeded)
    od_list.setVerticalScrollMode(
        QAbstractItemView.ScrollMode.ScrollPerPixel)
    od_list.setTextElideMode(Qt.TextElideMode.ElideRight)
    _ui.style_list(od_list)
    right_lay.addWidget(od_list, 1)

    # Hidden field state for click-to-map (no visible advanced panel).
    map_idx = _ui.suite_spin(
        0, minimum=0, maximum=0xFFFF, hex_mode=True,
        tip="Mapped OD index", width=120)
    map_sub = _ui.suite_spin(
        0, minimum=0, maximum=254, tip="Mapped sub-index", width=88)
    map_bits = _ui.suite_spin(
        16, minimum=1, maximum=64, tip="Bit length", width=88)
    raw_edit = QLineEdit()
    apply_btn = _ui.ghost_btn("Apply", "Save Index/Sub/Bits as typed", "apply")
    for w in (map_idx, map_sub, map_bits, raw_edit, apply_btn):
        w.setParent(root)
        w.hide()

    split = QSplitter(Qt.Orientation.Horizontal)
    split.addWidget(left)
    split.addWidget(right)
    _ui.configure_splitter(
        split, golden=True, master_left=True, stretch=(1, 1), handle_width=6)

    def _fit_split_sizes():
        """Re-apply ratio from current viewport so panes share real width."""
        w = max(400, split.width())
        major = int(round(w / _ui.PHI))
        minor = max(1, w - major)
        split.setSizes([major, minor])

    from PyQt6.QtCore import QTimer
    QTimer.singleShot(0, _fit_split_sizes)

    empty_import = _ui.primary_btn(
        "Import RPDO1+TPDO1",
        "Insert communication + mapping objects, then click an OD to map",
        "database")
    empty = _ui.empty_state(
        "No PDO slots yet",
        "Import a pack, select a slot, click an object — done.",
        actions=[empty_import])
    body = QStackedWidget()
    body.addWidget(empty)
    body.addWidget(split)
    layout.addWidget(body, 1)

    sel = {"idx": None, "sub": None, "mute": False}

    def _entries():
        return session.draft_entries or []

    def _pdo_indexes():
        return sorted({
            e.index for e in _entries()
            if (0x1400 <= e.index <= 0x15FF
                or 0x1600 <= e.index <= 0x17FF
                or 0x1800 <= e.index <= 0x19FF
                or 0x1A00 <= e.index <= 0x1BFF)
        })

    def _slot_ready() -> bool:
        return (
            sel["idx"] is not None
            and sel["sub"] is not None
            and sel["sub"] >= 1
            and _classify(sel["idx"]) is not None
            and _classify(sel["idx"])[2] == "map")

    def _can_add_map() -> bool:
        info = _classify(sel["idx"]) if sel["idx"] is not None else None
        return bool(info and info[2] == "map")

    def _set_editor_enabled(on: bool):
        for w in (map_idx, map_sub, map_bits, raw_edit, apply_btn, remove_btn):
            w.setEnabled(on)
        od_list.setEnabled(on)
        od_filter.setEnabled(on)

    def _set_status(text: str, *, alarm: bool = False):
        status_lab.setText(text)
        status_lab.setStyleSheet("color: #C72E0F;" if alarm else "")

    def _update_budget(map_index: int | None):
        if map_index is None or not _slot_ready():
            if sel["idx"] is None:
                _set_status("Select a slot, then click an object")
            return
        _slots, used = map_budget(_entries(), map_index)
        info = _classify(map_index)
        fam = "%s%d" % (info[0], info[1]) if info else "PDO"
        _set_status(
            format_status_line(fam, sel["sub"], used), alarm=used > 64)

    def _refresh_od_list():
        needle = (od_filter.text() or "").strip().lower()
        cur_key = None
        cit = od_list.currentItem()
        if cit is not None:
            cur_key = cit.data(Qt.ItemDataRole.UserRole)
        prefer = None
        fi = getattr(session, "focus_index", 0) or 0
        if fi:
            prefer = (fi, getattr(session, "focus_subindex", 0) or 0)
        od_list.blockSignals(True)
        od_list.clear()
        for e in _mappable_entries(_entries()):
            if needle and not _entry_matches_filter(e, needle):
                continue
            label = "0x%04X:%02X  %s" % (
                e.index, e.subindex, e.name or "(unnamed)")
            bits = _bits_for_entry(e)
            suffix = " · %d bit" % bits
            if _pdo_flag(e):
                suffix += " · PDO"
            text = label + suffix
            item = QListWidgetItem(text)
            item.setData(
                Qt.ItemDataRole.UserRole,
                (e.index, e.subindex, bits, e.name or ""))
            tip = "Click to map · 0x%04X sub %d · %s · %d bits" % (
                e.index, e.subindex, e.name or "", bits)
            if _pdo_flag(e):
                tip += " · PDO-mappable"
            item.setToolTip(tip)
            od_list.addItem(item)
            key2 = (e.index, e.subindex)
            if prefer and key2 == prefer:
                od_list.setCurrentItem(item)
            elif cur_key and cur_key[:2] == key2 and not prefer:
                od_list.setCurrentItem(item)
        od_list.blockSignals(False)

    def focus_object(index: int, subindex: int = 0):
        if hasattr(session, "set_focus"):
            session.set_focus(index, subindex)
        # Prefer filter by hex index so the row is visible
        od_filter.setText("%04X" % (index & 0xFFFF))
        _refresh_od_list()
        for i in range(od_list.count()):
            item = od_list.item(i)
            data = item.data(Qt.ItemDataRole.UserRole)
            if data and data[0] == index and data[1] == subindex:
                od_list.setCurrentItem(item)
                od_list.scrollToItem(item)
                break
        from _shared import plugin_shell
        plugin_shell.set_status(
            parent, "Focus 0x%04X:%02X" % (index, subindex), 2000)

    def _write_slot(mi: int, ms: int, mb: int, name_hint: str = ""):
        """Persist mapping dword into the selected slot."""
        idx, sub = sel["idx"], sel["sub"]
        if idx is None or sub is None or sub < 1:
            from _shared import plugin_shell
            plugin_shell.set_status(
                parent, "Select a map slot on the left first", 3000)
            return False
        if would_exceed_pdo_bits(_entries(), idx, sub, mb):
            from _shared import plugin_shell
            plugin_shell.set_status(
                parent,
                "Would exceed 64 bits — free space or pick a shorter object",
                4000)
            _update_budget(idx)
            return False
        raw = _pack_map(mi, ms, mb)
        entries = list(_entries())
        entry = _find(entries, idx, sub)
        if entry is None:
            entry = OdEntry(
                index=idx, subindex=sub,
                name=name_hint or ("Mapping entry %d" % sub),
                object_type="0x7", data_type="0x0007", access_type="rw")
            entries.append(entry)
        else:
            if name_hint:
                entry.name = name_hint
        entry.default_value = raw
        parent_e = _find(entries, idx, 0)
        if parent_e is None:
            parent_e = OdEntry(
                index=idx, subindex=0, name="Number of mapped objects",
                object_type="0x7", data_type="0x0005", access_type="rw",
                default_value=str(sub))
            entries.append(parent_e)
        else:
            try:
                cur = int(
                    str(parent_e.default_value or "0").replace("0x", ""), 0)
            except ValueError:
                cur = 0
            if sub > cur:
                parent_e.default_value = str(sub)
        session.draft_entries = sorted(
            entries, key=lambda e: (e.index, e.subindex))
        session.refresh_dirty()
        session._notify_od()
        log_fn(
            "SYS", "-", b"",
            "Mapped 0x%04X:%02X (%d bit) → PDO 0x%04X #%d" % (
                mi, ms, mb, idx, sub))
        return True

    def _fill_spins(mi: int, ms: int, mb: int):
        sel["mute"] = True
        map_idx.setValue(mi)
        map_sub.setValue(ms)
        map_bits.setValue(mb if mb else 16)
        raw_edit.setText(_pack_map(mi, ms, mb if mb else 16))
        sel["mute"] = False

    def _load_fields(idx: int, sub: int):
        sel["mute"] = True
        info = _classify(idx)
        entry = _find(_entries(), idx, sub)
        if entry and sub > 0:
            mi, ms, mb = _parse_map(entry.parameter_value or entry.default_value)
            _fill_spins(mi, ms, mb or 16)
            _set_editor_enabled(True)
            add_btn.setEnabled(True)
        else:
            _fill_spins(0, 0, 16)
            _set_editor_enabled(False)
            add_btn.setEnabled(_can_add_map())
            _set_status("Select a map entry (#1…), then click an object")
        _update_budget(idx if (info and info[2] == "map") else None)
        sel["mute"] = False

    def refresh():
        prev = (sel["idx"], sel["sub"])
        tree.blockSignals(True)
        tree.clear()
        by_idx = {}
        for e in _entries():
            by_idx.setdefault(e.index, []).append(e)
        idxs = _pdo_indexes()
        if not idxs:
            body.setCurrentWidget(empty)
            count_lab.setText("0")
            sel["idx"] = sel["sub"] = None
            next_host.setVisible(False)
            tree.blockSignals(False)
            _refresh_od_list()
            return
        body.setCurrentWidget(split)
        next_host.setVisible(True)

        groups: dict[tuple, list] = {}
        orphans = []
        for idx in idxs:
            info = _classify(idx)
            if info is None:
                orphans.append(idx)
                continue
            fam, slot, _role = info
            groups.setdefault((fam, slot), []).append(idx)

        n_map = 0
        for (fam, slot) in sorted(
                groups.keys(),
                key=lambda k: (0 if k[0] == "RPDO" else 1, k[1])):
            g_item = QTreeWidgetItem(["%s%d" % (fam, slot), "", ""])
            g_item.setFlags(g_item.flags() & ~Qt.ItemFlag.ItemIsSelectable)
            tree.addTopLevelItem(g_item)
            for idx in sorted(groups[(fam, slot)]):
                info = _classify(idx)
                role = info[2] if info else ""
                role_lab = "Map" if role == "map" else "Comm"
                parent = QTreeWidgetItem([
                    "0x%04X · %s" % (idx, role_lab), "", ""])
                parent.setData(0, Qt.ItemDataRole.UserRole, (idx, 0))
                if role == "comm":
                    parent.setFlags(
                        parent.flags() & ~Qt.ItemFlag.ItemIsSelectable)
                    parent.setForeground(0, Qt.GlobalColor.gray)
                    parent.setToolTip(
                        0, "Communication parameters — edit COB-ID in Dictionary")
                g_item.addChild(parent)
                if role != "map":
                    continue
                used = 0
                for e in sorted(
                        by_idx.get(idx, []), key=lambda x: x.subindex):
                    if e.subindex == 0:
                        child = QTreeWidgetItem([
                            "#0 · count",
                            e.default_value or e.parameter_value or "0", ""])
                        child.setData(
                            0, Qt.ItemDataRole.UserRole, (idx, 0))
                        child.setFlags(
                            child.flags() & ~Qt.ItemFlag.ItemIsSelectable)
                        parent.addChild(child)
                        continue
                    n_map += 1
                    mi, ms, mb = _parse_map(
                        e.parameter_value or e.default_value)
                    used += mb or 0
                    # Resolve friendly name from OD
                    src = _find(_entries(), mi, ms) if mi else None
                    mapped = (
                        "0x%04X:%02X" % (mi, ms) if mi else "— empty —")
                    if src and src.name:
                        mapped = "%s (%s)" % (mapped, src.name)
                    child = QTreeWidgetItem([
                        "#%d" % e.subindex,
                        mapped,
                        str(mb) if mb else "—"])
                    child.setData(
                        0, Qt.ItemDataRole.UserRole, (idx, e.subindex))
                    parent.addChild(child)
                parent.setText(2, "%d/64" % used)

        for idx in orphans:
            parent = QTreeWidgetItem(["0x%04X" % idx, "", ""])
            parent.setData(0, Qt.ItemDataRole.UserRole, (idx, 0))
            tree.addTopLevelItem(parent)

        tree.expandToDepth(1)
        count_lab.setText("%d · %d maps" % (len(idxs), n_map))
        tree.blockSignals(False)
        _ui.fit_columns(tree, stretch=0)
        _refresh_od_list()
        if prev[0] is not None:
            _select_key(prev[0], prev[1])

    def _select_key(idx: int, sub: int):
        for i in range(tree.topLevelItemCount()):
            top = tree.topLevelItem(i)
            stack = [top]
            while stack:
                item = stack.pop()
                key = item.data(0, Qt.ItemDataRole.UserRole)
                if key == (idx, sub):
                    tree.setCurrentItem(item)
                    return
                for c in range(item.childCount()):
                    stack.append(item.child(c))

    def on_select():
        item = tree.currentItem()
        if not item:
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        if not key:
            _set_status("Select a slot, then click an object")
            _set_editor_enabled(False)
            add_btn.setEnabled(False)
            return
        idx, sub = key
        sel["idx"], sel["sub"] = idx, sub
        info = _classify(idx)
        if info and info[2] == "comm":
            _set_status(
                "%s%d Comm — edit COB-ID in Dictionary" % (info[0], info[1]))
            _set_editor_enabled(False)
            add_btn.setEnabled(False)
            return
        _load_fields(idx, sub)

    def sync_raw():
        if sel["mute"]:
            return
        raw_edit.setText(_pack_map(
            map_idx.value(), map_sub.value(), map_bits.value()))

    def sync_fields():
        if sel["mute"]:
            return
        mi, ms, mb = _parse_map(raw_edit.text())
        sel["mute"] = True
        map_idx.setValue(mi)
        map_sub.setValue(ms)
        if mb:
            map_bits.setValue(mb)
        sel["mute"] = False

    def apply_map():
        if not _write_slot(
                map_idx.value(), map_sub.value(), map_bits.value()):
            return
        refresh()

    def map_from_od():
        item = od_list.currentItem()
        if item is None:
            from _shared import plugin_shell
            plugin_shell.set_status(
                parent, "Click an object in the list", 2500)
            return
        if not _slot_ready():
            from _shared import plugin_shell
            plugin_shell.set_status(
                parent, "Select a map slot on the left first", 3000)
            return
        data = item.data(Qt.ItemDataRole.UserRole)
        mi, ms, mb, name = data
        _fill_spins(mi, ms, mb)
        label = name or ("Object 0x%04X" % mi)
        mapped_sub = sel["sub"]
        if _write_slot(mi, ms, mb, "Mapped: %s" % label):
            from _shared import plugin_shell
            plugin_shell.set_status(
                parent,
                "Mapped %s → #%d" % (label, mapped_sub), 2500)
            refresh()
            nxt = first_ready_map_slot(_entries())
            if nxt is not None and nxt != (sel["idx"], sel["sub"]):
                sel["idx"], sel["sub"] = nxt[0], nxt[1]
                _select_key(nxt[0], nxt[1])
                _load_fields(nxt[0], nxt[1])
            elif _slot_ready():
                _select_key(sel["idx"], sel["sub"])
                _load_fields(sel["idx"], sel["sub"])

    def on_od_activated(_item=None):
        map_from_od()

    def on_od_clicked(item):
        if item is None:
            return
        od_list.setCurrentItem(item)
        map_from_od()

    def add_slot():
        idx = sel["idx"]
        info = _classify(idx) if idx is not None else None
        # If user selected a group/comm, try first map object under same PDO
        if info is None or info[2] != "map":
            # Prefer first map index in draft
            maps = [
                i for i in _pdo_indexes()
                if (_c := _classify(i)) is not None and _c[2] == "map"]
            if not maps:
                from _shared import plugin_shell
                plugin_shell.set_status(
                    parent, "Import a PDO pack first", 2500)
                return
            idx = maps[0]
            info = _classify(idx)
        used = {
            e.subindex for e in _entries()
            if e.index == idx and e.subindex > 0}
        sub = 1
        while sub in used and sub < 64:
            sub += 1
        sel["idx"], sel["sub"] = idx, sub
        entries = list(_entries())
        if _find(entries, idx, sub) is None:
            entries.append(OdEntry(
                index=idx, subindex=sub,
                name="Mapping entry %d" % sub,
                object_type="0x7", data_type="0x0007", access_type="rw",
                default_value=_pack_map(0, 0, 16)))
            parent_e = _find(entries, idx, 0)
            if parent_e is None:
                entries.append(OdEntry(
                    index=idx, subindex=0,
                    name="Number of mapped objects",
                    object_type="0x7", data_type="0x0005", access_type="rw",
                    default_value=str(sub)))
            else:
                try:
                    cur = int(
                        str(parent_e.default_value or "0").replace("0x", ""),
                        0)
                except ValueError:
                    cur = 0
                if sub > cur:
                    parent_e.default_value = str(sub)
            session.draft_entries = sorted(
                entries, key=lambda e: (e.index, e.subindex))
            session.refresh_dirty()
            session._notify_od()
        refresh()
        _select_key(idx, sub)
        _load_fields(idx, sub)
        od_filter.setFocus()

    def remove_slot():
        idx, sub = sel["idx"], sel["sub"]
        if idx is None or sub is None or sub < 1:
            return
        entries = [
            e for e in _entries()
            if not (e.index == idx and e.subindex == sub)]
        parent_e = _find(entries, idx, 0)
        if parent_e is not None:
            remaining = [
                e.subindex for e in entries
                if e.index == idx and e.subindex > 0]
            parent_e.default_value = str(max(remaining) if remaining else 0)
        session.draft_entries = sorted(
            entries, key=lambda e: (e.index, e.subindex))
        session.refresh_dirty()
        session._notify_od()
        log_fn("SYS", "-", b"", "Removed PDO map 0x%04X:%02X" % (idx, sub))
        sel["sub"] = 0
        refresh()
        _select_key(idx, 0)

    def import_pack():
        from _shared.canopen_profiles import objects_for
        stats = session.set_draft_from_library(
            objects_for("RPDO1+TPDO1"), merge=True, overwrite=False)
        log_fn(
            "SYS", "-", b"",
            "PDO pack +%d · skip %d" % (
                stats.get("added", 0), stats.get("skipped", 0)))
        refresh()
        key = first_ready_map_slot(_entries())
        if key is not None:
            sel["idx"], sel["sub"] = key[0], key[1]
            _select_key(key[0], key[1])
            _load_fields(key[0], key[1])
            od_filter.setFocus()
        from _shared import plugin_shell
        plugin_shell.set_status(
            parent,
            "PDO pack ready — pick an object on the right to map",
            3500)

    map_idx.valueChanged.connect(lambda *_: sync_raw())
    map_sub.valueChanged.connect(lambda *_: sync_raw())
    map_bits.valueChanged.connect(lambda *_: sync_raw())
    raw_edit.editingFinished.connect(sync_fields)
    tree.itemSelectionChanged.connect(on_select)
    apply_btn.clicked.connect(apply_map)
    od_list.itemClicked.connect(on_od_clicked)
    od_list.itemActivated.connect(on_od_activated)
    od_filter.textChanged.connect(lambda _t: _refresh_od_list())
    add_btn.clicked.connect(add_slot)
    remove_btn.clicked.connect(remove_slot)
    import_btn.clicked.connect(import_pack)
    empty_import.clicked.connect(import_pack)
    goto_check.clicked.connect(
        lambda: parent.goto_page("eds_check")
        if hasattr(parent, "goto_page") else None)
    goto_apply.clicked.connect(
        lambda: parent.run_action("eds.apply_od")
        if hasattr(parent, "run_action") else None)
    _set_editor_enabled(False)
    add_btn.setEnabled(False)
    session.on_od_changed(refresh)
    refresh()
    root.focus_object = focus_object  # type: ignore[attr-defined]
    return root
