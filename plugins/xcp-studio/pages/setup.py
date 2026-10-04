# -*- coding: utf-8 -*-
"""Setup — XCP on CAN IDs + Connect."""

from __future__ import annotations

from PyQt6.QtWidgets import QFormLayout, QVBoxLayout, QWidget

from pages import _ui


def build(shell, session, log_fn) -> QWidget:
    root = QWidget()
    lay = QVBoxLayout(root)
    lay.setContentsMargins(12, 8, 12, 8)
    lay.setSpacing(8)

    form_host = QWidget()
    form_host.setMaximumWidth(_ui.FORM_MAX_W)
    form = QFormLayout(form_host)
    form.setContentsMargins(0, 0, 0, 0)

    master = _ui.line_edit("Master (CRO) CAN ID", "0x6B0")
    slave = _ui.line_edit("Slave (DTO) CAN ID", "0x6B1")
    master.setText("0x%X" % session.master_id)
    slave.setText("0x%X" % session.slave_id)
    status = _ui.count_label("Disconnected")
    form.addRow("Master ID", master)
    form.addRow("Slave ID", slave)
    form.addRow("Link", status)

    apply_ids = _ui.ghost_btn("Apply IDs", "Set CRO / DTO CAN IDs", "gear")
    connect = _ui.primary_btn("Connect", "XCP CONNECT", "plug")
    disconnect = _ui.ghost_btn("Disconnect", "XCP DISCONNECT", "debug-disconnect")
    get_st = _ui.ghost_btn("Get status", "XCP GET_STATUS", "info")
    open_a2l = _ui.ghost_btn("Open A2L…", "Load description file", "file")
    edit_a2l = _ui.ghost_btn("Edit in A2L Studio", "Open the file studio", "go-to-file")

    lay.addWidget(form_host)
    lay.addWidget(_ui.tool_strip(
        apply_ids, connect, disconnect, get_st, stretch_at=4))
    lay.addWidget(_ui.tool_strip(open_a2l, edit_a2l, stretch_at=2))
    lay.addStretch(1)

    def _parse_id(text: str, fallback: int) -> int:
        t = (text or "").strip()
        try:
            return int(t, 0) & 0x7FF
        except ValueError:
            return fallback

    def _sync_status():
        status.setText("Connected" if session.xcp.connected else "Disconnected")

    def on_ids():
        session.set_ids(_parse_id(master.text(), session.master_id),
                        _parse_id(slave.text(), session.slave_id))
        log_fn("SYS", "-", b"", "IDs applied")

    def on_connect():
        on_ids()

        def done(ok, _p, note):
            _sync_status()
            log_fn("SYS" if ok else "ERR", "-", b"", note or "CONNECT")

        session.connect_xcp(on_done=done)

    def on_disc():
        def done(ok, _p, note):
            _sync_status()
            log_fn("SYS" if ok else "ERR", "-", b"", note or "DISCONNECT")

        session.disconnect_xcp(on_done=done)

    def on_status():
        def done(ok, payload, note):
            log_fn("SYS" if ok else "ERR", "-", b"", note or "GET_STATUS")
            if ok and payload:
                log_fn("RX", "-", bytes(payload), "status payload")

        session.xcp.get_status(on_done=done)

    apply_ids.clicked.connect(on_ids)
    connect.clicked.connect(on_connect)
    disconnect.clicked.connect(on_disc)
    get_st.clicked.connect(on_status)
    open_a2l.clicked.connect(lambda: shell.run_action("xcp.open_a2l"))
    edit_a2l.clicked.connect(lambda: shell.run_action("xcp.edit_a2l"))
    session.on_changed(_sync_status)
    _sync_status()
    return root
