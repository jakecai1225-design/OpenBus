# -*- coding: utf-8 -*-
"""CiA 402 drive profile — searchable OD stub library."""

from __future__ import annotations

from typing import List

from .eds_parse import OdEntry


def _e(index, sub, name, otype="0x7", dtype="0x0007", access="rw", default=""):
    return OdEntry(
        index=index,
        subindex=sub,
        name=name,
        object_type=otype,
        data_type=dtype,
        access_type=access,
        default_value=default,
    )


CIA402_OBJECTS: List[OdEntry] = [
    _e(0x603F, 0, "Error code", "0x7", "0x0006", "ro", "0"),
    _e(0x6040, 0, "Controlword", "0x7", "0x0006", "rw", "0"),
    _e(0x6041, 0, "Statusword", "0x7", "0x0006", "ro", "0"),
    _e(0x605A, 0, "Quick stop option code", "0x7", "0x0002", "rw"),
    _e(0x605B, 0, "Shutdown option code", "0x7", "0x0002", "rw"),
    _e(0x605C, 0, "Disable operation option code", "0x7", "0x0002", "rw"),
    _e(0x605D, 0, "Halt option code", "0x7", "0x0002", "rw"),
    _e(0x605E, 0, "Fault reaction option code", "0x7", "0x0002", "rw"),
    _e(0x6060, 0, "Modes of operation", "0x7", "0x0002", "rw", "0"),
    _e(0x6061, 0, "Modes of operation display", "0x7", "0x0002", "ro"),
    _e(0x6062, 0, "Position demand value", "0x7", "0x0004", "ro"),
    _e(0x6063, 0, "Position actual value (internal)", "0x7", "0x0004", "ro"),
    _e(0x6064, 0, "Position actual value", "0x7", "0x0004", "ro"),
    _e(0x6065, 0, "Following error window", "0x7", "0x0007", "rw"),
    _e(0x6066, 0, "Following error time out", "0x7", "0x0006", "rw"),
    _e(0x6067, 0, "Position window", "0x7", "0x0007", "rw"),
    _e(0x6068, 0, "Position window time", "0x7", "0x0006", "rw"),
    _e(0x606B, 0, "Velocity demand value", "0x7", "0x0004", "ro"),
    _e(0x606C, 0, "Velocity actual value", "0x7", "0x0004", "ro"),
    _e(0x606D, 0, "Velocity window", "0x7", "0x0006", "rw"),
    _e(0x606E, 0, "Velocity window time", "0x7", "0x0006", "rw"),
    _e(0x6071, 0, "Target torque", "0x7", "0x0003", "rw"),
    _e(0x6077, 0, "Torque actual value", "0x7", "0x0003", "ro"),
    _e(0x607A, 0, "Target position", "0x7", "0x0004", "rw", "0"),
    _e(0x607C, 0, "Home offset", "0x7", "0x0004", "rw"),
    _e(0x607D, 0, "Software position limit", "0x8", "0x0004", "rw"),
    _e(0x6081, 0, "Profile velocity", "0x7", "0x0007", "rw"),
    _e(0x6083, 0, "Profile acceleration", "0x7", "0x0007", "rw"),
    _e(0x6084, 0, "Profile deceleration", "0x7", "0x0007", "rw"),
    _e(0x6085, 0, "Quick stop deceleration", "0x7", "0x0007", "rw"),
    _e(0x6086, 0, "Motion profile type", "0x7", "0x0002", "rw"),
    _e(0x6098, 0, "Homing method", "0x7", "0x0002", "rw"),
    _e(0x60FF, 0, "Target velocity", "0x7", "0x0004", "rw", "0"),
    _e(0x6502, 0, "Supported drive modes", "0x7", "0x0007", "ro"),
]


def search_cia402(query: str) -> List[OdEntry]:
    q = (query or "").strip().lower()
    if not q:
        return list(CIA402_OBJECTS)
    out = []
    for e in CIA402_OBJECTS:
        hay = "0x%04x %s %s" % (e.index, e.name.lower(), e.display_index().lower())
        if q in hay or q in ("%04x" % e.index) or q in ("0x%04x" % e.index):
            out.append(e)
    return out
