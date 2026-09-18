# -*- coding: utf-8 -*-
"""SharedSession — one ISO-TP + UDS stack for the whole UDS Suite."""

from __future__ import annotations

from typing import Callable, Optional

import sin
from PyQt6.QtCore import QObject, QTimer

from core import IsotpLayer, UdsClient, SESSIONS, encode_3e


class SharedSession(QObject):
    """Suite-wide connection + protocol stack.

    Pages must not create their own IsotpLayer/UdsClient for normal TX/RX.
    Scan may temporarily use IsotpClient for range probing (documented there).
    """

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.tx_id = 0x7E0
        self.rx_id = 0x7E8
        self.func_id = 0x7DF
        self.functional = False
        self.session_name = "unknown"
        self.tester_present = False
        self.flashing = False
        self.show_isotp_frames = False

        self._id_listeners: list[Callable[[], None]] = []
        self._session_listeners: list[Callable[[str], None]] = []
        self._log_fn: Optional[Callable] = None

        def _send_frame(can_id, data):
            sin.frames.send(can_id, data)

        self.isotp = IsotpLayer(_send_frame, qt_parent=self)
        self.client = UdsClient(self.isotp, qt_parent=self)
        self._apply_ids_to_stack()

        self.isotp.on_error = lambda msg: self.log("ERR", "-", b"", msg)
        self.client.on_pdu = lambda resp, desc: self.log(
            "RX", self.rx_id, resp, desc)
        self.client.on_pending = lambda: self.log(
            "RX", self.rx_id, b"\x7F\x00\x78",
            "NRC 0x78 ResponsePending — waiting P2*")

        self._tp_timer = QTimer(self)
        self._tp_timer.setInterval(2000)
        self._tp_timer.timeout.connect(self._on_tp_tick)
        self._tp_timer.start(2000)

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

        def _isotp_log(d, cid, data, note):
            if not self.show_isotp_frames:
                return
            direction = "FC" if "FC" in note else ("TX" if d == "TX" else "RX")
            self.log(direction, cid, bytes(data), "[ISO-TP %s]" % note)

        self.isotp.on_log = _isotp_log

    def log(self, direction, can_id, pdu, note, color=None):
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def on_ids_changed(self, cb: Callable[[], None]) -> None:
        self._id_listeners.append(cb)

    def on_session_changed(self, cb: Callable[[str], None]) -> None:
        self._session_listeners.append(cb)

    def _notify_ids(self) -> None:
        for cb in list(self._id_listeners):
            try:
                cb()
            except Exception:
                pass

    def set_session_name(self, name: str) -> None:
        self.session_name = name
        for cb in list(self._session_listeners):
            try:
                cb(name)
            except Exception:
                pass

    def _apply_ids_to_stack(self) -> None:
        self.isotp.tx_id = self.tx_id
        self.isotp.rx_id = self.rx_id
        self.isotp.func_id = self.func_id

    def apply_ids(self, tx_id: int, rx_id: int, func_id: Optional[int] = None) -> None:
        self.tx_id = int(tx_id)
        self.rx_id = int(rx_id)
        if func_id is not None:
            self.func_id = int(func_id)
        self._apply_ids_to_stack()
        self._notify_ids()
        self.log(
            "RX", "-", b"",
            "Connection IDs: TX 0x%X / RX 0x%X / Func 0x%X" % (
                self.tx_id, self.rx_id, self.func_id))

    def on_frame(self, frame) -> None:
        if frame.data:
            self.isotp.on_frame(frame.id, bytes(frame.data))

    def request(
        self,
        pdu,
        on_done=None,
        expect_response=True,
        tag="Request",
        functional=None,
    ):
        """Send UDS PDU via shared stack; log TX row."""
        use_func = self.functional if functional is None else functional
        self._apply_ids_to_stack()
        cid = self.func_id if use_func else self.tx_id
        self.log("TX", cid, pdu, tag)

        def _wrap(ok, resp, note):
            if not ok:
                self.log("ERR", "-", b"", note or "Failed")
            if on_done:
                on_done(ok, resp, note)

        self.client.request(
            pdu,
            functional=use_func,
            expect_response=expect_response and not use_func,
            on_done=_wrap,
        )

    def go_session(self, session: int) -> None:
        from core import encode_10

        def cb(ok, resp, note):
            if ok and resp and resp[:1] == b"\x50":
                name = SESSIONS.get(resp[1], "0x%02X" % resp[1])
                self.set_session_name(name)

        self.request(encode_10(session), on_done=cb, tag="SessionControl 10")

    def _on_tp_tick(self) -> None:
        if (self.tester_present and self.isotp._alive
                and not self.flashing and not self.client.busy):
            self._apply_ids_to_stack()
            pdu = encode_3e(0x80)
            self.isotp.send(pdu)
            self.log("TX", self.tx_id, pdu, "TesterPresent 3E 80 (heartbeat)")

    def shutdown(self) -> None:
        self._tp_timer.stop()
        self.client.shutdown()
        self.isotp.shutdown()
