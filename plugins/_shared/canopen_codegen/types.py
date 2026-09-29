# -*- coding: utf-8 -*-
"""EDS DataType / AccessType → C type and stack flag helpers."""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Optional, Tuple

from _shared.edsparse import OdEntry


@dataclass(frozen=True)
class CTypeInfo:
    """Mapped C representation for one EDS data type."""

    c_type: str          # e.g. uint32_t / UNS32
    bit_length: int
    is_string: bool = False
    is_domain: bool = False
    default_zero: str = "0"


# CiA 301 data type codes (hex string or name) → (c99, bitlen, string?, domain?)
_EDS_TYPE_MAP = {
    "0x0001": ("bool", 1, False, False),
    "BOOLEAN": ("bool", 1, False, False),
    "0x0002": ("int8_t", 8, False, False),
    "INTEGER8": ("int8_t", 8, False, False),
    "0x0003": ("int16_t", 16, False, False),
    "INTEGER16": ("int16_t", 16, False, False),
    "0x0004": ("int32_t", 32, False, False),
    "INTEGER32": ("int32_t", 32, False, False),
    "0x0005": ("uint8_t", 8, False, False),
    "UNSIGNED8": ("uint8_t", 8, False, False),
    "0x0006": ("uint16_t", 16, False, False),
    "UNSIGNED16": ("uint16_t", 16, False, False),
    "0x0007": ("uint32_t", 32, False, False),
    "UNSIGNED32": ("uint32_t", 32, False, False),
    "0x0008": ("float", 32, False, False),
    "REAL32": ("float", 32, False, False),
    "0x0009": ("char", 8, True, False),
    "VISIBLE_STRING": ("char", 8, True, False),
    "0x000A": ("uint8_t", 8, True, False),
    "OCTET_STRING": ("uint8_t", 8, True, False),
    "0x000B": ("uint16_t", 16, True, False),
    "UNICODE_STRING": ("uint16_t", 16, True, False),
    "0x000F": ("uint8_t", 8, False, True),
    "DOMAIN": ("uint8_t", 8, False, True),
    "0x0010": ("int32_t", 24, False, False),
    "INTEGER24": ("int32_t", 24, False, False),
    "0x0011": ("double", 64, False, False),
    "REAL64": ("double", 64, False, False),
    "0x0015": ("int64_t", 64, False, False),
    "INTEGER64": ("int64_t", 64, False, False),
    "0x0016": ("uint32_t", 24, False, False),
    "UNSIGNED24": ("uint32_t", 24, False, False),
    "0x001B": ("uint64_t", 64, False, False),
    "UNSIGNED64": ("uint64_t", 64, False, False),
}

# C99 → CanFestival typedef
_C99_TO_CF = {
    "bool": "BOOLEAN",
    "int8_t": "INTEGER8",
    "int16_t": "INTEGER16",
    "int32_t": "INTEGER32",
    "int64_t": "INTEGER64",
    "uint8_t": "UNS8",
    "uint16_t": "UNS16",
    "uint32_t": "UNS32",
    "uint64_t": "UNS64",
    "float": "REAL32",
    "double": "REAL64",
    "char": "VISIBLE_STRING",
}


def _norm_dtype(raw: str) -> str:
    t = (raw or "").strip()
    if not t:
        return "UNSIGNED32"
    if t.lower().startswith("0x"):
        try:
            return "0x%04X" % int(t, 16)
        except ValueError:
            return t.upper()
    return t.upper()


def ctype_info(data_type: str) -> CTypeInfo:
    key = _norm_dtype(data_type)
    mapped = _EDS_TYPE_MAP.get(key) or _EDS_TYPE_MAP.get(key.upper())
    if mapped is None:
        # try bare name without 0x
        for k, v in _EDS_TYPE_MAP.items():
            if k.upper() == key.upper():
                mapped = v
                break
    if mapped is None:
        mapped = ("uint32_t", 32, False, False)
    c_type, bits, is_str, is_dom = mapped
    zero = '""' if is_str else ("false" if c_type == "bool" else "0")
    return CTypeInfo(c_type=c_type, bit_length=bits, is_string=is_str,
                     is_domain=is_dom, default_zero=zero)


