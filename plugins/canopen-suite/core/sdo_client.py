# -*- coding: utf-8 -*-
"""Expedited SDO client (CCS upload/download) over sin.frames."""

from __future__ import annotations

import time
from typing import Callable, Optional, Tuple

from PyQt6.QtCore import QObject, QTimer


from core.cia301_codes import SDO_ABORT_CODES as ABORT_CODES  # noqa: E402


def encode_expedited_upload(index: int, subindex: int = 0) -> bytes:
    """CCS=2 initiate upload (read)."""
    return bytes([
        0x40,
        index & 0xFF, (index >> 8) & 0xFF,
        subindex & 0xFF,
        0, 0, 0, 0,
    ])


def encode_expedited_download(
    index: int, subindex: int, value: int, size: int = 4
) -> bytes:
    """CCS=1 initiate download (write), expedited, size 1..4 bytes."""
    size = max(1, min(4, int(size)))
    # n = 4 - size; e=1 expedited; s=1 size indicated; ccs=1
    cmd = 0x20 | (((4 - size) & 0x03) << 2) | 0x03
    data = int(value).to_bytes(4, "little", signed=False)
    return bytes([
        cmd,
        index & 0xFF, (index >> 8) & 0xFF,
        subindex & 0xFF,
        data[0], data[1], data[2], data[3],
    ])


def decode_sdo_response(data: bytes) -> Tuple[str, dict]:
    """Classify TSDO response. Returns (kind, fields).

    kind: expedited_upload | download_ack | abort | other
    """
    if not data or len(data) < 4:
        return ("other", {})
    cmd = data[0]
    index = data[1] | (data[2] << 8)
    sub = data[3]
    if cmd == 0x80:
        abort = 0
        if len(data) >= 8:
            abort = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
        return ("abort", {
            "index": index, "subindex": sub, "abort": abort,
            "message": ABORT_CODES.get(abort, "Abort 0x%08X" % abort),
        })
    # Server upload response: scs=2, expedited often 0x4x
    if (cmd & 0xE0) == 0x40:
        n = (cmd >> 2) & 0x03
        size = 4 - n if (cmd & 0x02) else 4
        val = 0
        if len(data) >= 8:
            raw = data[4:8]
            val = int.from_bytes(raw[:size], "little", signed=False)
        return ("expedited_upload", {
            "index": index, "subindex": sub, "value": val, "size": size,
            "raw": bytes(data[4:8]) if len(data) >= 8 else b"",
        })
    # Download response scs=3
    if (cmd & 0xE0) == 0x60:
        return ("download_ack", {"index": index, "subindex": sub})
    return ("other", {"index": index, "subindex": sub, "cmd": cmd})


class SdoClient(QObject):
    """One outstanding expedited SDO request at a time."""

    def __init__(
        self,
        send_fn: Callable[[int, bytes], None],
        parent: Optional[QObject] = None,
        timeout_ms: int = 800,
    ):
        super().__init__(parent)
        self._send = send_fn
        self.timeout_ms = timeout_ms
        self.node_id = 1
        self._pending = None  # dict
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._on_timeout)
        self.on_log: Optional[Callable] = None

    def set_node(self, node_id: int) -> None:
        self.node_id = max(1, min(127, int(node_id)))

    @property
    def busy(self) -> bool:
        return self._pending is not None

    def _tx_id(self) -> int:
        return 0x600 + self.node_id

    def _rx_id(self) -> int:
        return 0x580 + self.node_id

    def cancel(self) -> None:
        self._timer.stop()
        self._pending = None

    def upload(
        self,
        index: int,
        subindex: int = 0,
        on_done: Optional[Callable] = None,
    ) -> bool:
        """Expedited SDO upload (read). on_done(ok, value|None, note)."""
        if self._pending:
            if on_done:
                on_done(False, None, "SDO busy")
            return False
        pdu = encode_expedited_upload(index, subindex)
        self._pending = {
            "op": "upload",
            "index": index,
            "subindex": subindex,
            "on_done": on_done,
            "ts": time.time(),
        }
        self._send(self._tx_id(), pdu)
        if self.on_log:
            self.on_log(
                "TX", self._tx_id(), pdu,
                "SDO upload 0x%04X:%02X" % (index, subindex))
        self._timer.start(self.timeout_ms)
        return True

    def download(
        self,
        index: int,
        subindex: int,
        value: int,
        size: int = 4,
        on_done: Optional[Callable] = None,
    ) -> bool:
        """Expedited SDO download (write). on_done(ok, None, note)."""
        if self._pending:
            if on_done:
                on_done(False, None, "SDO busy")
            return False
        pdu = encode_expedited_download(index, subindex, value, size)
        self._pending = {
            "op": "download",
            "index": index,
            "subindex": subindex,
            "on_done": on_done,
            "ts": time.time(),
        }
        self._send(self._tx_id(), pdu)
        if self.on_log:
            self.on_log(
                "TX", self._tx_id(), pdu,
                "SDO download 0x%04X:%02X = 0x%X" % (index, subindex, value))
        self._timer.start(self.timeout_ms)
        return True

    def on_frame(self, can_id: int, data: bytes) -> None:
        if not self._pending:
            return
        if can_id != self._rx_id():
            return
        kind, fields = decode_sdo_response(data)
        pend = self._pending
        if fields.get("index") not in (None, pend["index"]):
            return
        if fields.get("subindex") not in (None, pend["subindex"]):
            return

        self._timer.stop()
        self._pending = None
        cb = pend.get("on_done")

        if kind == "abort":
            note = fields.get("message", "SDO abort")
            if self.on_log:
                self.on_log("ERR", can_id, data, note)
            if cb:
                cb(False, None, note)
            return

        if pend["op"] == "upload" and kind == "expedited_upload":
            val = fields.get("value")
            if self.on_log:
                self.on_log(
                    "RX", can_id, data,
                    "SDO 0x%04X:%02X = 0x%X" % (
                        pend["index"], pend["subindex"], val or 0))
            if cb:
                cb(True, val, "OK")
            return

        if pend["op"] == "download" and kind == "download_ack":
            if self.on_log:
                self.on_log("RX", can_id, data, "SDO write ACK")
            if cb:
                cb(True, None, "OK")
            return

        # Unexpected — re-arm only if we consumed; treat as other
        if self.on_log:
            self.on_log("RX", can_id, data, "SDO unexpected response")
        if cb:
            cb(False, None, "Unexpected SDO response")

    def _on_timeout(self) -> None:
        pend = self._pending
        self._pending = None
        if not pend:
            return
        note = "SDO timeout 0x%04X:%02X" % (pend["index"], pend["subindex"])
        if self.on_log:
            self.on_log("ERR", "-", b"", note)
        cb = pend.get("on_done")
        if cb:
            cb(False, None, note)
