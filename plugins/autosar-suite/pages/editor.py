# -*- coding: utf-8 -*-
"""Editor — COM / ECUC-lite tree + property form + Spec tip."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QComboBox,
    QFormLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import arxmlparse, suite_chrome, vscode_theme
from pages import _ui
import widgets as W


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    chrome, crow = suite_chrome.make_toolbar()
    mode = QComboBox()
    mode.addItems(["COM", "ECUC"])
    mode.setFixedHeight(_ui.CTRL_H)
    mode.setToolTip("Edit COM extract or derived ECUC-lite intermediates")
    count = _ui.quiet_label("")
    add_pdu = _ui.ghost_btn("PDU", "Add I-SIGNAL-I-PDU (COM mode)", "add")
    add_sig = _ui.ghost_btn("Signal", "Add signal under selected PDU", "add")
    remove_btn = _ui.ghost_btn("", "Remove selection", "delete")
    apply_btn = _ui.primary_btn("Apply", "Write form into selection", "apply")
    crow.addWidget(mode)
    crow.addWidget(count)
    crow.addStretch(1)
    crow.addWidget(add_pdu)
    crow.addWidget(add_sig)
    crow.addWidget(remove_btn)
    crow.addWidget(apply_btn)
    layout.addWidget(chrome)

    split = QSplitter(Qt.Orientation.Horizontal)
    split.setHandleWidth(1)

    tree = QTreeWidget()
    tree.setHeaderLabels(["Name", "Kind", "Detail"])
    _ui.style_tree(tree)
    tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    tree.setMinimumWidth(280)
    split.addWidget(tree)

    right = QWidget()
    right.setMaximumWidth(400)
    rl = QVBoxLayout(right)
    rl.setContentsMargins(8, 8, 8, 8)
    rl.setSpacing(8)

    form = QFormLayout()
    vscode_theme.tune_form(form)
    name_edit = QLineEdit()
    name_edit.setFixedHeight(28)
    can_spin = W.spin(
        0, 0x1FFFFFFF, 0, "CAN identifier", width=140, hex_mode=True)
    dlc_spin = W.spin(0, 64, 8, "DLC", width=100)
    start_spin = W.spin(0, 512, 0, "Start bit", width=100)
    len_spin = W.spin(1, 64, 8, "Bit length", width=100)
    endian = QComboBox()
    endian.addItems(["intel", "motorola"])
    endian.setFixedHeight(28)
    factor_edit = QLineEdit("1")
    factor_edit.setFixedHeight(28)
    offset_edit = QLineEdit("0")
    offset_edit.setFixedHeight(28)
    unit_edit = QLineEdit()
    unit_edit.setFixedHeight(28)
    param_edit = QLineEdit()
    param_edit.setFixedHeight(28)
    defref_lbl = _ui.quiet_label("")
    defref_lbl.setWordWrap(True)

    rows = [
        ("Name", name_edit, "i-signal.short-name"),
        ("CAN ID", can_spin, "can-frame-triggering.identifier"),
        ("DLC", dlc_spin, "i-signal-i-pdu.length"),
        ("Start bit", start_spin, "mapping.start-position"),
        ("Length", len_spin, "i-signal.length"),
        ("Endian", endian, "mapping.packing-byte-order"),
        ("Factor", factor_edit, "i-signal.length"),
        ("Offset", offset_edit, "i-signal.length"),
        ("Unit", unit_edit, "i-signal.short-name"),
        ("Param value", param_edit, "ecuc.com.ComIPduSize"),
    ]
    for label, w, tip_id in rows:
        form.addRow(label, w)
        w.setProperty("spec_id", tip_id)
    form.addRow("Definition", defref_lbl)

    tip = _ui.tip_panel()
    tip.setText("Select a PDU or signal — Spec tip appears here.")
    rl.addLayout(form)
    rl.addWidget(QLabel("Spec tip"))
    rl.addWidget(tip, 1)
    split.addWidget(right)
    split.setStretchFactor(0, 3)
    split.setStretchFactor(1, 2)
    layout.addWidget(split, 1)

    selected = {
        "kind": None, "pdu": None, "signal": None,
        "module": None, "path": (), "param": None,
    }
    mute = {"on": False}

    def _is_ecuc() -> bool:
        return document.editor_mode == "ecuc"

    def _show_tip(spec_id: str):
        row = arxmlparse.tip_for_field(spec_id)
        if not row:
            tip.setText("")
            return
        tip.setText(
            "<b>%s</b><br/>%s<br/><span style='color:#78909C'>%s</span>"
            "<br/><i>Range: %s</i>" % (
                row["title"], row["summary"], row["detail"], row["range"]))

    def _set_com_widgets(enabled: bool):
        for w in (can_spin, dlc_spin, start_spin, len_spin, endian,
                  factor_edit, offset_edit, unit_edit, add_pdu, add_sig):
            w.setEnabled(enabled)
        param_edit.setEnabled(not enabled)
        remove_btn.setEnabled(enabled)

    def _rebuild_com():
        for pdu in document.model.ipdus:
            item = QTreeWidgetItem([
                pdu.name, "I-PDU",
                "0x%X · %d B · %d sig" % (
                    pdu.can_id, pdu.dlc, len(pdu.signals))])
            item.setData(0, Qt.ItemDataRole.UserRole, ("pdu", pdu.name, ""))
            tree.addTopLevelItem(item)
            for sig in pdu.signals:
                ch = QTreeWidgetItem([
                    sig.name, "Signal",
                    "%d|%d@%s" % (sig.start_bit, sig.length, sig.endian)])
                ch.setData(
                    0, Qt.ItemDataRole.UserRole, ("signal", pdu.name, sig.name))
                item.addChild(ch)
        n_pdu = len(document.model.ipdus)
        n_sig = sum(len(p.signals) for p in document.model.ipdus)
        count.setText("%d PDU · %d signals" % (n_pdu, n_sig))

    def _add_ecuc_nodes(parent_item, containers, path_prefix):
        for i, cont in enumerate(containers):
            path = path_prefix + (i,)
            detail = cont.definition.rsplit("/", 1)[-1] if cont.definition else ""
            if cont.link_pdu:
                detail = "→ %s" % cont.link_pdu
            item = QTreeWidgetItem([cont.name, "Container", detail])
            item.setData(
                0, Qt.ItemDataRole.UserRole,
                ("ecuc_cont", cont.name, path))
            if parent_item is None:
                tree.addTopLevelItem(item)
            else:
                parent_item.addChild(item)
            for j, p in enumerate(cont.params):
                ch = QTreeWidgetItem([p.name, "Param", p.value])
                ch.setData(
                    0, Qt.ItemDataRole.UserRole,
                    ("ecuc_param", p.name, path + ("p", j)))
                tip_id = {
                    "ComIPduSize": "ecuc.com.ComIPduSize",
                    "ComBitPosition": "ecuc.com.ComBitPosition",
                    "ComBitSize": "ecuc.com.ComBitSize",
                    "ComSignalEndianness": "ecuc.com.ComSignalEndianness",
                    "CanIfTxPduCanId": "ecuc.canif.CanIfTxPduCanId",
                    "CanIfTxPduRef": "ecuc.canif.CanIfTxPduRef",
                    "PduRSrcPduRef": "ecuc.pdur.PduRSrcPduRef",
                    "CanNmMainFunctionPeriod":
                        "ecuc.cannm.CanNmMainFunctionPeriod",
                }.get(p.name, "ecuc.com.ComIPduSize")
                ch.setData(1, Qt.ItemDataRole.UserRole, tip_id)
                item.addChild(ch)
            if cont.children:
                _add_ecuc_nodes(item, cont.children, path)

    def _rebuild_ecuc():
        for mod in document.ecuc.modules:
            item = QTreeWidgetItem([
                mod.name, "Module",
                mod.definition.rsplit("/", 1)[-1] if mod.definition else ""])
            item.setData(
                0, Qt.ItemDataRole.UserRole, ("ecuc_mod", mod.name, ()))
            tree.addTopLevelItem(item)
            _add_ecuc_nodes(item, mod.containers, (mod.name,))
        count.setText("%d ECUC modules" % len(document.ecuc.modules))

    def _rebuild():
        tree.blockSignals(True)
        tree.clear()
        if _is_ecuc():
            mode.blockSignals(True)
            mode.setCurrentText("ECUC")
            mode.blockSignals(False)
            _set_com_widgets(False)
            _rebuild_ecuc()
        else:
            mode.blockSignals(True)
            mode.setCurrentText("COM")
            mode.blockSignals(False)
            _set_com_widgets(True)
            _rebuild_com()
        tree.expandToDepth(2)
        tree.blockSignals(False)

    def _find_pdu(name: str):
        for p in document.model.ipdus:
            if p.name == name:
                return p
        return None

    def _resolve_ecuc_path(path):
        """path: (module_name, idx, idx, ...) or with ('p', j) at end."""
        if not path:
            return None, None, None
        mod = next(
            (m for m in document.ecuc.modules if m.name == path[0]), None)
        if not mod:
            return None, None, None
        containers = mod.containers
        cont = None
        idxs = path[1:]
        param = None
        i = 0
        while i < len(idxs):
            if idxs[i] == "p":
                if cont and i + 1 < len(idxs):
                    j = idxs[i + 1]
                    if 0 <= j < len(cont.params):
                        param = cont.params[j]
                break
            idx = idxs[i]
            if not isinstance(idx, int) or idx < 0 or idx >= len(containers):
                return mod, None, None
            cont = containers[idx]
            containers = cont.children
            i += 1
        return mod, cont, param

    def _on_select():
        item = tree.currentItem()
        if not item:
            return
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if not data:
            return
        kind = data[0]
        mute["on"] = True
        if kind in ("pdu", "signal"):
            _, pdu_n, sig_n = data
            selected["kind"] = kind
            selected["pdu"] = pdu_n
            selected["signal"] = sig_n
            pdu = _find_pdu(pdu_n)
            if kind == "pdu" and pdu:
                name_edit.setText(pdu.name)
                can_spin.setValue(pdu.can_id)
                dlc_spin.setValue(pdu.dlc)
                defref_lbl.setText("")
                _show_tip("i-signal-i-pdu.length")
            elif kind == "signal" and pdu:
                sig = next((s for s in pdu.signals if s.name == sig_n), None)
                if sig:
                    name_edit.setText(sig.name)
                    start_spin.setValue(sig.start_bit)
                    len_spin.setValue(sig.length)
                    endian.setCurrentText(sig.endian)
                    factor_edit.setText(str(sig.factor))
                    offset_edit.setText(str(sig.offset))
                    unit_edit.setText(sig.unit)
                    can_spin.setValue(pdu.can_id)
                    dlc_spin.setValue(pdu.dlc)
                    defref_lbl.setText("")
                    _show_tip("mapping.start-position")
        elif kind == "ecuc_param":
            _, pname, path = data
            selected["kind"] = kind
            selected["path"] = path
            selected["param"] = pname
            _mod, _cont, param = _resolve_ecuc_path(path)
            if param:
                name_edit.setText(param.name)
                param_edit.setText(param.value)
                defref_lbl.setText(param.definition or "")
                tip_id = item.data(1, Qt.ItemDataRole.UserRole) or (
                    "ecuc.com.ComIPduSize")
                _show_tip(tip_id)
        elif kind in ("ecuc_cont", "ecuc_mod"):
            selected["kind"] = kind
            selected["path"] = data[2]
            name_edit.setText(data[1])
            param_edit.setText("")
            if kind == "ecuc_cont":
                _mod, cont, _p = _resolve_ecuc_path(data[2])
                defref_lbl.setText(cont.definition if cont else "")
            else:
                mod = next(
                    (m for m in document.ecuc.modules if m.name == data[1]),
                    None)
                defref_lbl.setText(mod.definition if mod else "")
            tip.setText(
                "<b>%s</b><br/>ECUC short name. Definition-REF shown below."
                % data[1])
        mute["on"] = False

    def _apply():
        if _is_ecuc():
            if selected["kind"] != "ecuc_param" or not selected["path"]:
                return
            ecuc = document.clone_ecuc()
            # remap path onto clone
            path = selected["path"]
            mod = next((m for m in ecuc.modules if m.name == path[0]), None)
            if not mod:
                return
            containers = mod.containers
            cont = None
            idxs = path[1:]
            i = 0
            while i < len(idxs):
                if idxs[i] == "p":
                    if cont and i + 1 < len(idxs):
                        j = idxs[i + 1]
                        if 0 <= j < len(cont.params):
                            cont.params[j].value = param_edit.text().strip()
                            cont.params[j].name = (
                                name_edit.text().strip()
                                or cont.params[j].name)
                    break
                idx = idxs[i]
                cont = containers[idx]
                containers = cont.children
                i += 1
            document.apply_ecuc(ecuc)
            log_fn("OK", "Applied ECUC param %s" % selected["param"])
            return

        if not selected["pdu"]:
            return
        model = document.clone_model()
        pdu = next((p for p in model.ipdus if p.name == selected["pdu"]), None)
        if not pdu:
            return
        if selected["kind"] == "pdu":
            pdu.name = name_edit.text().strip() or pdu.name
            pdu.can_id = can_spin.value()
            pdu.dlc = dlc_spin.value()
            selected["pdu"] = pdu.name
        else:
            sig = next(
                (s for s in pdu.signals if s.name == selected["signal"]), None)
            if sig:
                sig.name = name_edit.text().strip() or sig.name
                sig.start_bit = start_spin.value()
                sig.length = len_spin.value()
                sig.endian = endian.currentText()
                try:
                    sig.factor = float(factor_edit.text() or 1)
                except ValueError:
                    pass
                try:
                    sig.offset = float(offset_edit.text() or 0)
                except ValueError:
                    pass
                sig.unit = unit_edit.text().strip()
                selected["signal"] = sig.name
        document.apply_model(model)
        log_fn("OK", "Applied %s" % (selected["signal"] or selected["pdu"]))

    def _add_pdu():
        if _is_ecuc():
            return
        model = document.clone_model()
        n = 1
        names = {p.name for p in model.ipdus}
        while "NewPdu%d" % n in names:
            n += 1
        model.ipdus.append(arxmlparse.Ipdu(
            "NewPdu%d" % n, 0x200 + n, 8,
            [arxmlparse.Signal("Sig0", 0, 8)]))
        document.apply_model(model)
        select_target("NewPdu%d" % n, "")

    def _add_sig():
        if _is_ecuc():
            return
        if not selected["pdu"]:
            log_fn("WARN", "Select a PDU first")
            return
        model = document.clone_model()
        pdu = next((p for p in model.ipdus if p.name == selected["pdu"]), None)
        if not pdu:
            return
        used = {s.name for s in pdu.signals}
        i = 0
        while "Sig%d" % i in used:
            i += 1
        start = 0
        if pdu.signals:
            last = pdu.signals[-1]
            start = last.start_bit + last.length
        pdu.signals.append(arxmlparse.Signal("Sig%d" % i, start, 8))
        document.apply_model(model)
        select_target(pdu.name, "Sig%d" % i)

    def _remove():
        if _is_ecuc() or not selected["pdu"]:
            return
        model = document.clone_model()
        if selected["kind"] == "pdu":
            model.ipdus = [p for p in model.ipdus if p.name != selected["pdu"]]
        else:
            pdu = next(
                (p for p in model.ipdus if p.name == selected["pdu"]), None)
            if pdu:
                pdu.signals = [
                    s for s in pdu.signals if s.name != selected["signal"]]
        selected["kind"] = selected["pdu"] = selected["signal"] = None
        document.apply_model(model)

    def select_target(pdu: str, signal: str = ""):
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            data = item.data(0, Qt.ItemDataRole.UserRole)
            if not data or data[0] not in ("pdu", "signal"):
                continue
            kind, pn, sn = data
            if not signal and pn == pdu:
                tree.setCurrentItem(item)
                return
            for j in range(item.childCount()):
                ch = item.child(j)
                _k, pn2, sn2 = ch.data(0, Qt.ItemDataRole.UserRole)
                if pn2 == pdu and sn2 == signal:
                    tree.setCurrentItem(ch)
                    return

    def _on_mode(text: str):
        document.set_editor_mode("ecuc" if text == "ECUC" else "com")

    tree.itemSelectionChanged.connect(_on_select)
    apply_btn.clicked.connect(_apply)
    add_pdu.clicked.connect(_add_pdu)
    add_sig.clicked.connect(_add_sig)
    remove_btn.clicked.connect(_remove)
    mode.currentTextChanged.connect(_on_mode)
    for w, tip_id in (
        (name_edit, "i-signal.short-name"),
        (can_spin, "can-frame-triggering.identifier"),
        (dlc_spin, "i-signal-i-pdu.length"),
        (start_spin, "mapping.start-position"),
        (len_spin, "i-signal.length"),
        (endian, "mapping.packing-byte-order"),
        (param_edit, "ecuc.com.ComIPduSize"),
    ):
        if hasattr(w, "editingFinished"):
            w.editingFinished.connect(_apply)
        if hasattr(w, "valueChanged"):
            w.valueChanged.connect(lambda *_a, tid=tip_id: _show_tip(tid))
        if hasattr(w, "currentIndexChanged"):
            w.currentIndexChanged.connect(
                lambda *_a, tid=tip_id: _show_tip(tid))
    for w in (factor_edit, offset_edit, unit_edit):
        w.editingFinished.connect(_apply)
    document.on_changed(_rebuild)
    _rebuild()

    root.select_target = select_target
    root.apply_from_menu = _apply
    return root