def cf_type(data_type: str) -> str:
    info = ctype_info(data_type)
    return _C99_TO_CF.get(info.c_type, "UNS32")


def byte_size(data_type: str, string_len: int = 32) -> int:
    info = ctype_info(data_type)
    if info.is_string or info.is_domain:
        return max(1, string_len)
    return max(1, (info.bit_length + 7) // 8)


def parse_default(value: str, data_type: str) -> str:
    """Return a C literal for the EDS default/parameter value."""
    info = ctype_info(data_type)
    raw = (value or "").strip()
    if info.is_string:
        s = raw.strip('"').replace("\\", "\\\\").replace('"', '\\"')
        return '"%s"' % s
    if not raw:
        return info.default_zero
    if info.c_type == "bool":
        low = raw.lower()
        if low in ("1", "true", "yes", "0x1", "0x01"):
            return "true"
        return "false"
    if info.c_type in ("float", "double"):
        try:
            return repr(float(raw))
        except ValueError:
            return "0.0"
    try:
        if raw.lower().startswith("0x"):
            n = int(raw, 16)
        else:
            n = int(raw, 0)
        if info.c_type.startswith("int") and n < 0:
            return str(n)
        return "0x%X" % n if n != 0 else "0"
    except ValueError:
        return info.default_zero


def c_ident(name: str, fallback: str) -> str:
    """Safe C identifier from EDS ParameterName."""
    s = re.sub(r"[^0-9A-Za-z_]", "_", (name or "").strip())
    s = re.sub(r"_+", "_", s).strip("_")
    if not s:
        s = fallback
    if s[0].isdigit():
        s = "x" + s
    return s


def var_name(entry: OdEntry) -> str:
    base = c_ident(entry.name, "obj_%04X_%02X" % (entry.index, entry.subindex))
    return "x%04X_%02X_%s" % (entry.index, entry.subindex, base)


def access_oda_flags(access: str, pdo_mapping: str) -> str:
    """CANopenNode V4 ODA_* flag expression."""
    a = (access or "rw").lower()
    flags = []
    if a in ("ro", "const", "rwr", "rww", "rw"):
        flags.append("ODA_SDO_R")
    if a in ("wo", "rw", "rwr", "rww"):
        flags.append("ODA_SDO_W")
    if a == "const":
        flags.append("ODA_MB")
    pdo = str(pdo_mapping).strip().lower()
    if pdo in ("1", "yes", "true"):
        if a in ("ro", "const", "rwr", "rw"):
            flags.append("ODA_TPDO")
        if a in ("wo", "rww", "rw"):
            flags.append("ODA_RPDO")
    return " | ".join(flags) if flags else "ODA_SDO_R"


def access_cf(access: str) -> str:
    """CanFestival RW/RO/WO/CONST."""
    a = (access or "rw").lower()
    if a == "ro":
        return "RO"
    if a == "wo":
        return "WO"
    if a == "const":
        return "CONST"
    return "RW"


def is_var_entry(entry: OdEntry) -> bool:
    """True if this OD row holds a concrete value (not ARRAY/RECORD header)."""
    ot = (entry.object_type or "").lower()
    if ot in ("0x8", "array", "0x9", "record"):
        # header with SubNumber only
        if entry.subindex == 0 and not entry.data_type:
            return False
    if not entry.data_type and entry.subindex == 0:
        # likely ARRAY/RECORD count object without type
        if "SubNumber" in (entry.extra or {}):
            return False
    return bool(entry.data_type) or entry.subindex > 0


def group_by_index(entries) -> list:
    """[(index, [OdEntry... sorted by sub]), ...]"""
    by = {}
    for e in entries:
        by.setdefault(e.index, []).append(e)
    out = []
    for idx in sorted(by):
        out.append((idx, sorted(by[idx], key=lambda x: x.subindex)))
    return out
