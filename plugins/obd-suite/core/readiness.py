# -*- coding: utf-8 -*-
"""Mode 01 PID 01 readiness / monitor decode (SAE J1979)."""

from __future__ import annotations

_CONTINUOUS = (
    (0, "Misfire"),
    (1, "Fuel system"),
    (2, "Comprehensive component"),
)

_SPARK = (
    (0, "Catalyst"),
    (1, "Heated catalyst"),
    (2, "Evaporative system"),
    (3, "Secondary air"),
    (4, "A/C refrigerant"),
    (5, "Oxygen sensor"),
    (6, "Oxygen sensor heater"),
    (7, "EGR"),
)

_COMPRESSION = (
    (0, "NMHC catalyst"),
    (1, "NOx/SCR"),
    (2, "Boost pressure"),
    (3, "Exhaust gas sensor"),
    (4, "PM filter"),
    (5, "EGR / VVT"),
    (6, "Reserved"),
    (7, "Reserved"),
)


def decode_pid01(data: bytes) -> dict:
    raw = bytes(data[:4]).ljust(4, b"\x00")
    a, b, c, d = raw[0], raw[1], raw[2], raw[3]
    compression = bool(b & 0x08)
    monitors = []
    for bit, name in _CONTINUOUS:
        supported = bool(b & (1 << bit))
        incomplete = bool(b & (1 << (bit + 4)))
        monitors.append({
            "name": name,
            "kind": "continuous",
            "supported": supported,
            "complete": supported and not incomplete,
        })
    table = _COMPRESSION if compression else _SPARK
    for bit, name in table:
        supported = bool(c & (1 << bit))
        incomplete = bool(d & (1 << bit))
        monitors.append({
            "name": name,
            "kind": "compression" if compression else "spark",
            "supported": supported,
            "complete": supported and not incomplete,
        })
    return {
        "mil": bool(a & 0x80),
        "dtc_count": a & 0x7F,
        "ignition": "compression" if compression else "spark",
        "monitors": monitors,
    }
