# -*- coding: utf-8 -*-
"""XCP on CAN master (CTO lite) — CONNECT, GET_STATUS, SHORT_UPLOAD, DOWNLOAD, DAQ lite."""

from __future__ import annotations

import struct
import time
from typing import Callable, Dict, List, Optional, Tuple

from PyQt6.QtCore import QObject, QTimer

# CTO command codes (ASAM XCP)
CMD_CONNECT = 0xFF
CMD_DISCONNECT = 0xFE
CMD_GET_STATUS = 0xFD
CMD_SYNCH = 0xFC
CMD_GET_ID = 0xFA
CMD_SET_MTA = 0xF6
CMD_UPLOAD = 0xF5
CMD_SHORT_UPLOAD = 0xF4
CMD_DOWNLOAD = 0xF0
CMD_DOWNLOAD_MAX = 0xEE
CMD_SET_CAL_PAGE = 0xEB
CMD_GET_CAL_PAGE = 0xEA
CMD_FREE_DAQ = 0xD6
CMD_ALLOC_DAQ = 0xD5
CMD_ALLOC_ODT = 0xD4
CMD_ALLOC_ODT_ENTRY = 0xD3
CMD_SET_DAQ_PTR = 0xE2
CMD_WRITE_DAQ = 0xE1
CMD_SET_DAQ_LIST_MODE = 0xE0
CMD_START_STOP_DAQ_LIST = 0xDE
CMD_START_STOP_SYNCH = 0xDD

PID_RES = 0xFF
PID_ERR = 0xFE


def encode_connect(mode: int = 0) -> bytes:
    return bytes([CMD_CONNECT, mode & 0xFF])


def encode_disconnect() -> bytes:
    return bytes([CMD_DISCONNECT])


def encode_get_status() -> bytes:
    return bytes([CMD_GET_STATUS])


def encode_short_upload(size: int, addr_ext: int, address: int) -> bytes:
    return bytes([
        CMD_SHORT_UPLOAD, size & 0xFF, 0, addr_ext & 0xFF,
    ]) + struct.pack("<I", int(address) & 0xFFFFFFFF)


def encode_set_mta(addr_ext: int, address: int) -> bytes:
    return bytes([CMD_SET_MTA, 0, 0, addr_ext & 0xFF]) + struct.pack(
        "<I", int(address) & 0xFFFFFFFF)


def encode_download(data: bytes) -> bytes:
    raw = bytes(data or b"")[:6]
    return bytes([CMD_DOWNLOAD, len(raw)]) + raw + bytes(6 - len(raw))


def encode_set_cal_page(mode: int, seg: int, page: int) -> bytes:
    return bytes([CMD_SET_CAL_PAGE, mode & 0xFF, seg & 0xFF, page & 0xFF])


def encode_get_cal_page(mode: int, seg: int) -> bytes:
    return bytes([CMD_GET_CAL_PAGE, mode & 0xFF, seg & 0xFF])


def encode_start_stop_synch(mode: int) -> bytes:
    """mode: 0=stop, 1=start, 2=select."""
    return bytes([CMD_START_STOP_SYNCH, mode & 0xFF])


def encode_alloc_daq(count: int) -> bytes:
    return bytes([CMD_ALLOC_DAQ, 0, count & 0xFF, (count >> 8) & 0xFF])


def decode_cto(data: bytes) -> Tuple[str, dict]:
    if not data:
        return ("empty", {})
    pid = data[0]
    if pid == PID_ERR:
        code = data[1] if len(data) > 1 else 0
        return ("err", {"code": code})
    if pid == PID_RES:
        return ("res", {"payload": bytes(data[1:])})
    # DAQ DTO: PID 0x00..0xFB typically
    if pid < 0xFC:
        return ("daq", {"pid": pid, "payload": bytes(data[1:])})
    return ("other", {"pid": pid, "payload": bytes(data[1:])})


