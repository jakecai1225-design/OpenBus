# -*- coding: utf-8 -*-
"""Expedited + segmented SDO client (CCS upload/download) over sin.frames."""

from __future__ import annotations

import time
from typing import Callable, List, Optional, Tuple

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
    cmd = 0x20 | (((4 - size) & 0x03) << 2) | 0x03
    data = int(value).to_bytes(4, "little", signed=False)
    return bytes([
        cmd,
        index & 0xFF, (index >> 8) & 0xFF,
        subindex & 0xFF,
        data[0], data[1], data[2], data[3],
    ])


def encode_segmented_download_init(
    index: int, subindex: int, total_size: int
) -> bytes:
    """CCS=1 initiate download, non-expedited, size indicated."""
    size = max(0, int(total_size))
    cmd = 0x21  # ccs=1, e=0, s=1
    return bytes([
        cmd,
        index & 0xFF, (index >> 8) & 0xFF,
        subindex & 0xFF,
        size & 0xFF, (size >> 8) & 0xFF,
        (size >> 16) & 0xFF, (size >> 24) & 0xFF,
    ])


def encode_download_segment(toggle: int, chunk: bytes, last: bool) -> bytes:
    """CCS=0 download segment. chunk length 1..7."""
    raw = bytes(chunk or b"")[:7]
    n = 7 - len(raw)
    cmd = ((1 if last else 0) << 0) | ((toggle & 1) << 4) | ((n & 7) << 1)
    pad = raw + bytes(7 - len(raw))
    return bytes([cmd]) + pad


def encode_upload_segment(toggle: int) -> bytes:
    """CCS=3 upload segment request."""
    return bytes([0x60 | ((toggle & 1) << 4), 0, 0, 0, 0, 0, 0, 0])


def decode_sdo_response(data: bytes) -> Tuple[str, dict]:
    """Classify TSDO response. Returns (kind, fields)."""
    if not data or len(data) < 1:
        return ("other", {})
    cmd = data[0]
    index = data[1] | (data[2] << 8) if len(data) >= 3 else 0
    sub = data[3] if len(data) >= 4 else 0

    if cmd == 0x80:
        abort = 0
        if len(data) >= 8:
            abort = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
        return ("abort", {
            "index": index, "subindex": sub, "abort": abort,
            "message": ABORT_CODES.get(abort, "Abort 0x%08X" % abort),
        })

    # Initiate upload response: scs=2
    if (cmd & 0xE0) == 0x40:
        expedited = bool(cmd & 0x02)
        size_ind = bool(cmd & 0x01)
        if expedited:
            n = (cmd >> 2) & 0x03
            size = 4 - n if size_ind else 4
            val = 0
            raw = b""
            if len(data) >= 8:
                raw = bytes(data[4:8])
                val = int.from_bytes(raw[:size], "little", signed=False)
            return ("expedited_upload", {
                "index": index, "subindex": sub, "value": val, "size": size,
                "raw": raw,
            })
        # Segmented initiate: size in bytes 4..7 if s=1
        total = 0
        if size_ind and len(data) >= 8:
            total = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
        return ("upload_init_seg", {
            "index": index, "subindex": sub, "size": total,
        })

    # Download response initiate scs=3
    if (cmd & 0xE0) == 0x60:
        return ("download_ack", {"index": index, "subindex": sub})

    # Upload segment response scs=0
    if (cmd & 0xE0) == 0x00:
        toggle = (cmd >> 4) & 1
        n = (cmd >> 1) & 0x07
        last = bool(cmd & 0x01)
        count = 7 - n
        payload = bytes(data[1:1 + count]) if len(data) > 1 else b""
        return ("upload_segment", {
            "toggle": toggle, "last": last, "data": payload,
        })

    # Download segment response scs=1
    if (cmd & 0xE0) == 0x20:
        toggle = (cmd >> 4) & 1
        return ("download_seg_ack", {"toggle": toggle})

    return ("other", {"index": index, "subindex": sub, "cmd": cmd})


