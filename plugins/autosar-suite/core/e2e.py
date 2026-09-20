# -*- coding: utf-8 -*-
"""AUTOSAR E2E Profile 1 (CRC-8 SAE J1850) and a Profile 2 CRC-8-H2F check.

Profile 1 layout used here (SWS_E2E, DataID mode BOTH / LOW):
  byte 0 = CRC
  byte 1 low nibble = counter (0..14)
CRC covers DataID byte(s) then data[1:].
"""

from __future__ import annotations


def crc8(data: bytes, poly: int, init: int = 0xFF, xorout: int = 0xFF) -> int:
    crc = init & 0xFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc ^ xorout


def crc8_j1850(data: bytes) -> int:
    """SAE J1850, used by E2E Profile 1."""
    return crc8(data, 0x1D)


def crc8_h2f(data: bytes) -> int:
    """CRC-8-H2F, used by E2E Profile 2."""
    return crc8(data, 0x2F)


def _data_id_bytes(data_id: int, mode: str, counter: int) -> bytes:
    lo = data_id & 0xFF
    hi = (data_id >> 8) & 0xFF
    m = (mode or "BOTH").upper()
    if m == "LOW":
        return bytes([lo])
    if m == "ALT":
        return bytes([lo if (counter % 2 == 0) else hi])
    return bytes([lo, hi])


def p01_protect(data: bytes, data_id: int, counter: int, mode: str = "BOTH") -> bytes:
    buf = bytearray(data)
    if len(buf) < 2:
        buf.extend(b"\x00" * (2 - len(buf)))
    buf[1] = (buf[1] & 0xF0) | (int(counter) & 0x0F)
    crc = crc8_j1850(_data_id_bytes(data_id, mode, counter) + bytes(buf[1:]))
    buf[0] = crc
    return bytes(buf)


def p01_check(data: bytes, data_id: int, mode: str = "BOTH") -> dict:
    if len(data) < 2:
        return {"ok": False, "reason": "short", "counter": 0, "crc": 0}
    counter = data[1] & 0x0F
    expect = crc8_j1850(_data_id_bytes(data_id, mode, counter) + bytes(data[1:]))
    return {
        "ok": expect == data[0],
        "reason": "ok" if expect == data[0] else "crc",
        "counter": counter,
        "crc": data[0],
        "expect": expect,
    }


def p02_protect(data: bytes, data_id_list: bytes, counter: int) -> bytes:
    """Profile 2: CRC over DataIDList[counter] + data[1:], counter in low nibble."""
    buf = bytearray(data)
    if len(buf) < 2:
        buf.extend(b"\x00" * (2 - len(buf)))
    buf[1] = (buf[1] & 0xF0) | (int(counter) & 0x0F)
    ids = bytes(data_id_list or b"\x00" * 16)
    if len(ids) < 16:
        ids = ids + bytes(16 - len(ids))
    did = ids[int(counter) & 0x0F]
    buf[0] = crc8_h2f(bytes([did]) + bytes(buf[1:]))
    return bytes(buf)


def p02_check(data: bytes, data_id_list: bytes) -> dict:
    if len(data) < 2:
        return {"ok": False, "reason": "short", "counter": 0, "crc": 0}
    counter = data[1] & 0x0F
    ids = bytes(data_id_list or b"\x00" * 16)
    if len(ids) < 16:
        ids = ids + bytes(16 - len(ids))
    expect = crc8_h2f(bytes([ids[counter]]) + bytes(data[1:]))
    return {
        "ok": expect == data[0],
        "reason": "ok" if expect == data[0] else "crc",
        "counter": counter,
        "crc": data[0],
        "expect": expect,
    }
