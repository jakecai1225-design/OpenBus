# -*- coding: utf-8 -*-
"""Diagnose workspace — services, DID, DTC, security access, flash helper."""

from __future__ import annotations

import json
import os
import time

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QAbstractItemView, QCheckBox, QComboBox, QDialog, QDialogButtonBox,
    QFileDialog, QFormLayout, QGroupBox, QHBoxLayout, QHeaderView,
    QInputDialog, QLabel, QLineEdit, QMessageBox, QProgressBar, QPushButton,
    QSpinBox, QTabWidget, QTableWidget, QTableWidgetItem, QTreeWidget,
    QTreeWidgetItem, QVBoxLayout, QWidget,
)

from core import (
    SESSIONS, DTC_STATUS_BITS, dtc_to_text, dtc_status_text,
    calc_key_demo, calc_key_expr, calc_key_file,
    encode_10, encode_11, encode_14, encode_19, encode_22, encode_2e,
    encode_27, encode_28, encode_2f, encode_31, encode_34, encode_36,
    encode_37, encode_3e, encode_85,
)

_HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIDS_FILE = os.path.join(_HERE, "core", "dids.json")

BUILTIN_DIDS = {
    "F190": {"name": "VIN", "type": "ascii"},
    "F18C": {"name": "ECU assembly date (BCD)", "type": "hex"},
    "F191": {"name": "Boot software version", "type": "ascii"},
    "F192": {"name": "System supplier identifier", "type": "ascii"},
    "F193": {"name": "Active diagnostic session", "type": "u8"},
    "F194": {"name": "Vehicle manufacturer", "type": "ascii"},
    "F195": {"name": "System name", "type": "ascii"},
    "F196": {"name": "ECU software version", "type": "ascii"},
    "F197": {"name": "ECU part number", "type": "ascii"},
    "F198": {"name": "ECU serial number", "type": "ascii"},
    "0100": {"name": "Vehicle info", "type": "hex"},
}

DID_TYPES = ["hex", "ascii", "u8", "u16", "u32"]

SERVICE_TREE = [
    ("Session & control", [
        (0x10, "10 DiagnosticSessionControl"),
        (0x11, "11 ECUReset"),
        (0x3E, "3E TesterPresent"),
        (0x28, "28 CommunicationControl"),
        (0x85, "85 ControlDTCSetting"),
    ]),
    ("Data R/W", [
        (0x22, "22 ReadDataByIdentifier"),
        (0x2E, "2E WriteDataByIdentifier"),
        (0x2F, "2F InputOutputControlByIdentifier"),
    ]),
    ("DTC", [
        (0x14, "14 ClearDiagnosticInformation"),
        (0x19, "19 ReadDTCInformation"),
    ]),
    ("Security", [
        (0x27, "27 SecurityAccess"),
    ]),
    ("Routine & flash", [
        (0x31, "31 RoutineControl"),
        (0x34, "34 RequestDownload"),
        (0x36, "36 TransferData"),
        (0x37, "37 RequestTransferExit"),
    ]),
    ("Advanced", [
        (0x00, "Raw request (hex)"),
    ]),
]


def _decode_did(dtype, data):
    hex_str = " ".join("%02X" % b for b in data)
    if dtype == "ascii":
        return hex_str, data.decode("ascii", "replace").rstrip("\x00 ")
    if dtype == "u8":
        return hex_str, str(data[0]) if data else "-"
    if dtype == "u16" and len(data) >= 2:
        return hex_str, str(int.from_bytes(data[:2], "big"))
    if dtype == "u32" and len(data) >= 4:
        return hex_str, str(int.from_bytes(data[:4], "big"))
    return hex_str, hex_str


def _encode_did_value(dtype, text):
    if dtype == "ascii":
        return text.encode("ascii")
    if dtype == "u8":
        return int(text, 0).to_bytes(1, "big")
    if dtype == "u16":
        return int(text, 0).to_bytes(2, "big")
    if dtype == "u32":
        return int(text, 0).to_bytes(4, "big")
    return bytes.fromhex(text.replace(" ", ""))


