"""sin SDK data bus — binary FRAME_BATCH over ZMQ SUB (scheme B)."""

from __future__ import annotations

import struct
import threading

_MAGIC = b"OBUS"
_MSG_FRAME_BATCH = 1


def decode_frame_batch(payload: bytes) -> list[dict]:
    """Decode a FRAME_BATCH payload into dicts compatible with sin.frames.Frame."""
    if len(payload) < 12 or payload[0:4] != _MAGIC:
        return []
    version, msg_type, count = struct.unpack_from("<HHI", payload, 4)
    if version != 1 or msg_type != _MSG_FRAME_BATCH:
        return []

    offset = 12
    frames = []
    for _ in range(count):
        if offset + 16 > len(payload):
            break
        can_id, flags, dlc, channel, data_len = struct.unpack_from("<IBBBB", payload, offset)
        offset += 8
        (ts_ns,) = struct.unpack_from("<Q", payload, offset)
        offset += 8
        if offset + data_len > len(payload):
            break
        data = payload[offset : offset + data_len]
        offset += data_len
        frames.append({
            "id": can_id,
            "extended": bool(flags & 0x80),
            "fd": bool(flags & 0x40),
            "dlc": dlc,
            "data": data.hex(),
            "timestamp": ts_ns / 1e9 if ts_ns else 0.0,
            "channel": channel,
            "direction": "Tx" if (flags & 0x08) else "Rx",
        })
    return frames


class DataBus:
    """Background SUB reader; delivers decoded frames via callback on caller thread queue."""

    def __init__(self, endpoint: str, on_batch):
        import zmq
        self._ctx = zmq.Context.instance()
        self._sock = self._ctx.socket(zmq.SUB)
        self._sock.setsockopt(zmq.RCVHWM, 1000)
        self._sock.setsockopt(zmq.SUBSCRIBE, b"frames")
        self._sock.connect(endpoint)
        self._on_batch = on_batch
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, name="sin-databus", daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        try:
            self._sock.close(0)
        except Exception:
            pass

    def _loop(self):
        import zmq
        while not self._stop.is_set():
            try:
                parts = self._sock.recv_multipart()
            except zmq.ZMQError:
                break
            if len(parts) < 2:
                continue
            try:
                batch = decode_frame_batch(parts[-1])
                if batch:
                    self._on_batch(batch)
            except Exception:
                continue
