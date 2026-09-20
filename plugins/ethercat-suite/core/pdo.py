# -*- coding: utf-8 -*-
"""EtherCAT process image: entries packed little-endian from bit 0."""

from __future__ import annotations

from core.esi import PdoEntry


def image_bit_length(entries: list) -> int:
    return sum(int(e.bit_len) for e in entries)


def image_size(entries: list) -> int:
    bits = image_bit_length(entries)
    return (bits + 7) // 8


def unpack_entries(data: bytes, entries: list) -> list:
    raw = int.from_bytes(bytes(data), "little") if data else 0
    out = []
    bit = 0
    for e in entries:
        length = int(e.bit_len)
        mask = (1 << length) - 1 if length else 0
        value = (raw >> bit) & mask
        out.append((e, value))
        bit += length
    return out


def pack_entries(entries: list, values: list, size: int = 0) -> bytes:
    raw = 0
    bit = 0
    for e, value in zip(entries, values):
        length = int(e.bit_len)
        mask = (1 << length) - 1 if length else 0
        raw |= (int(value) & mask) << bit
        bit += length
    nbytes = size or image_size(entries)
    return int(raw).to_bytes(max(0, nbytes), "little")


def flat_entries(pdos: list) -> list:
    out = []
    for pdo in pdos:
        out.extend(list(pdo.entries))
    return out
