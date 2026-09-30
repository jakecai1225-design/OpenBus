# -*- coding: utf-8 -*-
"""Scan workspace — ECU ID range probe.

Uses a temporary IsotpClient for range probing so SharedSession IDs stay
stable for Diagnose/Batch/Security. "Apply to connection" writes the selected
row into SharedSession.
"""

from __future__ import annotations

import time

from PyQt6.QtCore import QTimer, Qt
from PyQt6.QtGui import QGuiApplication
from PyQt6.QtWidgets import (
    QCheckBox,
    QHeaderView,
    QLineEdit,
    QMenu,
    QMessageBox,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

import sin
from _shared import plugin_shell, state_store
from _shared.isotp_client import IsotpClient

PLUGIN_ID = "uds-suite"

PROBE_SERVICES = [
    (0x10, bytes([0x10, 0x03]), "Session 03"),
    (0x22, bytes([0x22, 0xF1, 0x90]), "Read VIN"),
]


def _hex(data):
    return " ".join("%02X" % b for b in data)


def build(parent, session, log_fn) -> QWidget:
    from pages import _ui
    from widgets.step_spin import StepSpin

    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    start_edit = QLineEdit("0x7E0")
    start_edit.setFixedHeight(_ui.CTRL_H)
    start_edit.setMaximumWidth(100)
    end_edit = QLineEdit("0x7E7")
    end_edit.setFixedHeight(_ui.CTRL_H)
    end_edit.setMaximumWidth(100)
    offset_edit = QLineEdit("0x08")
    offset_edit.setFixedHeight(_ui.CTRL_H)
    offset_edit.setMaximumWidth(80)
    timeout_spin = StepSpin(600, minimum=100, maximum=5000, suffix=" ms", width=100)
    probe_check = QCheckBox("Probe 10 03 + VIN")
    probe_check.setChecked(True)
    probe_check.setToolTip("Also probe session (10 03) and VIN (22 F190)")

    scan_btn = _ui.primary_btn("Start", "Start range scan", "start")
    stop_btn = _ui.ghost_btn("Stop", "Stop scan", "stop")
    apply_btn = _ui.ghost_btn("Apply", "Write selected IDs into shared session", "apply")
    clear_btn = _ui.ghost_btn("Clear", "Clear results", "clear")
    export_btn = _ui.ghost_btn("Export", "Export results as CSV", "export")
    stop_btn.setEnabled(False)

    layout.addWidget(_ui.tool_strip(
        _ui.strip_field("From", start_edit, tip="First request ID (hex)"),
        _ui.strip_field("To", end_edit, tip="Last request ID (hex)"),
        _ui.strip_field(
            "Offset", offset_edit, tip="Response ID = request + offset"),
        _ui.strip_field("Timeout", timeout_spin, tip="Per-ID timeout"),
        probe_check,
        scan_btn, stop_btn, apply_btn, clear_btn, export_btn,
        stretch_at=5))

    body = QWidget()
    body.setObjectName("SuiteContent")
    body_l = QVBoxLayout(body)
    body_l.setContentsMargins(12, 8, 12, 8)
    body_l.setSpacing(6)

    tree = QTreeWidget()
    tree.setHeaderLabels([
        "Request ID", "Response ID", "Status", "Services", "Latency ms", "Last PDU",
    ])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.setToolTip("Online ECUs. Select a row, then Apply.")
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tree.header().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
    empty = plugin_shell.empty_state_label(
        "No ECUs yet. Set the range, then Start.")
    body_l.addWidget(tree, 1)
    body_l.addWidget(empty)
    layout.addWidget(body, 1)

    state = {
        "client": None,
        "scanning": False,
        "results": [],
        "ids": [],
        "idx": 0,
        "phase": "tp",
        "probe_i": 0,
        "t0": 0.0,
        "current": None,
        "frame_hook": None,
    }

    step_timer = QTimer(root)
    step_timer.setSingleShot(True)

    def _plog(text):
        log_fn("RX", "-", b"", "[Scan] %s" % text)

    def _persist():
        state_store.save_state(PLUGIN_ID, {
            "scan_start": start_edit.text().strip(),
            "scan_end": end_edit.text().strip(),
            "scan_offset": offset_edit.text().strip(),
            "scan_timeout_ms": timeout_spin.value(),
            "scan_probe": probe_check.isChecked(),
        }, "scan.json")

    def _refresh_tree():
        tree.clear()
        for r in state["results"]:
            svcs = ", ".join(
                "%s:%s" % (k, v) for k, v in sorted(r.get("services", {}).items()))
            tree.addTopLevelItem(QTreeWidgetItem([
                r["req"], r["rsp"], r["status"], svcs,
                "%.0f" % r.get("lat", 0), r.get("hex", ""),
            ]))
        empty.setVisible(len(state["results"]) == 0)
        tree.setVisible(len(state["results"]) > 0)

    def _ensure_client():
        if state["client"] is None:
            c = IsotpClient(sin.frames.send, root)
            c.on_error = lambda msg: _plog("ISO-TP: %s" % msg)
            state["client"] = c

            def _on_frame(frame):
                if state["client"] is not None:
                    state["client"].on_frame(frame.id, frame.data)

            if hasattr(parent, "_context") and parent._context is not None:
                parent._context.on_frame(_on_frame)
            state["frame_hook"] = _on_frame
        return state["client"]

    def _finish_scan():
        state["scanning"] = False
        step_timer.stop()
        scan_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        online = sum(1 for r in state["results"] if r.get("online"))
        summary = "Scan done: %d IDs, %d online" % (len(state["results"]), online)
        session.note_scan_hits(online)
        _plog(summary)
        plugin_shell.set_status(parent.window(), summary, 6000)
        _persist()

    def _advance():
        if not state["scanning"]:
            return
        state["idx"] += 1
        state["phase"] = "tp"
        state["probe_i"] = 0
        _probe_or_next()

    def _probe_or_next():
        if not state["scanning"]:
            return
        ids = state["ids"]
        idx = state["idx"]
        if idx >= len(ids):
            _finish_scan()
            return
        if state["phase"] == "tp":
            _send_tester(ids[idx])
            return
        if not probe_check.isChecked():
            _advance()
            return
        pi = state["probe_i"]
        if pi >= len(PROBE_SERVICES):
            _advance()
            return
        _send_probe(ids[idx], pi)

    def _send_tester(req_id):
        c = _ensure_client()
        offset = int(offset_edit.text(), 0)
        c.tx_id = req_id
        c.rx_id = req_id + offset
        row = {
            "req": "0x%03X" % req_id,
            "rsp": "0x%03X" % (req_id + offset),
            "status": "Probing...",
            "services": {},
            "lat": 0.0,
            "hex": "",
            "online": False,
            "req_id": req_id,
            "rsp_id": req_id + offset,
        }
        while len(state["results"]) <= state["idx"]:
            state["results"].append(row)
        state["results"][state["idx"]] = row
        state["current"] = row
        state["t0"] = time.time()
        state["phase"] = "tp"

        def on_pdu(pdu):
            if not state["scanning"] or state["current"] is not row:
                return
            lat = (time.time() - state["t0"]) * 1000.0
            row["lat"] = lat
            row["hex"] = _hex(pdu)
            row["online"] = True
            if len(pdu) >= 2 and pdu[0] == 0x7E:
                row["status"] = "Online (3E)"
                row["services"]["3E"] = "OK"
            elif len(pdu) >= 3 and pdu[0] == 0x7F:
                nrc = pdu[2]
                row["status"] = "Online (NRC %02X)" % nrc
                row["services"]["3E"] = "NRC %02X" % nrc
            else:
                row["status"] = "Online"
                row["services"]["3E"] = _hex(pdu[:8])
            _plog("%s → %s (%.0f ms)" % (row["req"], row["status"], lat))
            _refresh_tree()
            if probe_check.isChecked():
                state["phase"] = "probe"
                state["probe_i"] = 0
                QTimer.singleShot(80, _probe_or_next)
            else:
                QTimer.singleShot(50, _advance)

        c.on_received = on_pdu
        c.send(bytes([0x3E, 0x00]))
        _plog("TesterPresent @ %s (%d/%d)" % (
            row["req"], state["idx"] + 1, len(state["ids"])))
        step_timer.stop()
        try:
            step_timer.timeout.disconnect()
        except TypeError:
            pass
        step_timer.timeout.connect(lambda: _on_step_timeout(row))
        step_timer.start(timeout_spin.value())
        _refresh_tree()

    def _on_step_timeout(row):
        if not state["scanning"] or state["current"] is not row:
            return
        if not row.get("online"):
            row["status"] = "No response"
            row["lat"] = float(timeout_spin.value())
            _plog("%s — no response" % row["req"])
            _refresh_tree()
            QTimer.singleShot(30, _advance)
        elif state["phase"] == "probe":
            state["probe_i"] += 1
            QTimer.singleShot(30, _probe_or_next)

    def _send_probe(req_id, probe_i):
        c = _ensure_client()
        offset = int(offset_edit.text(), 0)
        c.tx_id = req_id
        c.rx_id = req_id + offset
        sid, pdu_req, label = PROBE_SERVICES[probe_i]
        row = state["current"]
        state["t0"] = time.time()
        state["probe_i"] = probe_i

        def on_pdu(pdu):
            if not state["scanning"] or state["current"] is not row:
                return
            key = "%02X" % sid
            if len(pdu) >= 3 and pdu[0] == 0x7F:
                row["services"][key] = "NRC %02X" % pdu[2]
            else:
                row["services"][key] = _hex(pdu[:12])
            _plog("Probe %s @ %s → %s" % (label, row["req"], row["services"][key]))
            _refresh_tree()
            state["probe_i"] = probe_i + 1
            QTimer.singleShot(80, _probe_or_next)

        c.on_received = on_pdu
        c.send(pdu_req)
        step_timer.stop()
        try:
            step_timer.timeout.disconnect()
        except TypeError:
            pass
        step_timer.timeout.connect(lambda: _on_step_timeout(row))
        step_timer.start(timeout_spin.value())

    def on_scan():
        try:
            start = int(start_edit.text(), 0)
            end = int(end_edit.text(), 0)
            int(offset_edit.text(), 0)
        except ValueError:
            QMessageBox.warning(
                root, "Config error",
                "IDs and offset must be integers (hex OK, e.g. 0x7E0)")
            return
        if end < start or (end - start) > 256:
            QMessageBox.warning(
                root, "Range error",
                "End must be >= start and range size <= 256")
            return
        _persist()
        state["results"] = []
        state["ids"] = list(range(start, end + 1))
        state["idx"] = 0
        state["phase"] = "tp"
        state["probe_i"] = 0
        state["scanning"] = True
        scan_btn.setEnabled(False)
        stop_btn.setEnabled(True)
        empty.setVisible(False)
        tree.setVisible(True)
        tree.clear()
        _plog("Scan %s .. %s (offset %s)" % (
            start_edit.text(), end_edit.text(), offset_edit.text()))
        _probe_or_next()

    def on_stop():
        state["scanning"] = False
        step_timer.stop()
        scan_btn.setEnabled(True)
        stop_btn.setEnabled(False)
        _plog("Scan stopped by user")

    def on_apply():
        sel = tree.selectedItems()
        if not sel:
            QMessageBox.information(root, "Hint", "Select a scan result row first")
            return
        idx = tree.indexOfTopLevelItem(sel[0])
        if idx < 0 or idx >= len(state["results"]):
            return
        r = state["results"][idx]
        if not r.get("online"):
            QMessageBox.warning(root, "Offline", "Selected ID did not respond")
            return
        shell = parent
        if hasattr(shell, "run_action"):
            shell.run_action(
                "uds.apply_scan_ids",
                req_id=r["req_id"], rsp_id=r["rsp_id"])
        else:
            session.apply_ids(r["req_id"], r["rsp_id"])
        session.note_scan_hits(sum(1 for x in state["results"] if x.get("online")))
        _plog("Applied TX 0x%X / RX 0x%X to connection" % (
            r["req_id"], r["rsp_id"]))
        plugin_shell.set_status(
            parent.window(),
            "Applied %s → %s" % (r["req"], r["rsp"]), 4000)

    def on_clear():
        if state["scanning"]:
            return
        state["results"] = []
        tree.clear()
        empty.setVisible(True)
        plugin_shell.set_status(parent.window(), "Scan cleared", 2000)

    def on_export():
        export_rows = []
        for r in state["results"]:
            svcs = "; ".join(
                "%s=%s" % (k, v) for k, v in sorted(r.get("services", {}).items()))
            export_rows.append([
                r.get("req", ""), r.get("rsp", ""), r.get("status", ""),
                svcs, "%.0f" % r.get("lat", 0), r.get("hex", ""),
            ])
        path = plugin_shell.export_csv(
            root,
            ["Request ID", "Response ID", "Status", "Services", "Latency ms", "Last PDU"],
            export_rows,
            "uds_scan.csv",
        )
        if path:
            plugin_shell.set_status(parent.window(), "Exported %s" % path, 4000)

    scan_btn.clicked.connect(on_scan)
    stop_btn.clicked.connect(on_stop)
    apply_btn.clicked.connect(on_apply)
    clear_btn.clicked.connect(on_clear)
    export_btn.clicked.connect(on_export)

    def _copy_text(text: str):
        if not text:
            return
        QGuiApplication.clipboard().setText(str(text))
        plugin_shell.set_status(parent.window(), "Copied", 1500)

    def _scan_menu(pos):
        item = tree.itemAt(pos)
        menu = QMenu(tree)
        if item is None:
            menu.addAction("Start scan", on_scan)
            menu.addAction("Clear", on_clear)
        else:
            tree.setCurrentItem(item)
            menu.addAction(
                "Copy request ID", lambda: _copy_text(item.text(0)))
            menu.addAction(
                "Copy response ID", lambda: _copy_text(item.text(1)))
            menu.addSeparator()
            menu.addAction("Apply to connection", on_apply)
            menu.addAction(
                "Open Services…",
                lambda: parent.run_action("uds.goto", page="services")
                if hasattr(parent, "run_action")
                else parent.goto_page("services"))
            menu.addAction(
                "Open DID…",
                lambda: parent.run_action("uds.goto", page="did")
                if hasattr(parent, "run_action")
                else parent.goto_page("did"))
        if menu.actions():
            menu.exec(tree.viewport().mapToGlobal(pos))

    tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    tree.customContextMenuRequested.connect(_scan_menu)

    saved = state_store.load_state(PLUGIN_ID, "scan.json") or {}
    if saved.get("scan_start"):
        start_edit.setText(str(saved["scan_start"]))
    if saved.get("scan_end"):
        end_edit.setText(str(saved["scan_end"]))
    if saved.get("scan_offset"):
        offset_edit.setText(str(saved["scan_offset"]))
    if saved.get("scan_timeout_ms"):
        timeout_spin.setValue(int(saved["scan_timeout_ms"]))
    if "scan_probe" in saved:
        probe_check.setChecked(bool(saved["scan_probe"]))

    empty.setVisible(True)
    tree.setVisible(False)
    _ui.polish_work_surface(root)
    return root