class SdoClient(QObject):
    """One outstanding SDO request (expedited or segmented)."""

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

    def _arm(self, pending: dict) -> None:
        self._pending = pending
        self._timer.start(self.timeout_ms)

    def _finish(self, ok: bool, value, note: str) -> None:
        pend = self._pending
        self._timer.stop()
        self._pending = None
        if not pend:
            return
        cb = pend.get("on_done")
        if cb:
            cb(ok, value, note)

    def upload(
        self,
        index: int,
        subindex: int = 0,
        on_done: Optional[Callable] = None,
    ) -> bool:
        """SDO upload (read). Expedited or segmented automatically."""
        if self._pending:
            if on_done:
                on_done(False, None, "SDO busy")
            return False
        pdu = encode_expedited_upload(index, subindex)
        self._arm({
            "op": "upload",
            "index": index,
            "subindex": subindex,
            "on_done": on_done,
            "ts": time.time(),
            "seg_buf": bytearray(),
            "toggle": 0,
            "expect_seg": False,
        })
        self._send(self._tx_id(), pdu)
        if self.on_log:
            self.on_log(
                "TX", self._tx_id(), pdu,
                "SDO upload 0x%04X:%02X" % (index, subindex))
        return True

    def download(
        self,
        index: int,
        subindex: int,
        value: int,
        size: int = 4,
        on_done: Optional[Callable] = None,
    ) -> bool:
        """Expedited SDO download (write) for integer ≤4 bytes."""
        return self.download_bytes(
            index, subindex,
            int(value).to_bytes(max(1, min(4, int(size))), "little", signed=False),
            on_done=on_done)

    def download_bytes(
        self,
        index: int,
        subindex: int,
        payload: bytes,
        on_done: Optional[Callable] = None,
    ) -> bool:
        """Download arbitrary bytes (expedited if ≤4, else segmented)."""
        if self._pending:
            if on_done:
                on_done(False, None, "SDO busy")
            return False
        data = bytes(payload or b"")
        if len(data) <= 4:
            # Pad to size for expedited encoder
            size = max(1, len(data))
            val = int.from_bytes(data.ljust(4, b"\x00")[:4], "little")
            pdu = encode_expedited_download(index, subindex, val, size)
            self._arm({
                "op": "download",
                "mode": "expedited",
                "index": index,
                "subindex": subindex,
                "on_done": on_done,
                "ts": time.time(),
            })
            self._send(self._tx_id(), pdu)
            if self.on_log:
                self.on_log(
                    "TX", self._tx_id(), pdu,
                    "SDO download 0x%04X:%02X (%d B)" % (index, subindex, size))
            return True

        pdu = encode_segmented_download_init(index, subindex, len(data))
        self._arm({
            "op": "download",
            "mode": "segmented",
            "index": index,
            "subindex": subindex,
            "on_done": on_done,
            "ts": time.time(),
            "payload": data,
            "offset": 0,
            "toggle": 0,
            "phase": "init",
        })
        self._send(self._tx_id(), pdu)
        if self.on_log:
            self.on_log(
                "TX", self._tx_id(), pdu,
                "SDO seg download init 0x%04X:%02X len=%d" % (
                    index, subindex, len(data)))
        return True

    def _send_next_download_segment(self) -> None:
        pend = self._pending
        if not pend or pend.get("mode") != "segmented":
            return
        data = pend["payload"]
        off = pend["offset"]
        chunk = data[off:off + 7]
        last = (off + len(chunk)) >= len(data)
        pdu = encode_download_segment(pend["toggle"], chunk, last)
        pend["offset"] = off + len(chunk)
        pend["phase"] = "seg"
        pend["last_sent"] = last
        self._send(self._tx_id(), pdu)
        self._timer.start(self.timeout_ms)
        if self.on_log:
            self.on_log(
                "TX", self._tx_id(), pdu,
                "SDO seg download t=%d last=%d" % (pend["toggle"], int(last)))

    def _request_upload_segment(self) -> None:
        pend = self._pending
        if not pend:
            return
        pdu = encode_upload_segment(pend["toggle"])
        self._send(self._tx_id(), pdu)
        self._timer.start(self.timeout_ms)
        if self.on_log:
            self.on_log(
                "TX", self._tx_id(), pdu,
                "SDO seg upload req t=%d" % pend["toggle"])

    def on_frame(self, can_id: int, data: bytes) -> None:
        if not self._pending:
            return
        if can_id != self._rx_id():
            return
        kind, fields = decode_sdo_response(data)
        pend = self._pending

        if kind == "abort":
            note = fields.get("message", "SDO abort")
            if self.on_log:
                self.on_log("ERR", can_id, data, note)
            self._finish(False, None, note)
            return

        # --- upload path ---
        if pend["op"] == "upload":
            if kind == "expedited_upload":
                val = fields.get("value")
                raw = fields.get("raw") or b""
                if self.on_log:
                    self.on_log(
                        "RX", can_id, data,
                        "SDO 0x%04X:%02X = 0x%X" % (
                            pend["index"], pend["subindex"], val or 0))
                self._finish(True, val if raw == b"" or len(raw) <= 4 else bytes(raw), "OK")
                return
            if kind == "upload_init_seg":
                pend["expect_seg"] = True
                pend["toggle"] = 0
                self._timer.stop()
                self._request_upload_segment()
                return
            if kind == "upload_segment" and pend.get("expect_seg"):
                if fields.get("toggle") != pend["toggle"]:
                    self._finish(False, None, "SDO toggle mismatch")
                    return
                pend["seg_buf"].extend(fields.get("data") or b"")
                if fields.get("last"):
                    blob = bytes(pend["seg_buf"])
                    if self.on_log:
                        self.on_log(
                            "RX", can_id, data,
                            "SDO seg done %d B" % len(blob))
                    # Prefer int if ≤4 bytes for API compat
                    if len(blob) <= 4:
                        val = int.from_bytes(blob, "little", signed=False)
                        self._finish(True, val, "OK")
                    else:
                        self._finish(True, blob, "OK")
                    return
                pend["toggle"] ^= 1
                self._timer.stop()
                self._request_upload_segment()
                return

        # --- download path ---
        if pend["op"] == "download":
            if pend.get("mode") == "expedited" and kind == "download_ack":
                if self.on_log:
                    self.on_log("RX", can_id, data, "SDO write ACK")
                self._finish(True, None, "OK")
                return
            if pend.get("mode") == "segmented":
                if pend.get("phase") == "init" and kind == "download_ack":
                    self._timer.stop()
                    self._send_next_download_segment()
                    return
                if kind == "download_seg_ack":
                    if pend.get("last_sent"):
                        if self.on_log:
                            self.on_log("RX", can_id, data, "SDO seg write done")
                        self._finish(True, None, "OK")
                        return
                    pend["toggle"] ^= 1
                    self._timer.stop()
                    self._send_next_download_segment()
                    return

        if self.on_log:
            self.on_log("RX", can_id, data, "SDO unexpected response")
        self._finish(False, None, "Unexpected SDO response")

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
