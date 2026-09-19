# -*- coding: utf-8 -*-
"""CiA 301 PDO mapping dword helpers."""

from __future__ import annotations

RPDO_MAP = (0x1600, 0x1601, 0x1602, 0x1603)
TPDO_MAP = (0x1A00, 0x1A01, 0x1A02, 0x1A03)


def decode_mapping(value: int):
    """Return (index, subindex, bit_length) or None if the slot is empty."""
    value = int(value) & 0xFFFFFFFF
    bit_len = value & 0xFF
    sub = (value >> 8) & 0xFF
    index = (value >> 16) & 0xFFFF
    if value == 0 or bit_len == 0 or index == 0:
        return None
    return index, sub, bit_len


def pdo_label(index: int) -> str:
    if index in RPDO_MAP:
        return "RPDO%d" % (RPDO_MAP.index(index) + 1)
    if index in TPDO_MAP:
        return "TPDO%d" % (TPDO_MAP.index(index) + 1)
    return "0x%04X" % index


def mapping_indexes():
    return RPDO_MAP + TPDO_MAP
