# -*- coding: utf-8 -*-
"""PDO Map — file-layer edit of 0x14xx/16xx/18xx/1Axx mapping entries."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QComboBox,
    QLabel,
    QLineEdit,
    QSpinBox,
    QStackedWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import edsparse, suite_chrome
from pages import _ui


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


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    pdo_box = QComboBox()
    pdo_box.setFixedHeight(26)
    pdo_box.setMinimumWidth(150)
    pdo_box.setToolTip("PDO communication / mapping object")
    sub_spin = QSpinBox()
    sub_spin.setObjectName("SuiteSpin")
    sub_spin.setRange(1, 64)
    sub_spin.setFixedHeight(26)
    sub_spin.setToolTip("Mapping entry number")
    map_idx = QSpinBox()
    map_idx.setObjectName("SuiteSpin")
    map_idx.setRange(0, 0xFFFF)
    map_idx.setDisplayIntegerBase(16)
    map_idx.setPrefix("0x")
    map_idx.setFixedHeight(26)
    map_idx.setToolTip("Mapped OD index")
    map_sub = QSpinBox()
    map_sub.setObjectName("SuiteSpin")
    map_sub.setRange(0, 254)
    map_sub.setFixedHeight(26)
    map_sub.setToolTip("Mapped sub-index")
    map_bits = QSpinBox()
    map_bits.setObjectName("SuiteSpin")
    map_bits.setRange(1, 64)
    map_bits.setValue(16)
    map_bits.setFixedHeight(26)
    map_bits.setToolTip("Bit length")
    raw_edit = QLineEdit()
    raw_edit.setFixedHeight(26)
    raw_edit.setMaximumWidth(120)
    raw_edit.setPlaceholderText("0x…")
    raw_edit.setToolTip("Raw mapping dword = index|sub|bits")
    count = _ui.count_label()
    apply_btn = _ui.primary_btn("Apply", "Write mapping entry into the EDS", "apply")
    for lab, w in (
        ("", pdo_box), ("#", sub_spin), ("Idx", map_idx),
        ("Sub", map_sub), ("Bits", map_bits), ("", raw_edit),
    ):
        if lab:
            t = QLabel(lab)
            t.setStyleSheet("color:#78909C;font-size:11px;")
            crow.addWidget(t)
        crow.addWidget(w)
    crow.addWidget(count)
    crow.addStretch(1)
    crow.addWidget(apply_btn)
    layout.addWidget(chrome)

    body = QStackedWidget()
    empty = _ui.empty_state(
        "No PDO objects yet.\n"
        "Open Library → Profiles → Pack RPDO1+TPDO1, or use a Starter.")
    tree = QTreeWidget()
    tree.setHeaderLabels([
        "PDO", "Sub", "Name", "Mapped", "Bits", "Raw"])
    _ui.style_tree(tree, stretch_col=2)
    tree.setRootIsDecorated(True)
    tree.setToolTip("Select a mapping row to edit · dword = index|sub|bitlen")
    body.addWidget(empty)
    body.addWidget(tree)
    layout.addWidget(body, 1)

    def _pdo_indexes():
        return sorted({
            e.index for e in document.eds.entries
            if (0x1400 <= e.index <= 0x15FF
                or 0x1600 <= e.index <= 0x17FF
                or 0x1800 <= e.index <= 0x19FF
                or 0x1A00 <= e.index <= 0x1BFF)
        })

    def _refresh():
        tree.clear()
        pdo_box.blockSignals(True)
        pdo_box.clear()
        by_idx = {}
        for e in document.eds.entries:
            by_idx.setdefault(e.index, []).append(e)
        idxs = _pdo_indexes()
        if not idxs:
            body.setCurrentWidget(empty)
            count.setText("0 PDOs")
            pdo_box.blockSignals(False)
            return
        body.setCurrentWidget(tree)
        n_map = 0
        for idx in idxs:
            kind = "RPDO" if idx < 0x1800 else "TPDO"
            role = ("comm" if (0x1400 <= idx <= 0x15FF or 0x1800 <= idx <= 0x19FF)
                    else "map")
            pdo_box.addItem("0x%04X %s" % (idx, kind), idx)
            parent = QTreeWidgetItem([
                "0x%04X" % idx, "", "%s · %s" % (kind, role), "", "", ""])
            parent.setFlags(parent.flags() & ~Qt.ItemFlag.ItemIsSelectable)
            tree.addTopLevelItem(parent)
            for e in sorted(by_idx.get(idx, []), key=lambda x: x.subindex):
                if e.subindex == 0:
                    child = QTreeWidgetItem([
                        "", "0", e.name or "Number of entries",
                        "", "", e.default_value or e.parameter_value or ""])
                else:
                    n_map += 1
                    mi, ms, mb = _parse_map(
                        e.parameter_value or e.default_value)
                    child = QTreeWidgetItem([
                        "", str(e.subindex), e.name,
                        "0x%04X:%02X" % (mi, ms) if mi else "",
                        str(mb) if mb else "",
                        e.parameter_value or e.default_value or ""])
                child.setData(
                    0, Qt.ItemDataRole.UserRole, (e.index, e.subindex))
                parent.addChild(child)
        tree.expandAll()
        count.setText("%d PDO · %d maps" % (len(idxs), n_map))
        pdo_box.blockSignals(False)

    def _on_select():
        item = tree.currentItem()
        if not item:
            return
        key = item.data(0, Qt.ItemDataRole.UserRole)
        if not key:
            return
        idx, sub = key
        for i in range(pdo_box.count()):
            if pdo_box.itemData(i) == idx:
                pdo_box.setCurrentIndex(i)
                break
        sub_spin.setValue(max(1, sub))
        entry = edsparse.find_entry(document.eds.entries, idx, sub)
        if not entry or sub == 0:
            return
        mi, ms, mb = _parse_map(entry.parameter_value or entry.default_value)
        map_idx.setValue(mi)
        map_sub.setValue(ms)
        if mb:
            map_bits.setValue(mb)
        raw_edit.setText(entry.parameter_value or entry.default_value or "")

    def _sync_raw_from_fields():
        raw_edit.setText(_pack_map(
            map_idx.value(), map_sub.value(), map_bits.value()))

    def _sync_fields_from_raw():
        mi, ms, mb = _parse_map(raw_edit.text())
        map_idx.blockSignals(True)
        map_sub.blockSignals(True)
        map_bits.blockSignals(True)
        map_idx.setValue(mi)
        map_sub.setValue(ms)
        if mb:
            map_bits.setValue(mb)
        map_idx.blockSignals(False)
        map_sub.blockSignals(False)
        map_bits.blockSignals(False)

    def _apply():
        idx = pdo_box.currentData()
        if idx is None:
            log_fn("WARN", "No PDO selected")
            return
        sub = sub_spin.value()
        raw = raw_edit.text().strip() or _pack_map(
            map_idx.value(), map_sub.value(), map_bits.value())
        eds = document.clone_eds()
        entry = edsparse.find_entry(eds.entries, idx, sub)
        if entry is None:
            entry = edsparse.OdEntry(
                index=idx, subindex=sub,
                name="Mapping entry %d" % sub,
                object_type="0x7", data_type="0x0007", access_type="rw")
            eds.entries.append(entry)
            eds.entries.sort(key=lambda e: (e.index, e.subindex))
        entry.default_value = raw
        if eds.is_dcf:
            entry.parameter_value = raw
        parent = edsparse.find_entry(eds.entries, idx, 0)
        if parent is None:
            parent = edsparse.OdEntry(
                index=idx, subindex=0, name="Number of mapped objects",
                object_type="0x7", data_type="0x0005", access_type="rw",
                default_value=str(sub))
            eds.entries.append(parent)
            eds.entries.sort(key=lambda e: (e.index, e.subindex))
        else:
            try:
                cur = int(str(parent.default_value or "0").replace("0x", ""), 0)
            except ValueError:
                cur = 0
            if sub > cur:
                parent.default_value = str(sub)
        document.apply_eds(eds)
        log_fn("OK", "PDO 0x%04X:%02X = %s" % (idx, sub, raw))
        _refresh()

    map_idx.valueChanged.connect(lambda *_: _sync_raw_from_fields())
    map_sub.valueChanged.connect(lambda *_: _sync_raw_from_fields())
    map_bits.valueChanged.connect(lambda *_: _sync_raw_from_fields())
    raw_edit.editingFinished.connect(_sync_fields_from_raw)
    tree.itemSelectionChanged.connect(_on_select)
    apply_btn.clicked.connect(_apply)
    document.on_changed(_refresh)
    _refresh()
    return root
