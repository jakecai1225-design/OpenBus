# -*- coding: utf-8 -*-
"""Diagnose workspace — services, DID, DTC, security access, flash helper."""

from __future__ import annotations

import json
import os
import time

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QColor, QFont, QGuiApplication
from PyQt6.QtWidgets import (
    QAbstractItemView, QCheckBox, QComboBox, QDialog, QDialogButtonBox,
    QFileDialog, QFormLayout, QHBoxLayout, QHeaderView,
    QInputDialog, QLabel, QLineEdit, QMenu, QMessageBox, QProgressBar, QPushButton,
    QSizePolicy, QTableWidget, QTableWidgetItem, QTreeWidget,
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


def _leaf(pages: dict, key: str, widget: QWidget, tip: str = "") -> None:
    """Register a Diagnose sidebar leaf (no chrome Tab strip)."""
    wrap = QWidget()
    v = QVBoxLayout(wrap)
    v.setContentsMargins(12, 8, 12, 8)
    v.setSpacing(0)
    v.addWidget(widget, 1)
    if tip:
        wrap.setToolTip(tip)
    pages[key] = wrap


def build(parent, session, log_fn) -> QWidget:
    from pages import _ui
    from widgets.layout import page
    from widgets.step_spin import StepSpin
    from widgets.session_tab import build as build_session_tab
    from _shared import vscode_theme, codicons, plugin_shell

    # Holder for dialogs / leaf_pages only — never mount into the window.
    # Parenting to AppShell leaves a 100x30 ghost at (0,0) that blocks the menubar.
    root, layout = page(None)
    root.hide()
    root.setAttribute(Qt.WidgetAttribute.WA_DontShowOnScreen, True)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)
    # Leaves are mounted into the shell Diagnose workspace stack.
    leaf_pages: dict = {}

    def uds_send(pdu, on_done=None, expect_response=True, tag="Request"):
        session.request(pdu, on_done=on_done, expect_response=expect_response, tag=tag)

    # ========== Leaf: Session ==========
    session_page = build_session_tab(parent, session, log_fn)
    _leaf(leaf_pages, "session", session_page,
          "TX/RX IDs, diagnostic session, keep-alive, timing")

    # ========== Tab 1: Services ==========
    svc_tab = QWidget()
    svc_h = QHBoxLayout(svc_tab)
    svc_h.setContentsMargins(0, 0, 0, 0)
    svc_h.setSpacing(6)
    svc_tree = QTreeWidget()
    svc_tree.setHeaderLabel("Services")
    svc_tree.setMinimumWidth(240)
    svc_tree.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Expanding)
    _ui.style_tree(svc_tree, header_hidden=True)
    for cat, items in SERVICE_TREE:
        cat_item = QTreeWidgetItem([cat])
        cat_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
        f = QFont(svc_tree.font())
        f.setBold(True)
        f.setPixelSize(12)
        cat_item.setFont(0, f)
        svc_tree.addTopLevelItem(cat_item)
        for sid, name in items:
            it = QTreeWidgetItem([name])
            it.setData(0, Qt.ItemDataRole.UserRole, sid)
            cat_item.addChild(it)
    svc_tree.expandAll()

    req_host = QWidget()
    req_body = QVBoxLayout(req_host)
    req_body.setContentsMargins(4, 0, 0, 0)
    req_body.setSpacing(6)
    svc_title = QLabel("Select a service")
    svc_title.setObjectName("SuiteSectionTitle")
    svc_param_box = QWidget()
    svc_form = QFormLayout(svc_param_box)
    vscode_theme.tune_form(svc_form)

    def _form_row(label: str, widget: QWidget) -> None:
        lab = QLabel(label)
        lab.setObjectName("SuiteFieldLabel")
        svc_form.addRow(lab, widget)
    svc_send_btn = QPushButton("Send")
    svc_send_btn.setObjectName("PrimaryButton")
    svc_send_btn.setFixedSize(84, 28)
    svc_send_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    codicons.set_button(svc_send_btn, "send", primary=True)
    svc_resp_label = QLabel("Last response: —")
    svc_resp_label.setObjectName("SuiteStatusMuted")
    svc_resp_label.setWordWrap(True)
    svc_resp_label.setToolTip("Last UDS response")
    svc_resp_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
    req_body.addWidget(svc_title)

    starters = QWidget()
    starters.setObjectName("SuiteStarters")
    st_l = QHBoxLayout(starters)
    st_l.setContentsMargins(0, 0, 0, 4)
    st_l.setSpacing(8)

    def _starter_btn(text, tip, sid, preset=None):
        b = QPushButton(text)
        b.setObjectName("GhostButton")
        b.setFixedHeight(24)
        b.setCursor(Qt.CursorShape.PointingHandCursor)
        b.setToolTip(tip)
        st_l.addWidget(b)

        def _go(_=False, service_id=sid, pre=preset):
            for i in range(svc_tree.topLevelItemCount()):
                cat = svc_tree.topLevelItem(i)
                for j in range(cat.childCount()):
                    it = cat.child(j)
                    if it.data(0, Qt.ItemDataRole.UserRole) == service_id:
                        svc_tree.setCurrentItem(it)
                        break
            if pre:
                pre()

        b.clicked.connect(_go)
        return b

    def _preset_extended():
        for i in range(svc_form.rowCount()):
            w = svc_form.itemAt(i, QFormLayout.ItemRole.FieldRole)
            if w and w.widget() and isinstance(w.widget(), QComboBox):
                c = w.widget()
                idx = c.findData(0x03)
                if idx >= 0:
                    c.setCurrentIndex(idx)
                break

    _starter_btn("Extended session", "10 03 — most ECUs need this before DID/DTC",
                 0x10, _preset_extended)
    _starter_btn("Read VIN", "22 F190 — common identity DID", 0x22)
    _starter_btn("TesterPresent", "3E 80 — keep session alive", 0x3E)
    st_l.addStretch(1)
    req_body.addWidget(starters)

    related_row = QHBoxLayout()
    related_row.setContentsMargins(0, 0, 0, 0)
    related_row.setSpacing(6)
    related_lbl = QLabel("Related")
    related_lbl.setObjectName("SuiteFieldLabel")
    rel_did = QPushButton("DID")
    rel_did.setObjectName("GhostButton")
    rel_did.setFixedHeight(24)
    rel_did.setToolTip("Open DID leaf")
    rel_dtc = QPushButton("DTC")
    rel_dtc.setObjectName("GhostButton")
    rel_dtc.setFixedHeight(24)
    rel_dtc.setToolTip("Open DTC leaf")
    rel_scan = QPushButton("Scan")
    rel_scan.setObjectName("GhostButton")
    rel_scan.setFixedHeight(24)
    rel_scan.setToolTip("Open Scan workspace")
    for b, ic in ((rel_did, "symbol-numeric"), (rel_dtc, "warning"),
                  (rel_scan, "search")):
        try:
            codicons.set_button(b, ic)
        except Exception:
            pass
        related_row.addWidget(b)
    related_row.addStretch(1)
    req_body.addWidget(related_lbl)
    req_body.addLayout(related_row)

    req_body.addWidget(svc_param_box)
    req_body.addWidget(svc_send_btn, 0, Qt.AlignmentFlag.AlignLeft)
    svc_resp_label.setToolTip("Last UDS response")
    req_body.addWidget(svc_resp_label)
    req_body.addStretch()
    svc_h.addWidget(svc_tree, 2)
    svc_h.addWidget(req_host, 3)

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
        _form_row("Target session:", c)
        return lambda: encode_10(c.currentData())

    def _f_11():
        c = QComboBox()
        for k, v in ((0x01, "01 hardReset"), (0x02, "02 keyOffOnReset"),
                     (0x03, "03 softReset")):
            c.addItem(v, k)
        _form_row("Reset type:", c)
        return lambda: encode_11(c.currentData())

    def _f_3e():
        c = QComboBox()
        c.addItem("80 suppress positive (recommended)", 0x80)
        c.addItem("00 require response", 0x00)
        _form_row("Sub-function:", c)

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
        _form_row("Control:", cc)
        _form_row("Comm type:", ct)
        return lambda: encode_28(cc.currentData(), ct.currentData())

    def _f_85():
        c = QComboBox()
        c.addItem("01 on", 0x01)
        c.addItem("02 off", 0x02)
        _form_row("Sub-function:", c)
        return lambda: encode_85(c.currentData())

    def _f_22():
        e = _hex_edit("DID hex, e.g. F190", "F190")
        _form_row("DID:", e)
        return lambda: encode_22(_parse_did(e.text()))

    def _f_2e():
        d = _hex_edit("DID hex", "F187")
        v = _hex_edit("Data hex")
        _form_row("DID:", d)
        _form_row("Data:", v)
        return lambda: encode_2e(_parse_did(d.text()), _parse_hex(v.text()))

    def _f_2f():
        d = _hex_edit("DID hex", "0B7A")
        c = QComboBox()
        for k, v in ((0x01, "01 returnControlToECU"), (0x03, "03 resetToDefault"),
                     (0x04, "04 freezeCurrentState"),
                     (0x00, "0X shortTermAdjustment (needs data)")):
            c.addItem(v, k)
        v = _hex_edit("Control data hex")
        _form_row("DID:", d)
        _form_row("Control:", c)
        _form_row("Data:", v)

        def make():
            data = _parse_hex(v.text()) if v.text().strip() else b""
            return encode_2f(_parse_did(d.text()), c.currentData(), data)

        return make

    def _f_14():
        e = _hex_edit("DTC group 3 bytes (FFFFFF=all)", "FFFFFF")
        _form_row("DTC group:", e)
        return lambda: bytes([0x14]) + _parse_n(e.text(), 3, "DTC group")

    def _f_19():
        c = QComboBox()
        for k, v in ((0x01, "01 reportNumberOfDTCByStatusMask"),
                     (0x02, "02 reportDTCByStatusMask"),
                     (0x04, "04 reportDTCSnapshotByDTCNumber"),
                     (0x0A, "0A reportSupportedDTC")):
            c.addItem(v, k)
        m = StepSpin(0xFF, minimum=0, maximum=0xFF, hex_mode=True, width=72)
        e = _hex_edit("DTC number 3 bytes (sub 04)", "FFFFFF")
        _form_row("Sub-function:", c)
        _form_row("Status mask:", m)
        _form_row("DTC number:", e)

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
        _form_row("Sub-function:", c)
        _form_row("Data:", v)

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
        _form_row("Sub-function:", c)
        _form_row("Routine ID:", r)
        return lambda: encode_31(c.currentData(), _parse_did(r.text()))

    def _f_34():
        f = QComboBox()
        f.addItem("0x44 addr 4B + size 4B", (4, 4))
        f.addItem("0x22 addr 2B + size 2B", (2, 2))
        a = _hex_edit("Start address hex", "08040000")
        s = StepSpin(0x10000, minimum=1, maximum=0x7FFFFFFF, width=120)
        _form_row("Format:", f)
        _form_row("Start address:", a)
        _form_row("Byte count:", s)

        def make():
            al, sl = f.currentData()
            addr = int.from_bytes(_parse_n(a.text(), al, "address"), "big")
            return encode_34(addr, s.value(), al, sl)

        return make

    def _f_36():
        c = StepSpin(1, minimum=0, maximum=0xFF, width=72)
        d = _hex_edit("Block data hex")
        _form_row("Block counter:", c)
        _form_row("Data:", d)
        return lambda: encode_36(c.value(), _parse_hex(d.text()))

    def _f_37():
        _form_row("Params:", QLabel("none — RequestTransferExit"))
        return encode_37

    def _f_raw():
        e = _hex_edit("Full request hex, e.g. 22 F1 90", "22 F1 90")
        _form_row("Request:", e)
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
        session.set_focus(leaf="services", service=int(sid))
        _clear_form()
        svc_title.setText(NAMES.get(sid, str(sid)))
        _svc_expect[0] = True
        _svc_maker[0] = FORMS[sid]()

    svc_tree.currentItemChanged.connect(lambda cur, _p: _on_service_selected(cur))

    def _copy_text(text: str):
        if not text:
            return
        QGuiApplication.clipboard().setText(str(text))
        plugin_shell.set_status(parent, "Copied", 1500)

    def _select_service(service_id: int):
        sid = int(service_id or 0) & 0xFF
        for i in range(svc_tree.topLevelItemCount()):
            cat = svc_tree.topLevelItem(i)
            for j in range(cat.childCount()):
                it = cat.child(j)
                if it.data(0, Qt.ItemDataRole.UserRole) == sid:
                    svc_tree.setCurrentItem(it)
                    svc_tree.scrollToItem(it)
                    session.set_focus(leaf="services", service=sid)
                    return

    def _svc_menu(pos):
        item = svc_tree.itemAt(pos)
        menu = QMenu(svc_tree)
        if item is None or item.data(0, Qt.ItemDataRole.UserRole) is None:
            menu.addAction(
                "Open DID…",
                lambda: parent.run_action("uds.goto", page="did")
                if hasattr(parent, "run_action") else parent.goto_page("did"))
            menu.addAction(
                "Open DTC…",
                lambda: parent.run_action("uds.goto", page="dtc")
                if hasattr(parent, "run_action") else parent.goto_page("dtc"))
        else:
            svc_tree.setCurrentItem(item)
            sid = int(item.data(0, Qt.ItemDataRole.UserRole) or 0)
            session.set_focus(leaf="services", service=sid)
            menu.addAction(
                "Copy service ID",
                lambda: _copy_text("0x%02X" % sid))
            menu.addAction("Send", _on_svc_send)
            menu.addSeparator()
            if sid == 0x22:
                menu.addAction(
                    "Open DID…",
                    lambda: parent.run_action("uds.goto", page="did")
                    if hasattr(parent, "run_action")
                    else parent.goto_page("did"))
            if sid in (0x14, 0x19):
                menu.addAction(
                    "Open DTC…",
                    lambda: parent.run_action("uds.goto", page="dtc")
                    if hasattr(parent, "run_action")
                    else parent.goto_page("dtc"))
            menu.addAction(
                "Open Scan…",
                lambda: parent.run_action("uds.goto", page="scan")
                if hasattr(parent, "run_action") else parent.goto_page("scan"))
        if menu.actions():
            menu.exec(svc_tree.viewport().mapToGlobal(pos))

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
        if pdu:
            session.set_focus(leaf="services", service=int(pdu[0]))

        def cb(ok, resp, note):
            svc_resp_label.setText(note if note else ("Sent" if ok else "Failed"))
            if ok and resp:
                svc_resp_label.setText(
                    (note or "") + "\n" + " ".join("%02X" % b for b in resp))

        uds_send(pdu, cb, expect_response=_svc_expect[0],
                 tag=NAMES.get(pdu[0] if pdu else 0, "Request"))

    svc_send_btn.clicked.connect(_on_svc_send)
    svc_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    svc_tree.customContextMenuRequested.connect(_svc_menu)
    rel_did.clicked.connect(
        lambda: parent.run_action("uds.goto", page="did")
        if hasattr(parent, "run_action") else parent.goto_page("did"))
    rel_dtc.clicked.connect(
        lambda: parent.run_action("uds.goto", page="dtc")
        if hasattr(parent, "run_action") else parent.goto_page("dtc"))
    rel_scan.clicked.connect(
        lambda: parent.run_action("uds.goto", page="scan")
        if hasattr(parent, "run_action") else parent.goto_page("scan"))
    _leaf(leaf_pages, "services", svc_tab,
          "Pick a service, fill parameters, Send. Prefer Extended session first.")
    leaf_pages["services"].select_service = _select_service  # type: ignore[attr-defined]

    # Default path: land on Services with DiagnosticSessionControl → Extended
    for i in range(svc_tree.topLevelItemCount()):
        cat = svc_tree.topLevelItem(i)
        for j in range(cat.childCount()):
            it = cat.child(j)
            if it.data(0, Qt.ItemDataRole.UserRole) == 0x10:
                svc_tree.setCurrentItem(it)
                _preset_extended()
                break

    # ========== Tab 2: DID ==========
    did_tab = QWidget()
    did_v = QVBoxLayout(did_tab)
    did_v.setContentsMargins(0, 0, 0, 0)
    did_v.setSpacing(8)
    did_table = QTableWidget(0, 6)
    did_table.setHorizontalHeaderLabels(
        ["DID", "Name", "Type", "Raw", "Decoded", "Period(ms)"])
    did_table.verticalHeader().setVisible(False)
    did_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    did_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
    did_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)

    did_btn_row = QHBoxLayout()
    did_add = QPushButton("Add")
    did_del = QPushButton("Remove")
    did_read_all = QPushButton("Read all")
    did_read_all.setObjectName("PrimaryButton")
    did_poll_check = QCheckBox("Periodic poll")
    did_write_edit = QLineEdit()
    did_write_edit.setPlaceholderText("Write value (selected row)")
    did_write_btn = QPushButton("Write selected")
    for w, ic in ((did_add, "add"), (did_del, "delete"), (did_read_all, "start"),
                  (did_write_btn, "edit")):
        w.setFixedHeight(28)
        if w is did_read_all:
            codicons.set_button(w, ic, primary=True)
        else:
            codicons.set_button(w, ic)
    for w in (did_add, did_del, did_read_all, did_poll_check):
        did_btn_row.addWidget(w)
    did_rel_svc = QPushButton("Services")
    did_rel_svc.setObjectName("GhostButton")
    did_rel_svc.setFixedHeight(28)
    did_rel_svc.setToolTip("Open Services (22 ReadDataByIdentifier)")
    try:
        codicons.set_button(did_rel_svc, "symbol-method")
    except Exception:
        pass
    did_btn_row.addWidget(did_rel_svc)
    did_btn_row.addStretch()
    did_btn_row.addWidget(did_write_edit, 2)
    did_write_btn.setFixedHeight(28)
    did_btn_row.addWidget(did_write_btn)

    act_card, act_body = vscode_theme.block(
        "Actions",
        "Add or remove rows, read every DID, or write the selected row.",
    )
    act_body.addLayout(did_btn_row)
    list_card, list_body = vscode_theme.block(
        "Identifiers",
        "22 reads, 2E writes. Built-in dictionary plus rows you add.",
    )
    list_body.addWidget(did_table, 1)
    did_v.addWidget(act_card)
    did_v.addWidget(list_card, 1)

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
        form.addRow(vscode_theme.field_label("DID"), de)
        form.addRow(vscode_theme.field_label("Name"), ne)
        form.addRow(vscode_theme.field_label("Type"), tc)
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

    def _select_did(did: int):
        target = int(did or 0) & 0xFFFF
        for row, r in enumerate(_did_rows):
            if r["did"] == target:
                did_table.selectRow(row)
                did_table.scrollToItem(did_table.item(row, 0))
                session.set_focus(leaf="did", did=target)
                return

    def _did_menu(pos):
        index = did_table.indexAt(pos)
        menu = QMenu(did_table)
        row = index.row() if index.isValid() else -1
        if 0 <= row < len(_did_rows):
            did_table.selectRow(row)
            r = _did_rows[row]
            did = int(r["did"])
            session.set_focus(leaf="did", did=did)
            menu.addAction(
                "Copy DID", lambda: _copy_text("0x%04X" % did))
            menu.addAction(
                "Copy name", lambda: _copy_text(r.get("name", "")))
            menu.addSeparator()
            menu.addAction("Read", lambda d=did: _did_read(d))
            menu.addAction("Write selected", _did_write_fn)
            menu.addSeparator()
            menu.addAction(
                "Open Services (22)…",
                lambda: parent.run_action("uds.goto_service", service=0x22)
                if hasattr(parent, "run_action")
                else parent.goto_page("services"))
            menu.addAction("Remove", _did_del_fn)
        else:
            menu.addAction("Add DID…", _did_add_fn)
            menu.addAction("Read all", _did_read_all_fn)
            menu.addAction(
                "Open Services…",
                lambda: parent.run_action("uds.goto", page="services")
                if hasattr(parent, "run_action")
                else parent.goto_page("services"))
        if menu.actions():
            menu.exec(did_table.viewport().mapToGlobal(pos))

    did_table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    did_table.customContextMenuRequested.connect(_did_menu)
    did_rel_svc.clicked.connect(
        lambda: parent.run_action("uds.goto_service", service=0x22)
        if hasattr(parent, "run_action") else parent.goto_page("services"))

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
    _leaf(leaf_pages, "did", did_tab,
          "Read or write identifiers (22 / 2E). Built-in dictionary plus your own DIDs.")
    leaf_pages["did"].select_did = _select_did  # type: ignore[attr-defined]

    # ========== Leaf: DTC ==========
    dtc_tab = QWidget()
    dtc_v = QVBoxLayout(dtc_tab)
    dtc_v.setContentsMargins(0, 0, 0, 0)
    dtc_v.setSpacing(8)
    mask_card, mask_body = vscode_theme.block(
        "Status mask",
        "Bits included in 19 02. Test failed and confirmed are on by default.",
    )
    mask_row = QHBoxLayout()
    mask_checks = {}
    for bit, name in DTC_STATUS_BITS.items():
        cb = QCheckBox(name)
        cb.setToolTip(name)
        if bit in (0x01, 0x08):
            cb.setChecked(True)
        mask_row.addWidget(cb)
        mask_checks[bit] = cb
    mask_row.addStretch()
    mask_body.addLayout(mask_row)

    dtc_btn_row = QHBoxLayout()
    dtc_read_btn = QPushButton("Read DTC list")
    dtc_read_btn.setObjectName("PrimaryButton")
    dtc_cnt_btn = QPushButton("Read count")
    dtc_clear_btn = QPushButton("Clear all")
    for w, ic, primary in (
        (dtc_read_btn, "search", True),
        (dtc_cnt_btn, "info", False),
        (dtc_clear_btn, "clear", False),
    ):
        w.setFixedHeight(28)
        codicons.set_button(w, ic, primary=primary)
    dtc_btn_row.addWidget(dtc_read_btn)
    dtc_btn_row.addWidget(dtc_cnt_btn)
    dtc_btn_row.addStretch()
    dtc_btn_row.addWidget(dtc_clear_btn)
    mask_body.addLayout(dtc_btn_row)

    dtc_table = QTableWidget(0, 4)
    dtc_table.setHorizontalHeaderLabels(
        ["DTC", "Raw bytes", "Status", "Status bits"])
    dtc_table.verticalHeader().setVisible(False)
    dtc_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    dtc_table.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    fault_card, fault_body = vscode_theme.block(
        "Faults",
        "Decoded DTC and status bits from the last 19 02.",
    )
    fault_body.addWidget(dtc_table, 1)
    dtc_v.addWidget(mask_card)
    dtc_v.addWidget(fault_card, 1)

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

    def _select_dtc(code: str):
        needle = (code or "").strip().upper()
        if not needle:
            return
        for row in range(dtc_table.rowCount()):
            item = dtc_table.item(row, 0)
            if item and item.text().upper() == needle:
                dtc_table.selectRow(row)
                dtc_table.scrollToItem(item)
                session.set_focus(leaf="dtc", dtc=needle)
                return

    def _dtc_menu(pos):
        index = dtc_table.indexAt(pos)
        menu = QMenu(dtc_table)
        row = index.row() if index.isValid() else -1
        menu.addAction("Read DTC list", _on_dtc_read)
        menu.addAction("Read count", _on_dtc_count)
        if row >= 0 and dtc_table.item(row, 0) is not None:
            dtc_table.selectRow(row)
            code = dtc_table.item(row, 0).text()
            session.set_focus(leaf="dtc", dtc=code)
            menu.addSeparator()
            menu.addAction("Copy DTC", lambda: _copy_text(code))
            raw = dtc_table.item(row, 1)
            if raw is not None:
                menu.addAction(
                    "Copy raw", lambda: _copy_text(raw.text()))
            menu.addSeparator()
            menu.addAction(
                "Open Services (19)…",
                lambda: parent.run_action("uds.goto_service", service=0x19)
                if hasattr(parent, "run_action")
                else parent.goto_page("services"))
        menu.addSeparator()
        menu.addAction("Clear all…", _on_dtc_clear)
        if menu.actions():
            menu.exec(dtc_table.viewport().mapToGlobal(pos))

    dtc_table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    dtc_table.customContextMenuRequested.connect(_dtc_menu)
    _leaf(leaf_pages, "dtc", dtc_tab,
          "Read stored faults (19) or clear them (14). Status bits are decoded in the table.")
    leaf_pages["dtc"].select_dtc = _select_dtc  # type: ignore[attr-defined]

    # ========== Leaf: SecAccess ==========
    sec_tab = QWidget()
    sec_v = QVBoxLayout(sec_tab)
    sec_v.setContentsMargins(0, 0, 0, 0)
    sec_v.setSpacing(8)
    unlock_card, unlock_body = vscode_theme.block(
        "Unlock",
        "Request a seed, compute a key, then send it. Observational audit is the Security page.",
    )
    sec_host = QWidget()
    sec_form = QFormLayout(sec_host)
    vscode_theme.tune_form(sec_form)
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
    sec_file_btn = QPushButton("Choose file")
    sec_file_btn.setObjectName("SecondaryButton")
    sec_file_btn.setFixedHeight(28)
    codicons.set_button(sec_file_btn, "browse")
    sec_file_label = vscode_theme.value_label("None")
    sec_seed_label = vscode_theme.value_label("—", mono=True)
    sec_key_label = vscode_theme.value_label("—", mono=True)
    sec_result_label = vscode_theme.value_label("—")
    sec_seed_btn = QPushButton("Request seed (27 odd)")
    sec_key_btn = QPushButton("Send key (27 even)")
    sec_key_btn.setEnabled(False)
    sec_form.addRow(vscode_theme.field_label("Security level"), sec_level)
    sec_form.addRow(vscode_theme.field_label("Key algorithm"), sec_algo)
    sec_form.addRow(vscode_theme.field_label("Expression"), sec_expr)
    sec_form.addRow(vscode_theme.field_label("Algo file"), sec_file_btn)
    sec_form.addRow(vscode_theme.field_label("Current file"), sec_file_label)
    sec_form.addRow(vscode_theme.field_label("Seed"), sec_seed_label)
    sec_form.addRow(vscode_theme.field_label("Computed key"), sec_key_label)
    sec_form.addRow(vscode_theme.field_label("Result"), sec_result_label)
    sec_form.addRow(sec_seed_btn)
    sec_form.addRow(sec_key_btn)
    vscode_theme.polish_form_labels(sec_form)

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

    sec_seed_btn.setObjectName("PrimaryButton")
    sec_seed_btn.setFixedSize(180, 28)
    sec_key_btn.setObjectName("SecondaryButton")
    sec_key_btn.setFixedSize(180, 28)
    codicons.set_button(sec_seed_btn, "lock", primary=True)
    codicons.set_button(sec_key_btn, "check")
    sec_seed_btn.clicked.connect(_on_request_seed)
    sec_key_btn.clicked.connect(_on_send_key)
    unlock_body.addWidget(sec_host)
    sec_v.addWidget(unlock_card)
    sec_v.addStretch(1)
    _leaf(leaf_pages, "sec_access", sec_tab,
          "Request a seed (27 01), compute a key, send it (27 02). Not an audit — that is the Security page.")

    # ========== Leaf: Flash ==========
    fl_tab = QWidget()
    fl_v = QVBoxLayout(fl_tab)
    fl_v.setContentsMargins(0, 0, 0, 0)
    fl_v.setSpacing(8)
    fl_file_edit = QLineEdit()
    fl_file_edit.setPlaceholderText("BIN, Intel HEX, or Motorola S19")
    fl_file_btn = QPushButton("Browse")
    fl_file_btn.setObjectName("SecondaryButton")
    fl_file_btn.setFixedSize(96, 28)
    codicons.set_button(fl_file_btn, "browse")
    fl_file_row = QHBoxLayout()
    fl_file_row.addWidget(fl_file_edit, 1)
    fl_file_row.addWidget(fl_file_btn)
    fl_addr = _hex_edit("Start address hex (4 bytes)", "08040000")
    fl_pre_check = QCheckBox("Precheck (10 02, 27 unlock, 85 02, 28 03 03)")
    fl_pre_check.setChecked(True)
    fl_erase_check = QCheckBox("Erase before download (31 01 FF00 + address + size)")
    fl_erase_check.setChecked(True)
    fl_reset_check = QCheckBox("ECU reset when done (11 01)")
    fl_reset_check.setChecked(True)
    fl_sec_lv = QComboBox()
    for lv in (0x01, 0x03, 0x05):
        fl_sec_lv.addItem("level %d" % ((lv + 1) // 2), lv)
    fl_block_edit = QLineEdit()
    fl_block_edit.setPlaceholderText("Empty = use max block from 34 response")

    cols = QHBoxLayout()
    cols.setSpacing(8)
    file_card, file_body = vscode_theme.block(
        "File",
        "BIN, Intel HEX, or Motorola S19. HEX and S19 fill the start address.",
    )
    file_host = QWidget()
    file_form = QFormLayout(file_host)
    vscode_theme.tune_form(file_form)
    file_form.addRow(vscode_theme.field_label("Firmware"), fl_file_row)
    file_form.addRow(vscode_theme.field_label("Start address"), fl_addr)
    file_form.addRow(vscode_theme.field_label("Block size"), fl_block_edit)
    vscode_theme.polish_form_labels(file_form)
    file_body.addWidget(file_host)
    cols.addWidget(file_card, 1)

    seq_card, seq_body = vscode_theme.block(
        "Sequence",
        "Runs in order: precheck, erase, 34/36/37, checksum, reset.",
    )
    seq_body.addWidget(fl_pre_check)
    seq_body.addWidget(fl_erase_check)
    seq_body.addWidget(fl_reset_check)
    seq_host = QWidget()
    seq_form = QFormLayout(seq_host)
    vscode_theme.tune_form(seq_form)
    seq_form.addRow(vscode_theme.field_label("Security level"), fl_sec_lv)
    vscode_theme.polish_form_labels(seq_form)
    seq_body.addWidget(seq_host)
    fl_btn_row = QHBoxLayout()
    fl_start_btn = QPushButton("Start flash")
    fl_start_btn.setObjectName("PrimaryButton")
    fl_start_btn.setFixedSize(120, 28)
    fl_start_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    codicons.set_button(fl_start_btn, "start", primary=True)
    fl_stop_btn = QPushButton("Abort")
    fl_stop_btn.setObjectName("SecondaryButton")
    fl_stop_btn.setFixedSize(88, 28)
    codicons.set_button(fl_stop_btn, "abort")
    fl_stop_btn.setEnabled(False)
    fl_btn_row.addWidget(fl_start_btn)
    fl_btn_row.addWidget(fl_stop_btn)
    fl_btn_row.addStretch()
    seq_body.addLayout(fl_btn_row)
    cols.addWidget(seq_card, 1)
    fl_v.addLayout(cols)

    fl_progress = QProgressBar()
    fl_progress.setRange(0, 1000)
    fl_progress.setValue(0)
    fl_status = QLabel("Idle — choose a file, then Start flash")
    fl_status.setObjectName("SuiteStatusMuted")
    fl_status.setToolTip("Flash sequence status")
    prog_card, prog_body = vscode_theme.block(
        "Progress",
        "Current step. Each request is also written to OUTPUT.",
    )
    prog_body.addWidget(fl_progress)
    prog_body.addWidget(fl_status)
    fl_v.addWidget(prog_card)
    fl_v.addStretch()

    def _on_pick_file():
        path, _ = QFileDialog.getOpenFileName(
            root, "Firmware file", "",
            "Firmware (*.bin *.hex *.ihex *.s19 *.s28 *.s37 *.srec *.mot);;All files (*.*)")
        if path:
            fl_file_edit.setText(path)
            try:
                from core.image_load import load_firmware
                addr, data, note = load_firmware(path)
                fl_status.setText("%s — %d bytes @ 0x%X" % (note, len(data), addr))
                if addr:
                    fl_addr.setText("%08X" % addr)
            except Exception as e:
                fl_status.setText("Cannot parse: %s" % e)

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
                nxt = "erase" if _flash.get("erase") else "reqdl"
                _fl_send(encode_28(0x03, 0x03), nxt, "28 03 03")
            else:
                _fl_step("erase" if _flash.get("erase") else "reqdl", None)
        elif st == "erase":
            if _flash.get("erase"):
                _fl_set("Erase 31 01 FF00...")
                pdu = (encode_31(0x01, 0xFF00)
                       + _flash["addr"].to_bytes(4, "big")
                       + len(_flash["data"]).to_bytes(4, "big"))
                _fl_send(pdu, "reqdl", "31 01 FF00")
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
        elif st == "exit":
            _fl_set("RequestTransferExit 37...")
            _fl_send(encode_37(), "check", "37 RequestTransferExit")
        elif st == "check":
            _fl_set("Integrity check 31 01 FF01...")
            nxt = "reset" if _flash.get("reset") else "done"
            _fl_send(encode_31(0x01, 0xFF01), nxt, "31 01 FF01")
        elif st == "reset":
            if _flash.get("reset"):
                _fl_set("ECU reset 11 01...")
                _fl_send(encode_11(0x01), "done", "11 01")
            else:
                _fl_step("done", None)
        elif st == "done":
            _fl_done()

    def _fl_next_block():
        if _flash["state"] != "data":
            return
        pos, data = _flash["pos"], _flash["data"]
        chunk = data[pos:pos + _flash["maxblk"] - 2]
        if not chunk:
            _flash["state"] = "exit"
            _fl_step("exit", None)
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
            from core.image_load import load_firmware
            img_addr, data, note = load_firmware(path)
        except Exception as e:
            QMessageBox.warning(root, "Flash", "Read failed: %s" % e)
            return
        if not data:
            QMessageBox.warning(root, "Flash", "Firmware file is empty")
            return
        try:
            addr = int.from_bytes(_parse_n(fl_addr.text(), 4, "start address"), "big")
        except ValueError:
            addr = img_addr
        if img_addr and fl_addr.text().strip() in ("", "08040000"):
            addr = img_addr
            fl_addr.setText("%08X" % addr)
        _flash.update({
            "data": data, "addr": addr, "size": len(data),
            "pos": 0, "block": 1, "maxblk": 0,
            "precheck": fl_pre_check.isChecked(),
            "erase": fl_erase_check.isChecked(),
            "reset": fl_reset_check.isChecked(),
            "t0": time.time(),
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
    _leaf(leaf_pages, "flash", fl_tab,
          "Programming sequence: optional precheck, erase, 34/36/37, checksum, reset.")

    # Leaves mounted by AppShell Diagnose workspace (sidebar, not Tab strip).
    root.leaf_pages = leaf_pages  # type: ignore[attr-defined]
    _ui.polish_work_surface(root)
    return root
