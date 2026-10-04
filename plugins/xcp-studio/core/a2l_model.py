# -*- coding: utf-8 -*-
"""Load applied A2L into XCP Studio session model."""

from __future__ import annotations

from typing import List, Optional

from _shared import a2lparse
from _shared.a2lparse import A2lDocument, A2lSymbol


def load_a2l(path: str) -> A2lDocument:
    return a2lparse.parse_a2l_file(path)


def datatype_size(datatype: str) -> int:
    t = (datatype or "").upper()
    table = {
        "UBYTE": 1, "SBYTE": 1, "UWORD": 2, "SWORD": 2,
        "ULONG": 4, "SLONG": 4, "A_UINT64": 8, "A_INT64": 8,
        "FLOAT32_IEEE": 4, "FLOAT64_IEEE": 8,
    }
    return table.get(t, 4)


def symbol_bytes(sym: A2lSymbol) -> int:
    if sym.kind == "CHARACTERISTIC" and (sym.char_type or "").upper() in (
            "CURVE", "MAP", "CUBOID"):
        return max(4, datatype_size(sym.datatype))
    return datatype_size(sym.datatype)


def decode_raw(raw: bytes, datatype: str) -> float:
    import struct
    t = (datatype or "ULONG").upper()
    b = bytes(raw or b"")
    try:
        if t in ("UBYTE",):
            return float(b[0] if b else 0)
        if t in ("SBYTE",):
            return float(struct.unpack("<b", b[:1].ljust(1, b"\x00"))[0])
        if t in ("UWORD",):
            return float(struct.unpack("<H", b[:2].ljust(2, b"\x00"))[0])
        if t in ("SWORD",):
            return float(struct.unpack("<h", b[:2].ljust(2, b"\x00"))[0])
        if t in ("FLOAT32_IEEE",):
            return float(struct.unpack("<f", b[:4].ljust(4, b"\x00"))[0])
        if t in ("SLONG",):
            return float(struct.unpack("<i", b[:4].ljust(4, b"\x00"))[0])
        return float(struct.unpack("<I", b[:4].ljust(4, b"\x00"))[0])
    except Exception:
        return 0.0


def encode_value(value: float, datatype: str) -> bytes:
    import struct
    t = (datatype or "ULONG").upper()
    if t == "UBYTE":
        return bytes([int(value) & 0xFF])
    if t == "SBYTE":
        return struct.pack("<b", int(value))
    if t == "UWORD":
        return struct.pack("<H", int(value) & 0xFFFF)
    if t == "SWORD":
        return struct.pack("<h", int(value))
    if t == "FLOAT32_IEEE":
        return struct.pack("<f", float(value))
    if t == "SLONG":
        return struct.pack("<i", int(value))
    return struct.pack("<I", int(value) & 0xFFFFFFFF)
