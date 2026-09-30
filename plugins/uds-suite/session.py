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

        self.tp_interval_ms = 2000
        self._tp_timer = QTimer(self)
        self._tp_timer.setInterval(self.tp_interval_ms)
        self._tp_timer.timeout.connect(self._on_tp_tick)
        self._tp_timer.start(self.tp_interval_ms)

        # Cross-page focus + Context Next (plugin development norm).
        self.focus_leaf = "services"
        self.focus_service = 0
        self.focus_did = 0
        self.focus_dtc = ""
        self._focus_listeners: list[Callable] = []
        self._ids_touched = False
        self._did_request = False
        self._scan_hits = 0
        self._post_hint_stage = 0
        self._profile_path = ""

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
        self._ids_touched = True
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
        self._did_request = True

        def _wrap(ok, resp, note):
            if resp and len(resp) >= 3 and resp[0] == 0x7F:
                from core import NRCS
                name = NRCS.get(resp[2], "unknown")
                self.log("ERR", self.rx_id, bytes(resp),
                         "NRC 0x%02X %s" % (resp[2], name))
            elif not ok:
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
                # ISO 14229 session timing: P2 in ms, P2* in 10 ms units.
                if len(resp) >= 6:
                    p2 = int.from_bytes(resp[2:4], "big")
                    p2s = int.from_bytes(resp[4:6], "big") * 10
                    if p2 > 0:
                        self.client.p2_ms = p2
                    if p2s > 0:
                        self.client.p2star_ms = p2s
                    self.log("RX", self.rx_id, bytes(resp),
                             "%s  P2=%dms P2*=%dms" % (name, self.client.p2_ms,
                                                       self.client.p2star_ms))

        self.request(encode_10(session), on_done=cb, tag="SessionControl 10")

    def set_timing(self, p2_ms: int, p2star_ms: int, tp_ms: int) -> None:
        self.client.p2_ms = max(50, int(p2_ms))
        self.client.p2star_ms = max(self.client.p2_ms, int(p2star_ms))
        self.tp_interval_ms = max(200, int(tp_ms))
        self._tp_timer.setInterval(self.tp_interval_ms)

    def read_identity(self) -> None:
        """Workshop one-shot: read the usual identification DIDs (22 F1xx)."""
        dids = (
            (0xF186, "Active diagnostic session"),
            (0xF187, "Spare part number"),
            (0xF18A, "System supplier"),
            (0xF18C, "ECU serial"),
            (0xF190, "VIN"),
            (0xF191, "ECU hardware"),
            (0xF195, "ECU software"),
            (0xF197, "System name"),
        )
        from core import encode_22

        pending = list(dids)

        def _next():
            if not pending:
                self.log("RX", "-", b"", "Identification read finished")
                return
            did, name = pending.pop(0)

            def cb(ok, resp, note):
                if ok and resp and resp[:1] == b"\x62" and len(resp) > 3:
                    payload = bytes(resp[3:])
                    text = payload.decode("ascii", "replace").rstrip("\x00 ")
                    shown = text if text.isprintable() and text.strip() else payload.hex().upper()
                    self.log("RX", self.rx_id, bytes(resp),
                             "22 %04X %s = %s" % (did, name, shown))
                _next()

            self.request(encode_22(did), on_done=cb, tag="Read %04X %s" % (did, name))

        _next()

    def on_focus(self, cb: Callable) -> None:
        self._focus_listeners.append(cb)

    def set_focus(
            self, leaf: str = "", service: int = 0, did: int = 0,
            dtc: str = "", **_kwargs) -> None:
        leaf = (leaf or "").strip()
        if leaf:
            self.focus_leaf = leaf
        if service:
            self.focus_service = int(service) & 0xFF
        if did:
            self.focus_did = int(did) & 0xFFFF
        if dtc is not None and str(dtc).strip():
            self.focus_dtc = str(dtc).strip()
        for cb in list(self._focus_listeners):
            try:
                cb(self.focus_leaf, self.focus_service, self.focus_did,
                   self.focus_dtc)
            except Exception:
                pass

    def note_scan_hits(self, count: int) -> None:
        self._scan_hits = max(0, int(count))

    def advance_next_hint(self) -> None:
        self._post_hint_stage = min(4, int(self._post_hint_stage) + 1)

    def next_hint(self) -> tuple:
        """Return (label, action_id, kwargs) for Context Next."""
        if not self._ids_touched:
            return ("Set TX/RX IDs", "uds.goto", {"page": "session"})
        if (self.session_name or "unknown") == "unknown":
            return ("Open Extended", "uds.extended", {})
        if not self._did_request:
            return ("Send a service", "uds.goto", {"page": "services"})
        if self._scan_hits > 0 and self._post_hint_stage < 2:
            return ("Review Scan", "uds.goto", {"page": "scan"})
        if self._post_hint_stage < 3:
            return ("Run Batch", "uds.goto", {"page": "batch"})
        return ("Security audit", "uds.goto", {"page": "security"})

    def _on_tp_tick(self) -> None:
        if not (self.tester_present and self.isotp._alive):
            return
        # During download, keep the session with a functional 3E 80 so the
        # physical P2 slot stays free for 36 blocks (vFlash / TSMaster style).
        if self.flashing:
            pdu = encode_3e(0x80)
            self.isotp.send(pdu, functional=True)
            return
        if self.client.busy:
            return
        self._apply_ids_to_stack()
        pdu = encode_3e(0x80)
        self.isotp.send(pdu)
        self.log("TX", self.tx_id, pdu, "TesterPresent 3E 80 (heartbeat)")

    def shutdown(self) -> None:
        self._tp_timer.stop()
        self.client.shutdown()
        self.isotp.shutdown()