def _fit_len(key, n):
    if len(key) >= n:
        return key[-n:]
    return b"\x00" * (n - len(key)) + key


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(4, 4, 4, 4)

    def uds_send(pdu, on_done=None, expect_response=True, tag="Request"):
        session.request(pdu, on_done=on_done, expect_response=expect_response, tag=tag)

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    # ========== Tab 1: Services ==========
    svc_tab = QWidget()
    svc_h = QHBoxLayout(svc_tab)
    svc_tree = QTreeWidget()
    svc_tree.setHeaderLabel("Services")
    for cat, items in SERVICE_TREE:
        cat_item = QTreeWidgetItem([cat])
        cat_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
        f = QFont()
        f.setBold(True)
        cat_item.setFont(0, f)
        svc_tree.addTopLevelItem(cat_item)
        for sid, name in items:
            it = QTreeWidgetItem([name])
            it.setData(0, Qt.ItemDataRole.UserRole, sid)
            cat_item.addChild(it)
    svc_tree.expandAll()

    svc_right = QWidget()
    svc_right_v = QVBoxLayout(svc_right)
    svc_title = QLabel("Select a service")
    svc_title.setStyleSheet("font-weight:bold;")
    svc_param_box = QGroupBox("Parameters")
    svc_form = QFormLayout(svc_param_box)
    svc_send_btn = QPushButton("Send request")
    svc_send_btn.setMinimumHeight(32)
    svc_resp_label = QLabel("Last response: —")
    svc_resp_label.setWordWrap(True)
    svc_resp_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
    svc_right_v.addWidget(svc_title)
    svc_right_v.addWidget(svc_param_box)
    svc_right_v.addWidget(svc_send_btn)
    svc_right_v.addWidget(QLabel("Response:"))
    svc_right_v.addWidget(svc_resp_label)
    svc_right_v.addStretch()
    svc_h.addWidget(svc_tree, 1)
    svc_h.addWidget(svc_right, 2)

    _svc_maker = [None]
    _svc_expect = [True]

    def _clear_form():
        while svc_form.count():
            item = svc_form.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()

    def _hex_edit(placeholder, default=""):
        e = QLineEdit(default)
        e.setPlaceholderText(placeholder)
        return e

    def _parse_hex(text):
        return bytes.fromhex(text.replace(" ", "").replace("0x", ""))

    def _parse_did(text):
        b = _parse_hex(text)
        if len(b) != 2:
            raise ValueError("DID must be 2-byte hex (e.g. F190)")
        return int.from_bytes(b, "big")

    def _parse_n(text, n, what):
        b = _parse_hex(text)
        if len(b) != n:
            raise ValueError("%s must be %d-byte hex" % (what, n))
        return b

    def _f_10():
        c = QComboBox()
        for k, v in SESSIONS.items():
            c.addItem(v, k)
        svc_form.addRow("Target session:", c)
        return lambda: encode_10(c.currentData())

    def _f_11():
        c = QComboBox()
        for k, v in ((0x01, "01 hardReset"), (0x02, "02 keyOffOnReset"),
                     (0x03, "03 softReset")):
            c.addItem(v, k)
        svc_form.addRow("Reset type:", c)
        return lambda: encode_11(c.currentData())

    def _f_3e():
        c = QComboBox()
        c.addItem("80 suppress positive (recommended)", 0x80)
        c.addItem("00 require response", 0x00)
        svc_form.addRow("Sub-function:", c)

        def sync_expect(*_a):
            _svc_expect[0] = c.currentData() != 0x80

        c.currentIndexChanged.connect(sync_expect)
        sync_expect()
        return lambda: encode_3e(c.currentData())

    def _f_28():
        cc = QComboBox()
        for k, v in ((0x00, "00 enable Rx+Tx"), (0x01, "01 enable Rx disable Tx"),
                     (0x02, "02 disable Rx enable Tx"), (0x03, "03 disable Rx+Tx")):
            cc.addItem(v, k)
        ct = QComboBox()
        for k, v in ((0x01, "01 normal"), (0x02, "02 network management"),
                     (0x03, "03 normal+NM")):
            ct.addItem(v, k)
        svc_form.addRow("Control:", cc)
        svc_form.addRow("Comm type:", ct)
        return lambda: encode_28(cc.currentData(), ct.currentData())

    def _f_85():
        c = QComboBox()
        c.addItem("01 on", 0x01)
        c.addItem("02 off", 0x02)
        svc_form.addRow("Sub-function:", c)
        return lambda: encode_85(c.currentData())

    def _f_22():
        e = _hex_edit("DID hex, e.g. F190", "F190")
        svc_form.addRow("DID:", e)
        return lambda: encode_22(_parse_did(e.text()))

    def _f_2e():
        d = _hex_edit("DID hex", "F187")
        v = _hex_edit("Data hex")
        svc_form.addRow("DID:", d)
        svc_form.addRow("Data:", v)
        return lambda: encode_2e(_parse_did(d.text()), _parse_hex(v.text()))

    def _f_2f():
        d = _hex_edit("DID hex", "0B7A")
        c = QComboBox()
        for k, v in ((0x01, "01 returnControlToECU"), (0x03, "03 resetToDefault"),
                     (0x04, "04 freezeCurrentState"),
                     (0x00, "0X shortTermAdjustment (needs data)")):
            c.addItem(v, k)
        v = _hex_edit("Control data hex")
        svc_form.addRow("DID:", d)
        svc_form.addRow("Control:", c)
        svc_form.addRow("Data:", v)

        def make():
            data = _parse_hex(v.text()) if v.text().strip() else b""
            return encode_2f(_parse_did(d.text()), c.currentData(), data)

        return make

    def _f_14():
        e = _hex_edit("DTC group 3 bytes (FFFFFF=all)", "FFFFFF")
        svc_form.addRow("DTC group:", e)
        return lambda: bytes([0x14]) + _parse_n(e.text(), 3, "DTC group")

    def _f_19():
        c = QComboBox()
        for k, v in ((0x01, "01 reportNumberOfDTCByStatusMask"),
                     (0x02, "02 reportDTCByStatusMask"),
                     (0x04, "04 reportDTCSnapshotByDTCNumber"),
                     (0x0A, "0A reportSupportedDTC")):
            c.addItem(v, k)
        m = QSpinBox()
        m.setRange(0, 0xFF)
        m.setDisplayIntegerBase(16)
        m.setPrefix("0x")
        m.setValue(0xFF)
        e = _hex_edit("DTC number 3 bytes (sub 04)", "FFFFFF")
        svc_form.addRow("Sub-function:", c)
        svc_form.addRow("Status mask:", m)
        svc_form.addRow("DTC number:", e)

        def make():
            sub = c.currentData()
            if sub == 0x04:
                return bytes([0x19, 0x04]) + _parse_n(e.text(), 3, "DTC number")
            return encode_19(sub, m.value())

        return make

    def _f_27():
        c = QComboBox()
        for lv in (0x01, 0x03, 0x05, 0x07, 0x09, 0x0B):
            c.addItem("%02X requestSeed level%d" % (lv, (lv + 1) // 2), lv)
            c.addItem("%02X sendKey level%d" % (lv + 1, (lv + 1) // 2), lv + 1)
        v = _hex_edit("Key hex (for sendKey)")
        svc_form.addRow("Sub-function:", c)
        svc_form.addRow("Data:", v)

        def make():
            req = encode_27(c.currentData())
            if v.text().strip():
                req += _parse_hex(v.text())
            return req

        return make

    def _f_31():
        c = QComboBox()
        for k, v in ((0x01, "01 startRoutine"), (0x02, "02 stopRoutine"),
                     (0x03, "03 requestRoutineResults")):
            c.addItem(v, k)
        r = _hex_edit("Routine ID hex", "FF01")
        svc_form.addRow("Sub-function:", c)
        svc_form.addRow("Routine ID:", r)
        return lambda: encode_31(c.currentData(), _parse_did(r.text()))

    def _f_34():
        f = QComboBox()
        f.addItem("0x44 addr 4B + size 4B", (4, 4))
        f.addItem("0x22 addr 2B + size 2B", (2, 2))
        a = _hex_edit("Start address hex", "08040000")
        s = QSpinBox()
        s.setRange(1, 0x7FFFFFFF)
        s.setValue(0x10000)
        svc_form.addRow("Format:", f)
        svc_form.addRow("Start address:", a)
        svc_form.addRow("Byte count:", s)

        def make():
            al, sl = f.currentData()
            addr = int.from_bytes(_parse_n(a.text(), al, "address"), "big")
            return encode_34(addr, s.value(), al, sl)

        return make

    def _f_36():
        c = QSpinBox()
        c.setRange(0, 0xFF)
        c.setValue(1)
        d = _hex_edit("Block data hex")
        svc_form.addRow("Block counter:", c)
        svc_form.addRow("Data:", d)
        return lambda: encode_36(c.value(), _parse_hex(d.text()))

    def _f_37():
        svc_form.addRow("Params:", QLabel("none — RequestTransferExit"))
        return encode_37

    def _f_raw():
        e = _hex_edit("Full request hex, e.g. 22 F1 90", "22 F1 90")
        svc_form.addRow("Request:", e)
        return lambda: _parse_hex(e.text())

    FORMS = {0x10: _f_10, 0x11: _f_11, 0x3E: _f_3e, 0x28: _f_28, 0x85: _f_85,
             0x22: _f_22, 0x2E: _f_2e, 0x2F: _f_2f, 0x14: _f_14, 0x19: _f_19,
             0x27: _f_27, 0x31: _f_31, 0x34: _f_34, 0x36: _f_36, 0x37: _f_37,
             0x00: _f_raw}
    NAMES = {sid: name for _, items in SERVICE_TREE for sid, name in items}

    def _on_service_selected(item):
        if item is None:
            return
        sid = item.data(0, Qt.ItemDataRole.UserRole)
        if sid is None:
            return
        _clear_form()
        svc_title.setText(NAMES.get(sid, str(sid)))
        _svc_expect[0] = True
        _svc_maker[0] = FORMS[sid]()

    svc_tree.currentItemChanged.connect(lambda cur, _p: _on_service_selected(cur))

    def _on_svc_send():
        if not _svc_maker[0]:
            return
        try:
            pdu = _svc_maker[0]()
        except (ValueError, Exception) as e:
            log_fn("ERR", "-", b"", "Parameter error: %s" % e)
            return
        if not pdu:
            log_fn("ERR", "-", b"", "Empty request")
            return

        def cb(ok, resp, note):
            svc_resp_label.setText(note if note else ("Sent" if ok else "Failed"))
            if ok and resp:
                svc_resp_label.setText(
                    (note or "") + "\n" + " ".join("%02X" % b for b in resp))

        uds_send(pdu, cb, expect_response=_svc_expect[0],
                 tag=NAMES.get(pdu[0] if pdu else 0, "Request"))

    svc_send_btn.clicked.connect(_on_svc_send)
    tabs.addTab(svc_tab, "Services")

    # ========== Tab 2: DID ==========
    did_tab = QWidget()
    did_v = QVBoxLayout(did_tab)
    did_table = QTableWidget(0, 6)
    did_table.setHorizontalHeaderLabels(
        ["DID", "Name", "Type", "Raw", "Decoded", "Period(ms)"])
    did_table.verticalHeader().setVisible(False)
    did_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    did_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    did_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
    did_v.addWidget(did_table, 1)

    did_btn_row = QHBoxLayout()
    did_add = QPushButton("Add")
    did_del = QPushButton("Remove")
    did_read_all = QPushButton("Read all")
    did_poll_check = QCheckBox("Periodic poll")
    did_write_edit = QLineEdit()
    did_write_edit.setPlaceholderText("Write value (selected row)")
    did_write_btn = QPushButton("Write selected")
    for w in (did_add, did_del, did_read_all, did_poll_check):
        did_btn_row.addWidget(w)
    did_btn_row.addStretch()
    did_btn_row.addWidget(did_write_edit, 2)
    did_btn_row.addWidget(did_write_btn)
    did_v.addLayout(did_btn_row)

    _did_rows = []

    def _load_dids():
        merged = {}
        merged.update(BUILTIN_DIDS)
        try:
            with open(DIDS_FILE, encoding="utf-8-sig") as f:
                data = json.load(f)
            if isinstance(data, dict):
                for h in data.get("disabled", []):
                    merged.pop(h, None)
                merged.update(data.get("custom", {}))
        except (OSError, json.JSONDecodeError):
            pass
        return merged

    def _save_dids():
        custom = {}
        disabled = []
        kept = {"%04X" % r["did"] for r in _did_rows}
        for did_hex in BUILTIN_DIDS:
            if did_hex not in kept:
                disabled.append(did_hex)
        for r in _did_rows:
            did_hex = "%04X" % r["did"]
            if did_hex not in BUILTIN_DIDS:
                custom[did_hex] = {"name": r["name"], "type": r["type"]}
        try:
            with open(DIDS_FILE, "w", encoding="utf-8") as f:
                json.dump({"custom": custom, "disabled": disabled}, f,
                          ensure_ascii=False, indent=2)
        except OSError as e:
            log_fn("ERR", "-", b"", "dids.json save failed: %s" % e)

    def _refresh_did_table():
        did_table.setRowCount(0)
        for r in _did_rows:
            raw, val = (_decode_did(r["type"], r["value"])
                        if r["value"] else ("-", "-"))
            row = did_table.rowCount()
            did_table.insertRow(row)
            for col, text in enumerate((
                    "0x%04X" % r["did"], r["name"], r["type"],
                    raw, val, str(r["period"] or "-"))):
                did_table.setItem(row, col, QTableWidgetItem(text))

    def _init_did_rows():
        _did_rows.clear()
        for did_hex, info in _load_dids().items():
            try:
                _did_rows.append({
                    "did": int(did_hex, 16),
                    "name": info.get("name", did_hex),
                    "type": info.get("type", "hex"),
                    "period": 0, "value": None,
                })
            except ValueError:
                continue
        _did_rows.sort(key=lambda r: r["did"])
        _refresh_did_table()

    _init_did_rows()

    def _did_add_fn():
        dlg = QDialog(root)
        dlg.setWindowTitle("Add DID")
        form = QFormLayout(dlg)
        de = QLineEdit()
        de.setPlaceholderText("4-digit hex, e.g. F1A0")
        ne = QLineEdit()
        ne.setPlaceholderText("Name")
        tc = QComboBox()
        tc.addItems(DID_TYPES)
        form.addRow("DID:", de)
        form.addRow("Name:", ne)
        form.addRow("Type:", tc)
        bb = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        form.addRow(bb)
        bb.accepted.connect(dlg.accept)
        bb.rejected.connect(dlg.reject)
        if dlg.exec() == QDialog.DialogCode.Accepted:
            try:
                did = int(de.text().strip(), 16)
                if not 0 <= did <= 0xFFFF:
                    raise ValueError
            except ValueError:
                log_fn("ERR", "-", b"", "DID must be 4-digit hex")
                return
            if any(r["did"] == did for r in _did_rows):
                log_fn("ERR", "-", b"", "DID already exists")
                return
            _did_rows.append({
                "did": did, "name": ne.text().strip() or "0x%04X" % did,
                "type": tc.currentText(), "period": 0, "value": None,
            })
            _did_rows.sort(key=lambda r: r["did"])
            _save_dids()
            _refresh_did_table()

    def _did_del_fn():
        sel = did_table.currentRow()
        if 0 <= sel < len(_did_rows):
            _did_rows.pop(sel)
            _save_dids()
            _refresh_did_table()

    def _did_read(did):
        def cb(ok, resp, note):
            if ok and resp and resp[:1] == b"\x62" and len(resp) >= 3:
                rid = int.from_bytes(resp[1:3], "big")
                for r in _did_rows:
                    if r["did"] == rid:
                        r["value"] = bytes(resp[3:])
                        break
                _refresh_did_table()

        uds_send(encode_22(did), cb, tag="Read DID 0x%04X" % did)

    def _did_read_all_fn():
        queue = [r["did"] for r in _did_rows]

        def next_one():
            if not queue or not session.isotp._alive:
                return
            if session.client.busy or session.flashing:
                QTimer.singleShot(100, next_one)
                return
            _did_read(queue.pop(0))
            QTimer.singleShot(60, next_one)

        next_one()

    def _did_write_fn():
        sel = did_table.currentRow()
        if not (0 <= sel < len(_did_rows)) or not did_write_edit.text().strip():
            return
        r = _did_rows[sel]
        try:
            data = _encode_did_value(r["type"], did_write_edit.text().strip())
        except (ValueError, UnicodeEncodeError) as e:
            log_fn("ERR", "-", b"", "Invalid write value: %s" % e)
            return
        uds_send(encode_2e(r["did"], data), tag="Write DID 0x%04X" % r["did"])

    def _on_did_double(row, col):
        if col != 5 or not (0 <= row < len(_did_rows)):
            return
        r = _did_rows[row]
        text, okk = QInputDialog.getInt(
            root, "Poll period",
            "DID 0x%04X period ms (0=off):" % r["did"],
            r["period"] or 1000, 0, 60000, 100)
        if okk:
            r["period"] = text
            _refresh_did_table()

    did_add.clicked.connect(_did_add_fn)
    did_del.clicked.connect(_did_del_fn)
    did_read_all.clicked.connect(_did_read_all_fn)
    did_write_btn.clicked.connect(_did_write_fn)
    did_table.cellDoubleClicked.connect(_on_did_double)

    _did_last_read = {}

    def _did_poll_tick():
        if not did_poll_check.isChecked() or session.flashing or session.client.busy:
            return
        now = time.time()
        for r in _did_rows:
            if r["period"] <= 0:
                continue
            last = _did_last_read.get(r["did"], 0)
            if now - last >= r["period"] / 1000.0 and not session.client.busy:
                _did_last_read[r["did"]] = now
                _did_read(r["did"])
                if session.client.busy:
                    break

    did_poll_timer = QTimer(root)
    did_poll_timer.setInterval(200)
    did_poll_timer.timeout.connect(_did_poll_tick)
    did_poll_timer.start(200)
    tabs.addTab(did_tab, "DID")

    # ========== Tab 3: DTC ==========
    dtc_tab = QWidget()
    dtc_v = QVBoxLayout(dtc_tab)
    mask_box = QGroupBox("Status mask (reportDTCByStatusMask 19 02)")
    mask_row = QHBoxLayout(mask_box)
    mask_checks = {}
    for bit, name in DTC_STATUS_BITS.items():
        cb = QCheckBox(name)
        cb.setToolTip(name)
        if bit in (0x01, 0x08):
            cb.setChecked(True)
        mask_row.addWidget(cb)
        mask_checks[bit] = cb
    mask_row.addStretch()

    dtc_btn_row = QHBoxLayout()
    dtc_read_btn = QPushButton("Read DTC list (19 02)")
    dtc_cnt_btn = QPushButton("Read DTC count (19 01)")
    dtc_clear_btn = QPushButton("Clear DTC (14 FF FF FF)")
    dtc_btn_row.addWidget(dtc_read_btn)
    dtc_btn_row.addWidget(dtc_cnt_btn)
    dtc_btn_row.addStretch()
    dtc_btn_row.addWidget(dtc_clear_btn)

    dtc_table = QTableWidget(0, 4)
    dtc_table.setHorizontalHeaderLabels(
        ["DTC", "Raw bytes", "Status", "Status bits"])
    dtc_table.verticalHeader().setVisible(False)
    dtc_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    dtc_table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    dtc_v.addWidget(mask_box)
    dtc_v.addLayout(dtc_btn_row)
    dtc_v.addWidget(dtc_table, 1)

    def _mask_value():
        v = 0
        for bit, cb in mask_checks.items():
            if cb.isChecked():
                v |= bit
        return v

    def _on_dtc_read():
        mask = _mask_value()

        def cb(ok, resp, note):
            if not (ok and resp and resp[:2] == b"\x59\x02"):
                return
            dtc_table.setRowCount(0)
            body = resp[3:]
            count = 0
            for i in range(0, len(body) - 2, 3):
                st = body[i + 2]
                row = dtc_table.rowCount()
                dtc_table.insertRow(row)
                raw = " ".join("%02X" % b for b in body[i:i + 3])
                for col, text in enumerate((
                        dtc_to_text(body[i:i + 2]), raw,
                        "0x%02X" % st, dtc_status_text(st))):
                    item = QTableWidgetItem(text)
                    if col == 0 and (st & 0x01):
                        item.setForeground(QColor("#C62828"))
                    dtc_table.setItem(row, col, item)
                count += 1
            log_fn("RX", session.rx_id, resp, "19 02 parsed %d DTCs" % count)

        uds_send(encode_19(0x02, mask), cb, tag="Read DTC mask 0x%02X" % mask)

    def _on_dtc_count():
        def cb(ok, resp, note):
            if ok and resp and resp[:2] == b"\x59\x01" and len(resp) >= 5:
                n = int.from_bytes(resp[3:5], "big")
                QMessageBox.information(
                    root, "DTC count",
                    "Status availability 0x%02X\nMatching DTCs: %d" % (resp[2], n))

        uds_send(encode_19(0x01, _mask_value()), cb, tag="Read DTC count")

    def _on_dtc_clear():
        reply = QMessageBox.question(
            root, "Clear DTC",
            "Clear all DTCs (14 FF FF FF)?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply != QMessageBox.StandardButton.Yes:
            return

        def cb(ok, resp, note):
            if ok:
                dtc_table.setRowCount(0)
                log_fn("RX", session.rx_id, b"", "Clear DTC OK (54)")

        uds_send(encode_14(0xFFFFFF), cb, tag="Clear DTC")

    dtc_read_btn.clicked.connect(_on_dtc_read)
    dtc_cnt_btn.clicked.connect(_on_dtc_count)
    dtc_clear_btn.clicked.connect(_on_dtc_clear)
    tabs.addTab(dtc_tab, "DTC")

    # ========== Tab 4: Security Access (local algos OK) ==========
    sec_tab = QWidget()
    sec_form = QFormLayout(sec_tab)
    sec_level = QComboBox()
    for lv in (0x01, 0x03, 0x05, 0x07, 0x09, 0x0B):
        sec_level.addItem(
            "level %d (seed %02X / key %02X)" % ((lv + 1) // 2, lv, lv + 1), lv)
    sec_algo = QComboBox()
    sec_algo.addItem("Demo (seed XOR 0xA5...)", "demo")
    sec_algo.addItem("Python expression", "expr")
    sec_algo.addItem("Algo file (calculate_key)", "file")
    sec_expr = QLineEdit("seed ^ 0x11223344")
    sec_expr.setToolTip("Vars: seed (big-endian int) / seed_bytes; return int or bytes")
    sec_file_btn = QPushButton("Choose algo file…")
    sec_file_label = QLabel("None")
    sec_seed_label = QLabel("—")
    sec_key_label = QLabel("—")
    sec_result_label = QLabel("—")
    for l in (sec_seed_label, sec_key_label, sec_result_label):
        l.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        l.setStyleSheet("font-family: Consolas, monospace;")
    sec_seed_btn = QPushButton("Request seed (27 odd)")
    sec_key_btn = QPushButton("Send key (27 even)")
    sec_key_btn.setEnabled(False)
    sec_form.addRow("Security level:", sec_level)
    sec_form.addRow("Key algorithm:", sec_algo)
    sec_form.addRow("Expression:", sec_expr)
    sec_form.addRow("Algo file:", sec_file_btn)
    sec_form.addRow("Current file:", sec_file_label)
    sec_form.addRow("Seed:", sec_seed_label)
    sec_form.addRow("Computed key:", sec_key_label)
    sec_form.addRow("Result:", sec_result_label)
    sec_form.addRow(sec_seed_btn)
    sec_form.addRow(sec_key_btn)

    _sec_state = {"seed": None, "file": None}

    def _on_pick_algo_file():
        path, _ = QFileDialog.getOpenFileName(
            root, "Key algorithm Python file", "", "Python (*.py)")
        if path:
            _sec_state["file"] = path
            sec_file_label.setText(path)

    sec_file_btn.clicked.connect(_on_pick_algo_file)

    def _calc_key(seed):
        mode = sec_algo.currentData()
        if mode == "demo":
            return calc_key_demo(seed, len(seed))
        if mode == "expr":
            key = calc_key_expr(sec_expr.text().strip(), seed)
        else:
            if not _sec_state["file"]:
                raise ValueError("No algo file selected")
            key = calc_key_file(_sec_state["file"], seed)
        return _fit_len(key, len(seed))

    _key_calc_ref = [_calc_key]

    def _on_request_seed():
        lv = sec_level.currentData()
        _sec_state["seed"] = None
        sec_key_btn.setEnabled(False)
        sec_seed_label.setText("Requesting...")
        sec_result_label.setText("—")

        def cb(ok, resp, note):
            if ok and resp and resp[0] == 0x67 and len(resp) > 2:
                seed = bytes(resp[2:])
                _sec_state["seed"] = seed
                sec_seed_label.setText(seed.hex().upper())
                try:
                    key = _calc_key(seed)
                    sec_key_label.setText(key.hex().upper())
                    sec_key_btn.setEnabled(True)
                    sec_result_label.setText("Key ready — send when ready")
                except Exception as e:
                    sec_key_label.setText("—")
                    sec_result_label.setText("Algo failed: %s" % e)
            else:
                sec_seed_label.setText("Failed: " + (note or "no response"))
                sec_result_label.setText("Failed")

        uds_send(encode_27(lv), cb, tag="Request seed 27 %02X" % lv)

    def _on_send_key():
        seed = _sec_state["seed"]
        if not seed:
            return
        try:
            key = _calc_key(seed)
        except Exception as e:
            sec_result_label.setText("Algo failed: %s" % e)
            return
        lv = sec_level.currentData() + 1

        def cb(ok, resp, note):
            if ok and resp and resp[0] == 0x67:
                sec_result_label.setText("Key accepted (unlocked)")
            else:
                sec_result_label.setText("Failed: " + (note or "no response"))

        uds_send(encode_27(lv) + key, cb, tag="Send key 27 %02X" % lv)

    sec_seed_btn.clicked.connect(_on_request_seed)
    sec_key_btn.clicked.connect(_on_send_key)
    tabs.addTab(sec_tab, "Security")

    # ========== Tab 5: Flash ==========
    fl_tab = QWidget()
    fl_v = QVBoxLayout(fl_tab)
    fl_form = QFormLayout()
    fl_file_edit = QLineEdit()
    fl_file_edit.setPlaceholderText("Firmware binary (.bin / raw bytes)")
    fl_file_btn = QPushButton("Browse…")
    fl_file_row = QHBoxLayout()
    fl_file_row.addWidget(fl_file_edit, 1)
    fl_file_row.addWidget(fl_file_btn)
    fl_addr = _hex_edit("Start address hex (4 bytes)", "08040000")
    fl_pre_check = QCheckBox("Precheck (10 02 → 27 unlock → 85 02 → 28 03 03)")
    fl_pre_check.setChecked(True)
    fl_sec_lv = QComboBox()
    for lv in (0x01, 0x03, 0x05):
        fl_sec_lv.addItem("level %d" % ((lv + 1) // 2), lv)
    fl_block_edit = QLineEdit()
    fl_block_edit.setPlaceholderText("Empty = use max block from 34 response")
    fl_form.addRow("Firmware file:", fl_file_row)
    fl_form.addRow("Start address:", fl_addr)
    fl_form.addRow("Precheck:", fl_pre_check)
    fl_form.addRow("Security level:", fl_sec_lv)
    fl_form.addRow("Block size:", fl_block_edit)
    fl_progress = QProgressBar()
    fl_progress.setRange(0, 1000)
    fl_progress.setValue(0)
    fl_status = QLabel("Idle — choose a file then Start")
    fl_status.setStyleSheet("color:#555;")
    fl_btn_row = QHBoxLayout()
    fl_start_btn = QPushButton("Start flash")
    fl_start_btn.setMinimumHeight(34)
    fl_stop_btn = QPushButton("Abort")
    fl_stop_btn.setEnabled(False)
    fl_btn_row.addWidget(fl_start_btn)
    fl_btn_row.addWidget(fl_stop_btn)
    fl_btn_row.addStretch()
    fl_v.addLayout(fl_form)
    fl_v.addLayout(fl_btn_row)
    fl_v.addWidget(fl_progress)
    fl_v.addWidget(fl_status)
    fl_v.addStretch()

    def _on_pick_file():
        path, _ = QFileDialog.getOpenFileName(
            root, "Firmware file", "", "All files (*.*)")
        if path:
            fl_file_edit.setText(path)
            try:
                size = os.path.getsize(path)
                fl_status.setText("Selected %d bytes (0x%X)" % (size, size))
            except OSError:
                pass

    fl_file_btn.clicked.connect(_on_pick_file)

    _flash = {"state": "idle", "data": b"", "addr": 0, "maxblk": 0, "pos": 0,
              "block": 0, "size": 0, "t0": 0}

    def _fl_set(status, pct=None):
        fl_status.setText(status)
        if pct is not None:
            fl_progress.setValue(int(pct * 1000))

    def _fl_fail(msg):
        _flash["state"] = "idle"
        session.flashing = False
        fl_start_btn.setEnabled(True)
        fl_stop_btn.setEnabled(False)
        _fl_set("Failed: %s" % msg)
        log_fn("ERR", "-", b"", "Flash failed: %s" % msg)

    def _fl_done():
        _flash["state"] = "idle"
        session.flashing = False
        fl_start_btn.setEnabled(True)
        fl_stop_btn.setEnabled(False)
        cost = time.time() - _flash["t0"]
        _fl_set("Flash done (%.1fs, %d bytes)" % (cost, _flash["size"]), 1.0)
        session.set_session_name(SESSIONS[0x02])

    def _fl_send(req, state, tag):
        def cb(ok, resp, note):
            if _flash["state"] == "idle":
                return
            if not ok:
                _fl_fail("%s: %s" % (tag, note))
                return
            _fl_step(state, resp)

        uds_send(req, cb, tag="[Flash] %s" % tag)

    def _fl_step(state, resp):
        st = _flash["state"] = state
        if st == "session":
            _fl_set("Precheck: programming session...")
            _fl_send(encode_10(0x02), "sec", "10 02")
        elif st == "sec":
            if _flash.get("precheck"):
                lv = fl_sec_lv.currentData()
                _fl_set("Precheck: request seed...")

                def seed_cb(ok, r, note):
                    if not ok:
                        _fl_fail("27 seed: %s" % note)
                        return
                    if r and r[0] == 0x67 and len(r) > 2:
                        seed = bytes(r[2:])
                        try:
                            key = _key_calc_ref[0](seed)
                        except Exception as e:
                            _fl_fail("Key algo: %s" % e)
                            return
                        _fl_send(encode_27(lv + 1) + key, "dtc_off",
                                 "27 %02X key" % (lv + 1))
                    else:
                        _fl_fail("27 did not return seed")

                uds_send(encode_27(lv), seed_cb, tag="[Flash] 27 seed")
            else:
                _fl_step("dtc_off", None)
        elif st == "dtc_off":
            if _flash.get("precheck"):
                _fl_set("Precheck: DTC off...")
                _fl_send(encode_85(0x02), "comm_off", "85 02")
            else:
                _fl_step("comm_off", None)
        elif st == "comm_off":
            if _flash.get("precheck"):
                _fl_set("Precheck: communication off...")
                _fl_send(encode_28(0x03, 0x03), "reqdl", "28 03 03")
            else:
                _fl_step("reqdl", None)
        elif st == "reqdl":
            _fl_set("RequestDownload 34...")
            _fl_send(encode_34(_flash["addr"], len(_flash["data"])),
                     "transfer", "34 RequestDownload")
        elif st == "transfer":
            if not (resp and resp[0] == 0x74 and len(resp) >= 3):
                _fl_fail("34 response format error")
                return
            n = resp[1] >> 4
            maxblk = int.from_bytes(resp[2:2 + n], "big") if n else 0
            if fl_block_edit.text().strip():
                try:
                    maxblk = int(fl_block_edit.text(), 0)
                except ValueError:
                    _fl_fail("Invalid block size")
                    return
            if maxblk <= 2:
                _fl_fail("Max block length too small: %d" % maxblk)
                return
            _flash["maxblk"] = maxblk
            _flash["pos"] = 0
            _flash["block"] = 1
            _flash["state"] = "data"
            _fl_set("Transferring (block %dB)..." % maxblk, 0.0)
            _fl_next_block()
        elif st == "check":
            _fl_set("Integrity check 31 01 FF01...")
            _fl_send(encode_31(0x01, 0xFF01), "exit_ok", "31 01 FF01")
        elif st == "exit_ok":
            _fl_send(encode_37(), "done", "37 RequestTransferExit")
        elif st == "done":
            _fl_done()

    def _fl_next_block():
        if _flash["state"] != "data":
            return
        pos, data = _flash["pos"], _flash["data"]
        chunk = data[pos:pos + _flash["maxblk"] - 2]
        if not chunk:
            _flash["state"] = "check"
            _fl_step("check", None)
            return
        counter = _flash["block"]
        _flash["block"] = (counter + 1) & 0xFF

        def cb(ok, resp, note):
            if _flash["state"] != "data":
                return
            if not ok:
                _fl_fail("36 block %d: %s" % (counter, note))
                return
            _flash["pos"] = pos + len(chunk)
            total = len(data)
            _fl_set("Transferring %d/%d bytes, block %d" % (
                pos + len(chunk), total, counter),
                (pos + len(chunk)) / total)
            _fl_next_block()

        uds_send(encode_36(counter, chunk), cb,
                 tag="[Flash] 36 block %d (%dB)" % (counter, len(chunk)))

    def _on_flash_start():
        if _flash["state"] != "idle":
            return
        path = fl_file_edit.text().strip()
        if not path or not os.path.isfile(path):
            QMessageBox.warning(root, "Flash", "Choose a valid firmware file")
            return
        reply = QMessageBox.question(
            root, "Flash",
            "Start programming download to the ECU?\nThis can brick the device if misconfigured.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply != QMessageBox.StandardButton.Yes:
            return
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            QMessageBox.warning(root, "Flash", "Read failed: %s" % e)
            return
        if not data:
            QMessageBox.warning(root, "Flash", "Firmware file is empty")
            return
        try:
            addr = int.from_bytes(_parse_n(fl_addr.text(), 4, "start address"), "big")
        except ValueError as e:
            QMessageBox.warning(root, "Flash", str(e))
            return
        _flash.update({
            "data": data, "addr": addr, "size": len(data),
            "pos": 0, "block": 1, "maxblk": 0,
            "precheck": fl_pre_check.isChecked(), "t0": time.time(),
        })
        session.flashing = True
        fl_start_btn.setEnabled(False)
        fl_stop_btn.setEnabled(True)
        fl_progress.setValue(0)
        log_fn("TX", session.tx_id, b"",
               "Flash start: %s (%dB @ 0x%X)" % (path, len(data), addr))
        _fl_step("session", None)

    def _on_flash_stop():
        if _flash["state"] != "idle":
            session.client.cancel()
            _flash["state"] = "idle"
            session.flashing = False
            fl_start_btn.setEnabled(True)
            fl_stop_btn.setEnabled(False)
            _fl_set("Aborted")

    fl_start_btn.clicked.connect(_on_flash_start)
    fl_stop_btn.clicked.connect(_on_flash_stop)
    tabs.addTab(fl_tab, "Flash")

    return root
