# -*- coding: utf-8 -*-
"""obd2-scanner — OBD-II (ISO 15031-5 / SAE J1979) with ISO-TP.

Mode 01 live data, Mode 02 freeze frame, 03/04/07 DTCs, 09 VIN,
PID history table, English UI.
"""

from __future__ import annotations

import time
from collections import deque

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QGuiApplication
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QLabel, QTreeWidget,
    QTreeWidgetItem, QTextEdit, QMessageBox, QHeaderView, QTabWidget,
    QSpinBox, QTableWidget, QTableWidgetItem, QLineEdit, QMenu,
)

import sin
from _shared import plugin_shell, state_store
from _shared.isotp_client import IsotpClient
from pages import _ui

SUITE_ID = "obd-suite"
PAGE_KEY = "scanner"


def _d_a(data):
    return data[0]


def _d_pct(data):
    return "%.1f" % (data[0] * 100.0 / 255.0)


def _d_temp(data):
    return data[0] - 40


def _d_rpm(data):
    return (data[0] * 256 + data[1]) // 4


def _d_speed(data):
    return data[0]


def _d_16_100(data):
    return "%.3f" % ((data[0] * 256 + data[1]) / 100.0)


def _d_16_1000(data):
    return "%.1f" % ((data[0] * 256 + data[1]) / 1000.0)


def _d_16_4(data):
    return (data[0] * 256 + data[1]) / 4.0


def _d_16_20(data):
    return "%.2f" % ((data[0] * 256 + data[1]) / 20.0)


def _d_afr(data):
    return "%.2f" % ((data[0] * 256 + data[1]) / 32768.0 * 14.7)


def _d_secs(data):
    return data[0] * 256 + data[1]


PIDS = {
    0x03: ("Fuel system status", lambda d: "0x%02X" % d[0], ""),
    0x04: ("Calculated load", _d_pct, "%"),
    0x05: ("Coolant temp", _d_temp, "C"),
    0x06: ("STFT B1", lambda d: "%.1f" % ((d[0] - 128) * 100.0 / 128.0), "%"),
    0x07: ("LTFT B1", lambda d: "%.1f" % ((d[0] - 128) * 100.0 / 128.0), "%"),
    0x0A: ("Fuel pressure", lambda d: d[0] * 3, "kPa"),
    0x0B: ("MAP", _d_a, "kPa"),
    0x0C: ("Engine RPM", _d_rpm, "rpm"),
    0x0D: ("Vehicle speed", _d_speed, "km/h"),
    0x0E: ("Timing advance", lambda d: "%.1f" % (d[0] / 2.0 - 64.0), "deg"),
    0x0F: ("Intake temp", _d_temp, "C"),
    0x10: ("MAF", _d_16_100, "g/s"),
    0x11: ("Throttle position", _d_pct, "%"),
    0x1F: ("Run time since start", _d_secs, "s"),
    0x21: ("Distance with MIL", _d_16_4, "km"),
    0x2F: ("Fuel level", _d_pct, "%"),
    0x31: ("Distance traveled", _d_16_4, "km"),
    0x33: ("Barometric pressure", _d_a, "kPa"),
    0x42: ("Control module voltage", _d_16_1000, "V"),
    0x43: ("Absolute load", lambda d: "%.1f" % (d[0] * 100.0 / 255.0 * 2.5), "%"),
    0x44: ("AFR (lambda->14.7)", _d_afr, ""),
    0x45: ("Relative throttle", _d_pct, "%"),
    0x46: ("Ambient temp", _d_temp, "C"),
    0x5C: ("Oil temp", _d_temp, "C"),
    0x5E: ("Fuel rate", _d_16_20, "L/h"),
}

DTC_TEXTS = {
    "P0300": "Random/multiple cylinder misfire",
    "P0420": "Catalyst efficiency below threshold (B1)",
    "P0171": "System too lean (B1)",
    "P0172": "System too rich (B1)",
    "U0100": "Lost communication with ECM/PCM",
}

_supported = {}
_values = {}
_history = {}  # pid -> deque of (ts, text)
_freeze = []   # [(pid, name, value, unit)]
_dtcs = []
_vin = ""
_readiness_raw = b""
_log = []
_tx_id = 0x7DF
_rx_id = 0x7E8
_polling = False
_poll_interval_ms = 500
_running = True
_isotp = None
_pending_kind = None
_sender = None


