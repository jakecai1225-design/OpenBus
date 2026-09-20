# -*- coding: utf-8 -*-
"""I-PDU bit layout (AUTOSAR packing byte order).

intel / MOST-SIGNIFICANT-BYTE-LAST: start_bit is the LSB of the signal.
motorola / MOST-SIGNIFICANT-BYTE-FIRST: start_bit is the MSB of the signal.
"""

from __future__ import annotations

from dataclasses import dataclass, field


ENDIAN_INTEL = "intel"
ENDIAN_MOTOROLA = "motorola"


@dataclass
class Signal:
    name: str
    start_bit: int
    length: int
    endian: str = ENDIAN_INTEL
    factor: float = 1.0
    offset: float = 0.0
    unit: str = ""
    init_raw: int = 0

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "start_bit": self.start_bit,
            "length": self.length,
            "endian": self.endian,
            "factor": self.factor,
            "offset": self.offset,
            "unit": self.unit,
            "init_raw": self.init_raw,
        }

    @staticmethod
    def from_dict(d: dict) -> "Signal":
        return Signal(
            name=str(d.get("name") or "Signal"),
            start_bit=int(d.get("start_bit") or 0),
            length=max(1, int(d.get("length") or 1)),
            endian=str(d.get("endian") or ENDIAN_INTEL),
            factor=float(d.get("factor") if d.get("factor") is not None else 1.0),
            offset=float(d.get("offset") or 0.0),
            unit=str(d.get("unit") or ""),
            init_raw=int(d.get("init_raw") or 0),
        )


@dataclass
class Ipdu:
    name: str
    can_id: int
    dlc: int = 8
    signals: list = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "can_id": self.can_id,
            "dlc": self.dlc,
            "signals": [s.to_dict() for s in self.signals],
        }

    @staticmethod
    def from_dict(d: dict) -> "Ipdu":
        sigs = [Signal.from_dict(s) for s in (d.get("signals") or [])]
        return Ipdu(
            name=str(d.get("name") or "PDU"),
            can_id=int(d.get("can_id") or 0),
            dlc=max(0, int(d.get("dlc") or 8)),
            signals=sigs,
        )


def normalize_endian(text: str) -> str:
    t = (text or "").strip().upper().replace("_", "-")
    if t in ("INTEL", "LITTLE", "LSB", "LITTLE-ENDIAN"):
        return ENDIAN_INTEL
    if "LAST" in t or "LITTLE" in t or t == "INTEL":
        return ENDIAN_INTEL
    if t in ("",):
        return ENDIAN_INTEL
    return ENDIAN_MOTOROLA


def signal_bits_lsb_first(start_bit: int, length: int, endian: str) -> list:
    """Frame bit index for each value bit, LSB of the raw value first."""
    if length <= 0:
        return []
    if normalize_endian(endian) == ENDIAN_INTEL:
        return [int(start_bit) + i for i in range(int(length))]
    bit = int(start_bit)
    msb_first = []
    for _ in range(int(length)):
        msb_first.append(bit)
        if bit % 8 == 0:
            bit += 15
        else:
            bit -= 1
    return list(reversed(msb_first))


def _ensure(buf: bytearray, bit_index: int) -> None:
    need = bit_index // 8 + 1
    if len(buf) < need:
        buf.extend(b"\x00" * (need - len(buf)))


def unpack_raw(data: bytes, start_bit: int, length: int, endian: str) -> int:
    bits = signal_bits_lsb_first(start_bit, length, endian)
    raw = 0
    src = bytes(data)
    for i, fb in enumerate(bits):
        bi = fb // 8
        if bi >= len(src):
            continue
        if src[bi] & (1 << (fb % 8)):
            raw |= 1 << i
    return raw


def pack_raw(data: bytes, start_bit: int, length: int, endian: str, raw: int) -> bytes:
    buf = bytearray(data)
    bits = signal_bits_lsb_first(start_bit, length, endian)
    value = int(raw)
    for i, fb in enumerate(bits):
        _ensure(buf, fb)
        mask = 1 << (fb % 8)
        if value & (1 << i):
            buf[fb // 8] |= mask
        else:
            buf[fb // 8] &= ~mask & 0xFF
    return bytes(buf)


def raw_to_phys(raw: int, factor: float, offset: float) -> float:
    return float(raw) * float(factor) + float(offset)


def phys_to_raw(phys: float, factor: float, offset: float, length: int) -> int:
    fac = float(factor) if factor else 1.0
    raw = int(round((float(phys) - float(offset)) / fac))
    if length <= 0:
        return 0
    mask = (1 << int(length)) - 1
    if raw < 0:
        raw = 0
    if raw > mask:
        raw = mask
    return raw


def unpack_signal(data: bytes, sig: Signal) -> tuple:
    raw = unpack_raw(data, sig.start_bit, sig.length, sig.endian)
    return raw, raw_to_phys(raw, sig.factor, sig.offset)


def pack_signal(data: bytes, sig: Signal, phys: float) -> bytes:
    raw = phys_to_raw(phys, sig.factor, sig.offset, sig.length)
    return pack_raw(data, sig.start_bit, sig.length, sig.endian, raw)


def blank_pdu(ipdu: Ipdu) -> bytes:
    return bytes(max(0, int(ipdu.dlc)))