class XcpClient(QObject):
    """One outstanding CTO at a time over CAN."""

    def __init__(
        self,
        send_fn: Callable[[int, bytes], None],
        parent: Optional[QObject] = None,
        timeout_ms: int = 500,
    ):
        super().__init__(parent)
        self._send = send_fn
        self.timeout_ms = timeout_ms
        self.master_id = 0x6B0
        self.slave_id = 0x6B1
        self.connected = False
        self._pending = None
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._on_timeout)
        self.on_log: Optional[Callable] = None
        self.on_daq: Optional[Callable[[int, bytes], None]] = None
        self.max_cto = 8
        self.max_dto = 8

    def set_ids(self, master_id: int, slave_id: int) -> None:
        self.master_id = int(master_id) & 0x7FF
        self.slave_id = int(slave_id) & 0x7FF

    @property
    def busy(self) -> bool:
        return self._pending is not None

    def cancel(self) -> None:
        self._timer.stop()
        self._pending = None

    def _arm(self, op: str, on_done) -> None:
        self._pending = {"op": op, "on_done": on_done, "ts": time.time()}
        self._timer.start(self.timeout_ms)

    def _finish(self, ok: bool, payload, note: str) -> None:
        pend = self._pending
        self._timer.stop()
        self._pending = None
        if not pend:
            return
        cb = pend.get("on_done")
        if cb:
            cb(ok, payload, note)

    def _tx(self, pdu: bytes, note: str) -> None:
        raw = bytes(pdu or b"")[:8]
        self._send(self.master_id, raw)
        if self.on_log:
            self.on_log("TX", self.master_id, raw, note)

    def connect(self, on_done=None) -> bool:
        if self._pending:
            if on_done:
                on_done(False, None, "XCP busy")
            return False
        self._arm("connect", on_done)
        self._tx(encode_connect(0), "XCP CONNECT")
        return True

    def disconnect(self, on_done=None) -> bool:
        if self._pending:
            if on_done:
                on_done(False, None, "XCP busy")
            return False
        self._arm("disconnect", on_done)
        self._tx(encode_disconnect(), "XCP DISCONNECT")
        return True

    def get_status(self, on_done=None) -> bool:
        if self._pending:
            if on_done:
                on_done(False, None, "XCP busy")
            return False
        self._arm("get_status", on_done)
        self._tx(encode_get_status(), "XCP GET_STATUS")
        return True

    def short_upload(
        self, address: int, size: int = 4, addr_ext: int = 0, on_done=None
    ) -> bool:
        if self._pending:
            if on_done:
                on_done(False, None, "XCP busy")
            return False
        size = max(1, min(7, int(size)))
        self._arm("short_upload", on_done)
        self._tx(
            encode_short_upload(size, addr_ext, address),
            "XCP SHORT_UPLOAD 0x%X (%d)" % (address, size))
        return True

    def download(
        self, address: int, data: bytes, addr_ext: int = 0, on_done=None
    ) -> bool:
        if self._pending:
            if on_done:
                on_done(False, None, "XCP busy")
            return False
        raw = bytes(data or b"")
        self._arm("download", on_done)
        self._pending["addr"] = address
        self._pending["addr_ext"] = addr_ext
        self._pending["data"] = raw
        self._pending["phase"] = "mta"
        self._tx(
            encode_set_mta(addr_ext, address),
            "XCP SET_MTA 0x%X" % address)
        return True

    def set_cal_page(self, page: int, on_done=None) -> bool:
        if self._pending:
            if on_done:
                on_done(False, None, "XCP busy")
            return False
        self._arm("set_cal_page", on_done)
        self._tx(encode_set_cal_page(0x03, 0, page), "XCP SET_CAL_PAGE %d" % page)
        return True

    def start_daq(self, on_done=None) -> bool:
        if self._pending:
            if on_done:
                on_done(False, None, "XCP busy")
            return False
        self._arm("start_daq", on_done)
        self._tx(encode_start_stop_synch(1), "XCP START_STOP_SYNCH start")
        return True

    def stop_daq(self, on_done=None) -> bool:
        if self._pending:
            if on_done:
                on_done(False, None, "XCP busy")
            return False
        self._arm("stop_daq", on_done)
        self._tx(encode_start_stop_synch(0), "XCP START_STOP_SYNCH stop")
        return True

    def on_frame(self, can_id: int, data: bytes) -> None:
        if can_id != self.slave_id:
            return
        kind, fields = decode_cto(data)
        if kind == "daq":
            if self.on_daq:
                self.on_daq(fields.get("pid", 0), fields.get("payload") or b"")
            return
        if not self._pending:
            return
        pend = self._pending
        if kind == "err":
            note = "XCP ERR 0x%02X" % fields.get("code", 0)
            if self.on_log:
                self.on_log("ERR", can_id, data, note)
            if pend.get("op") == "connect":
                self.connected = False
            self._finish(False, None, note)
            return
        if kind != "res":
            return
        payload = fields.get("payload") or b""
        if self.on_log:
            self.on_log("RX", can_id, data, "XCP RES %s" % pend["op"])

        op = pend["op"]
        if op == "connect":
            self.connected = True
            if len(payload) >= 6:
                self.max_cto = payload[2] or 8
                self.max_dto = payload[3] or 8
            self._finish(True, payload, "OK")
            return
        if op == "disconnect":
            self.connected = False
            self._finish(True, payload, "OK")
            return
        if op == "download" and pend.get("phase") == "mta":
            pend["phase"] = "data"
            self._tx(encode_download(pend.get("data") or b""), "XCP DOWNLOAD")
            self._timer.start(self.timeout_ms)
            return
        self._finish(True, payload, "OK")

    def _on_timeout(self) -> None:
        pend = self._pending
        self._pending = None
        if not pend:
            return
        note = "XCP timeout (%s)" % pend.get("op")
        if self.on_log:
            self.on_log("ERR", "-", b"", note)
        cb = pend.get("on_done")
        if cb:
            cb(False, None, note)