def readiness_raw() -> bytes:
    return bytes(_readiness_raw)


def apply_ids(tx_id: int, rx_id: int) -> None:
    global _tx_id, _rx_id
    _tx_id = int(tx_id)
    _rx_id = int(rx_id)
    if _isotp is not None:
        _isotp.func_id = _tx_id
        _isotp.rx_id = _rx_id
        _isotp.tx_id = 0x7E0 if _tx_id == 0x7DF else _tx_id


def request_pid(mode: int, pid: int = 0, functional: bool = False) -> bool:
    if _sender is None:
        return False
    _sender(mode, pid, functional)
    return True


def _lg(text):
    _log.append((time.time(), text))
    if len(_log) > 800:
        del _log[:400]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def _decode_dtc(a, b):
    prefix = "PCBU"[(a >> 6) & 0x03]
    return "%s%04X" % (prefix, ((a & 0x3F) << 8) | b)


def _handle_pdu(pdu: bytes):
    global _vin, _pending_kind, _readiness_raw
    if not pdu:
        return
    mode = pdu[0] - 0x40
    ts = time.time()
    _lg("RX PDU mode=%02X: %s" % (mode, _hex(pdu[:32])))

    if mode in (1, 2) and len(pdu) >= 2:
        pid = pdu[1]
        payload = pdu[2:]
        if mode == 1:
            if pid in (0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0):
                base = pid + 1
                for i in range(min(32, len(payload) * 8)):
                    if payload[i // 8] & (0x80 >> (i % 8)):
                        _supported[base + i] = True
            elif pid == 0x01 and payload:
                _readiness_raw = bytes(payload[:4])
                mil = "ON" if payload[0] & 0x80 else "OFF"
                cnt = payload[0] & 0x7F
                text = "MIL:%s DTCs:%d" % (mil, cnt)
                _values[0x01] = (text, "", ts)
            elif pid in PIDS:
                name, fn, unit = PIDS[pid]
                try:
                    text = str(fn(payload))
                except Exception:
                    text = "decode err: %s" % _hex(payload)
                _values[pid] = (text, unit, ts)
                hist = _history.setdefault(pid, deque(maxlen=120))
                hist.append((ts, text))
        elif mode == 2:
            # Freeze frame: PID after frame number byte in some ECUs — keep simple
            frame_no = payload[0] if payload else 0
            rest = payload[1:] if len(payload) > 1 else b""
            if pid in PIDS and rest:
                name, fn, unit = PIDS[pid]
                try:
                    text = str(fn(rest))
                except Exception:
                    text = _hex(rest)
                _freeze.append((pid, name, text, unit, frame_no))
            elif pid == 0x02:
                _lg("Freeze frame number response: %s" % _hex(payload))
    elif mode in (3, 7) and len(pdu) >= 2:
        cnt = pdu[1]
        for i in range((len(pdu) - 2) // 2):
            code = _decode_dtc(pdu[2 + 2 * i], pdu[3 + 2 * i])
            _dtcs.append((code, DTC_TEXTS.get(code, ""), "03" if mode == 3 else "07"))
        _lg("DTCs counted=%d" % cnt)
    elif mode == 4:
        _lg("Clear DTCs acknowledged")
    elif mode == 9 and len(pdu) >= 2:
        info = pdu[1]
        if info == 0x02:
            # VIN may arrive as multi-byte after count
            raw = pdu[2:]
            if raw and raw[0] <= 0x05:
                raw = raw[1:]  # skip message count
            chars = "".join(chr(b) for b in raw if 0x20 <= b < 0x7F)
            if chars:
                _vin = (_vin + chars) if _vin and not chars.startswith("1") else chars
                if len(_vin) > 17:
                    _vin = _vin[-17:]
    _pending_kind = None


def build(parent, session, log_fn):
    global _running, _polling, _tx_id, _rx_id, _isotp, _vin, _sender, _readiness_raw
    _supported.clear()
    _values.clear()
    _history.clear()
    del _freeze[:]
    del _dtcs[:]
    _log.clear()
    _vin = ""
    _readiness_raw = b""
    _polling = False
    _running = True
    _tx_id = int(getattr(session, "tx_id", 0x7DF) or 0x7DF)
    _rx_id = int(getattr(session, "rx_id", 0x7E8) or 0x7E8)

    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    poll_spin = QSpinBox()
    poll_spin.setRange(100, 5000)
    poll_spin.setValue(_poll_interval_ms)
    poll_spin.setSuffix(" ms")
    poll_spin.setToolTip("Live poll interval")

    support_btn = _ui.primary_btn(
        "Discover", "Discover supported Mode 01 PIDs", "search")
    dtc_btn = _ui.ghost_btn("DTCs", "Read stored DTCs (Mode 03)", "info")
    pending_btn = _ui.ghost_btn("Pending", "Read pending DTCs (Mode 07)", "info")
    clear_btn = _ui.ghost_btn("Clear", "Clear DTCs (Mode 04)", "clear")
    vin_btn = _ui.ghost_btn("VIN", "Read VIN (Mode 09)", "info")
    freeze_btn = _ui.ghost_btn("Freeze", "Freeze frame (Mode 02)", "sync")
    poll_btn = _ui.ghost_btn("Start poll", "Toggle live PID polling", "play")
    readiness_btn = _ui.ghost_btn(
        "Readiness", "Open Readiness monitors", "search")
    filter_edit = QLineEdit()
    filter_edit.setPlaceholderText("Filter PID / name…")
    filter_edit.setToolTip("Filter live data rows")
    filter_edit.setClearButtonEnabled(True)

    layout.addWidget(_ui.tool_strip(
        support_btn, dtc_btn, pending_btn, clear_btn, vin_btn, freeze_btn,
        poll_btn, _ui.field_label("Poll"), poll_spin, readiness_btn))
    layout.addWidget(_ui.inline_filter(filter_edit))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    data_tab = QWidget()
    dv = QVBoxLayout(data_tab)
    dv.setContentsMargins(0, 0, 0, 0)
    dv.setSpacing(0)
    val_tree = QTreeWidget()
    val_tree.setHeaderLabels(["PID", "Name", "Value", "Unit", "Time"])
    _ui.style_tree(val_tree, header_hidden=False)
    val_tree.setRootIsDecorated(False)
    dv.addWidget(val_tree, 1)
    export_btn = _ui.ghost_btn("Export CSV", "Export live PID values", "export")
    dv.addWidget(_ui.tool_strip(export_btn))
    tabs.addTab(data_tab, "Live data")

    hist_tab = QWidget()
    hv = QVBoxLayout(hist_tab)
    hv.setContentsMargins(0, 0, 0, 0)
    hist_table = QTableWidget(0, 3)
    hist_table.setHorizontalHeaderLabels(["Time", "PID", "Value"])
    hist_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
    _ui.style_table(hist_table)
    hv.addWidget(hist_table)
    tabs.addTab(hist_tab, "PID history")

    freeze_tab = QWidget()
    fv = QVBoxLayout(freeze_tab)
    fv.setContentsMargins(0, 0, 0, 0)
    freeze_tree = QTreeWidget()
    freeze_tree.setHeaderLabels(["PID", "Name", "Value", "Unit", "Frame#"])
    _ui.style_tree(freeze_tree, header_hidden=False)
    freeze_tree.setRootIsDecorated(False)
    fv.addWidget(freeze_tree)
    tabs.addTab(freeze_tab, "Freeze frame")

    dtc_tab = QWidget()
    tv = QVBoxLayout(dtc_tab)
    tv.setContentsMargins(0, 0, 0, 0)
    dtc_tree = QTreeWidget()
    dtc_tree.setHeaderLabels(["Code", "Description", "Mode"])
    _ui.style_tree(dtc_tree, header_hidden=False)
    dtc_tree.setRootIsDecorated(False)
    tv.addWidget(dtc_tree)
    tabs.addTab(dtc_tab, "DTCs")

    log_tab = QWidget()
    lv = QVBoxLayout(log_tab)
    lv.setContentsMargins(0, 0, 0, 0)
    log_view = QTextEdit()
    log_view.setObjectName("SuiteCode")
    log_view.setReadOnly(True)
    lv.addWidget(log_view)
    tabs.addTab(log_tab, "Log")

    vin_label = _ui.muted_label("VIN: -")
    vin_label.setToolTip("Vehicle identification number from Mode 09")
    layout.addWidget(_ui.tool_strip(vin_label))

    _ui.style_page_tabs(tabs)
    def _send_frame(can_id, data):
        sin.frames.send(can_id, bytes(data))

    _isotp = IsotpClient(_send_frame, qt_parent=root)
    _isotp.tx_id = 0x7E0 if _tx_id != 0x7DF else 0x7E0
    _isotp.func_id = _tx_id
    _isotp.rx_id = _rx_id
    _isotp.on_received = _handle_pdu
    _isotp.on_error = lambda m: _lg("ISO-TP: %s" % m)
    _isotp.on_log = lambda d, cid, fr, note: _lg("%s 0x%X %s [%s]" % (d, cid, _hex(fr), note))

    def _request(mode, pid=0, functional=True):
        global _pending_kind
        session.note_scan()
        session.set_focus(mode=mode, pid=pid)
        if mode in (1, 2, 9):
            pdu = bytes([mode, pid])
        else:
            pdu = bytes([mode])
        _isotp.tx_id = 0x7E0 if not functional else _tx_id
        _isotp.func_id = _tx_id
        _isotp.rx_id = _rx_id
        _pending_kind = (mode, pid)
        _isotp.send(pdu, functional=functional and _tx_id == 0x7DF)
        _lg("TX mode=%02X pid=%02X via ISO-TP" % (mode, pid))

    _sender = _request

    def on_frame(frame):
        if not _running:
            return
        if not (0x7E8 <= frame.id <= 0x7EF):
            return
        _isotp.rx_id = frame.id
        _isotp.on_frame(frame.id, bytes(frame.data))

    def _on_ids(tx, rx):
        apply_ids(tx, rx)

    session.on_bus_frame(on_frame)
    session.on_ids_changed(_on_ids)

    def refresh():
        needle = (filter_edit.text() or "").strip().lower()
        cur_pid = None
        cur = val_tree.currentItem()
        if cur is not None:
            data = cur.data(0, Qt.ItemDataRole.UserRole)
            if data is not None:
                cur_pid = int(data)
        val_tree.clear()
        focus_item = None
        for pid, (text, unit, ts) in sorted(_values.items()):
            name = "MIL / DTC count" if pid == 0x01 else (
                PIDS[pid][0] if pid in PIDS else "PID %02X" % pid)
            row_txt = "0x%02X %s %s" % (pid, name, text)
            if needle and needle not in row_txt.lower():
                continue
            it = QTreeWidgetItem([
                "0x%02X" % pid, name, text, unit, "%.1f" % ts])
            it.setData(0, Qt.ItemDataRole.UserRole, int(pid))
            val_tree.addTopLevelItem(it)
            if cur_pid is not None and pid == cur_pid:
                focus_item = it
            elif focus_item is None and session.focus_pid == pid:
                focus_item = it
        if focus_item is not None:
            val_tree.setCurrentItem(focus_item)
        freeze_tree.clear()
        for row in _freeze:
            it = QTreeWidgetItem([
                "0x%02X" % row[0], row[1], row[2], row[3], str(row[4])])
            it.setData(0, Qt.ItemDataRole.UserRole, int(row[0]))
            freeze_tree.addTopLevelItem(it)
        dtc_tree.clear()
        for code, text, mode in _dtcs:
            it = QTreeWidgetItem([code, text, mode])
            it.setData(0, Qt.ItemDataRole.UserRole, code)
            dtc_tree.addTopLevelItem(it)
        hist_table.setRowCount(0)
        for pid, hist in sorted(_history.items()):
            for ts, text in list(hist)[-40:]:
                r = hist_table.rowCount()
                hist_table.insertRow(r)
                hist_table.setItem(r, 0, QTableWidgetItem(time.strftime("%H:%M:%S", time.localtime(ts))))
                hist_table.setItem(r, 1, QTableWidgetItem("0x%02X" % pid))
                hist_table.setItem(r, 2, QTableWidgetItem(text))
        vin_label.setText("VIN: %s" % (_vin.strip() or "-"))
        if _log:
            log_view.setPlainText("\n".join(
                "[%s] %s" % (time.strftime("%H:%M:%S", time.localtime(ts)), t)
                for ts, t in _log[-160:]))

    def _copy_text(text: str):
        if not text:
            return
        QGuiApplication.clipboard().setText(str(text))
        plugin_shell.set_status(parent, "Copied", 1500)

    def _pid_from_item(item):
        if item is None:
            return None
        data = item.data(0, Qt.ItemDataRole.UserRole)
        if data is not None:
            try:
                return int(data)
            except (TypeError, ValueError):
                pass
        try:
            return int(item.text(0), 0)
        except Exception:
            return None

    def _val_menu(pos):
        item = val_tree.itemAt(pos)
        menu = QMenu(val_tree)
        if item is None:
            menu.addAction("Discover PIDs", on_support)
            menu.addAction("Open Readiness…", on_readiness)
        else:
            val_tree.setCurrentItem(item)
            pid = _pid_from_item(item)
            name = item.text(1)
            value = item.text(2)
            if pid is not None:
                session.set_focus(mode=1, pid=pid)
                menu.addAction(
                    "Copy PID", lambda: _copy_text("0x%02X" % pid))
                menu.addAction(
                    "Copy value",
                    lambda: _copy_text("%s %s" % (value, item.text(3)).strip()))
                menu.addSeparator()
                menu.addAction(
                    "Poll this PID",
                    lambda p=pid: _request(1, p, functional=(_tx_id == 0x7DF)))
                if pid == 0x01:
                    menu.addAction("Open Readiness…", on_readiness)
                else:
                    menu.addAction(
                        "Open Readiness…",
                        lambda: parent.run_action("obd.goto_readiness")
                        if hasattr(parent, "run_action") else on_readiness())
                menu.addSeparator()
                menu.addAction(
                    "Filter: %s" % (name[:24] or ("PID %02X" % pid)),
                    lambda n=name, p=pid: filter_edit.setText(
                        n if n else ("%02X" % p)))
        if menu.actions():
            menu.exec(val_tree.viewport().mapToGlobal(pos))

    def _dtc_menu(pos):
        item = dtc_tree.itemAt(pos)
        menu = QMenu(dtc_tree)
        if item is None:
            menu.addAction("Read DTCs", lambda: _request(3, 0))
            menu.addAction("Read pending", lambda: _request(7, 0))
        else:
            dtc_tree.setCurrentItem(item)
            code = item.text(0)
            menu.addAction("Copy code", lambda: _copy_text(code))
            menu.addAction(
                "Copy description", lambda: _copy_text(item.text(1)))
            menu.addSeparator()
            menu.addAction("Clear DTCs…", on_clear)
        if menu.actions():
            menu.exec(dtc_tree.viewport().mapToGlobal(pos))

    def _freeze_menu(pos):
        item = freeze_tree.itemAt(pos)
        menu = QMenu(freeze_tree)
        if item is None:
            menu.addAction("Read freeze frame", on_freeze)
        else:
            freeze_tree.setCurrentItem(item)
            pid = _pid_from_item(item)
            menu.addAction("Copy PID", lambda: _copy_text(item.text(0)))
            menu.addAction("Copy value", lambda: _copy_text(item.text(2)))
            if pid is not None:
                session.set_focus(mode=2, pid=pid)
                menu.addAction(
                    "Open Live data",
                    lambda: tabs.setCurrentWidget(data_tab))
        if menu.actions():
            menu.exec(freeze_tree.viewport().mapToGlobal(pos))

    def select_pid(pid: int):
        pid = int(pid or 0) & 0xFF
        tabs.setCurrentWidget(data_tab)
        for i in range(val_tree.topLevelItemCount()):
            it = val_tree.topLevelItem(i)
            if it and _pid_from_item(it) == pid:
                val_tree.setCurrentItem(it)
                val_tree.scrollToItem(it)
                return
        # Not in live table yet — still remember focus for next refresh
        session.set_focus(mode=1, pid=pid)

    def _on_focus(mode, pid):
        if int(mode or 0) in (0, 1, 2) and int(pid or 0):
            select_pid(int(pid))

    session.on_focus(_on_focus)
    refresher = QTimer(root)
    refresher.timeout.connect(refresh)
    refresher.start(400)

    poll_timer = QTimer(root)

    def on_poll_tick():
        if not _polling:
            return
        targets = sorted(p for p in _supported if p in PIDS) or sorted(PIDS)
        for pid in targets[:12]:
            _request(1, pid, functional=(_tx_id == 0x7DF))

    poll_timer.timeout.connect(on_poll_tick)

    def on_support():
        _supported.clear()
        for base in (0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0):
            _request(1, base)

    def on_freeze():
        del _freeze[:]
        # Request freeze frame 0 for common PIDs
        for pid in (0x0C, 0x0D, 0x05, 0x04, 0x11):
            _request(2, pid, functional=False)

    def on_clear():
        if QMessageBox.question(
                parent, "Confirm",
                "Clear DTCs and freeze frames?\nThis affects readiness monitors.") != \
                QMessageBox.StandardButton.Yes:
            return
        _request(4, 0)

    def on_poll():
        global _polling
        _polling = not _polling
        poll_btn.setText("Stop poll" if _polling else "Start poll")
        if _polling:
            poll_timer.start(poll_spin.value())
            plugin_shell.set_status(parent, "Polling")
        else:
            poll_timer.stop()
            plugin_shell.set_status(parent, "Idle")

    def on_export():
        rows = []
        for pid, (text, unit, ts) in sorted(_values.items()):
            name = PIDS[pid][0] if pid in PIDS else "PID %02X" % pid
            rows.append(["0x%02X" % pid, name, text, unit, "%.1f" % ts])
        path = plugin_shell.export_csv(
            parent, ["PID", "Name", "Value", "Unit", "Time"], rows, "obd2_data.csv")
        if path:
            plugin_shell.set_status(parent, "Exported %s" % path, 4000)

    support_btn.clicked.connect(on_support)
    dtc_btn.clicked.connect(lambda: _request(3, 0))
    pending_btn.clicked.connect(lambda: _request(7, 0))
    clear_btn.clicked.connect(on_clear)

    def on_vin():
        global _vin
        _vin = ""
        _request(9, 0x02, functional=False)

    def on_readiness():
        if hasattr(parent, "run_action"):
            parent.run_action("obd.goto", page="readiness")
        elif hasattr(parent, "goto_page"):
            parent.goto_page("readiness")

    vin_btn.clicked.connect(on_vin)
    freeze_btn.clicked.connect(on_freeze)
    poll_btn.clicked.connect(on_poll)
    export_btn.clicked.connect(on_export)
    readiness_btn.clicked.connect(on_readiness)
    filter_edit.textChanged.connect(lambda _t: refresh())
    val_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    val_tree.customContextMenuRequested.connect(_val_menu)
    dtc_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    dtc_tree.customContextMenuRequested.connect(_dtc_menu)
    freeze_tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    freeze_tree.customContextMenuRequested.connect(_freeze_menu)

    def _on_val_double(item, _col):
        pid = _pid_from_item(item)
        if pid is None:
            return
        session.set_focus(mode=1, pid=pid)
        _request(1, pid, functional=(_tx_id == 0x7DF))

    val_tree.itemDoubleClicked.connect(_on_val_double)
    plugin_shell.bind_shortcut(parent, "Ctrl+E", on_export)

    def on_poll_ms(v):
        prev = state_store.load_state(SUITE_ID, "settings.json") or {}
        prev["poll_ms"] = int(v)
        state_store.save_state(SUITE_ID, prev, "settings.json")

    poll_spin.valueChanged.connect(on_poll_ms)

    saved = state_store.load_state(SUITE_ID, "settings.json") or {}
    if saved.get("poll_ms"):
        poll_spin.setValue(int(saved["poll_ms"]))

    root.do_discover = on_support  # type: ignore[attr-defined]
    root.do_export = on_export  # type: ignore[attr-defined]
    root.select_pid = select_pid  # type: ignore[attr-defined]
    _ui.polish_work_surface(root)
    return root
