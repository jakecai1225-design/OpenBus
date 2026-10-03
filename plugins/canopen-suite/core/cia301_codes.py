# -*- coding: utf-8 -*-
"""CiA 301 / 402 symbolic tables for Trace-class CANopen decode.

Shared by EdsBusDecoder and the SDO client — same text Vector/Ixxat show.
"""

from __future__ import annotations

from typing import Dict, List, Tuple

# CiA 301 Table 21 — SDO abort codes (complete common set)
SDO_ABORT_CODES: Dict[int, str] = {
    0x05030000: "Toggle bit not alternated",
    0x05040000: "SDO protocol timed out",
    0x05040001: "Client/server command specifier invalid / unknown",
    0x05040002: "Invalid block size (block mode)",
    0x05040003: "Invalid sequence number (block mode)",
    0x05040004: "CRC error (block mode)",
    0x05040005: "Out of memory",
    0x06010000: "Unsupported access to an object",
    0x06010001: "Attempt to read a write only object",
    0x06010002: "Attempt to write a read only object",
    0x06020000: "Object does not exist in the object dictionary",
    0x06040041: "Object cannot be mapped to the PDO",
    0x06040042: "Mapped objects exceed PDO length",
    0x06040043: "General parameter incompatibility reason",
    0x06040047: "General internal incompatibility in the device",
    0x06060000: "Access failed due to a hardware error",
    0x06070010: "Data type does not match, length of service parameter does not match",
    0x06070012: "Data type does not match, length of service parameter too high",
    0x06070013: "Data type does not match, length of service parameter too low",
    0x06090011: "Sub-index does not exist",
    0x06090030: "Value range of parameter exceeded (only for write access)",
    0x06090031: "Value of parameter written too high",
    0x06090032: "Value of parameter written too low",
    0x06090036: "Maximum value is less than minimum value",
    0x060A0023: "Resource not available: SDO connection",
    0x08000000: "General error",
    0x08000020: "Data cannot be transferred or stored to the application",
    0x08000021: "Data cannot be transferred or stored to the application because of local control",
    0x08000022: "Data cannot be transferred or stored to the application because of the present device state",
    0x08000023: "Object dictionary dynamic generation fails or no object dictionary is present",
    0x08000024: "No data available",
}

# CiA 301 emergency error code ranges (high byte) + known fixed codes
EMCY_ERROR_CODES: Dict[int, str] = {
    0x0000: "Error reset / no error",
    0x1000: "Generic error",
    0x2000: "Current",
    0x2100: "Current, device input side",
    0x2200: "Current inside the device",
    0x2300: "Current, device output side",
    0x3000: "Voltage",
    0x3100: "Mains voltage",
    0x3200: "Voltage inside the device",
    0x3300: "Output voltage",
    0x4000: "Temperature",
    0x4100: "Ambient temperature",
    0x4200: "Device temperature",
    0x5000: "Device hardware",
    0x6000: "Device software",
    0x6100: "Internal software",
    0x6200: "User software",
    0x6300: "Data set",
    0x7000: "Additional modules",
    0x8000: "Monitoring",
    0x8100: "Communication",
    0x8110: "CAN overrun (objects lost)",
    0x8120: "CAN in error passive mode",
    0x8130: "Life guard error / heartbeat error",
    0x8140: "Recovered from bus off",
    0x8150: "CAN-ID collision",
    0x8200: "Protocol error",
    0x8210: "PDO not processed due to length error",
    0x8220: "PDO length exceeded",
    0x9000: "External error",
    0xF000: "Additional functions",
    0xFF00: "Device specific",
}

ERROR_REGISTER_BITS: Tuple[str, ...] = (
    "generic",
    "current",
    "voltage",
    "temperature",
    "communication",
    "device profile",
    "manufacturer",
    "reserved",
)

# CiA 402 statusword (0x6041) bit meanings — abbreviated industry labels
STATUSWORD_BITS: List[Tuple[int, str]] = [
    (0, "Ready to switch on"),
    (1, "Switched on"),
    (2, "Operation enabled"),
    (3, "Fault"),
    (4, "Voltage enabled"),
    (5, "Quick stop"),
    (6, "Switch on disabled"),
    (7, "Warning"),
    (8, "Manufacturer"),
    (9, "Remote"),
    (10, "Target reached"),
    (11, "Internal limit"),
    (12, "Op mode specific"),
    (13, "Op mode specific"),
    (14, "Manufacturer"),
    (15, "Manufacturer"),
]

CONTROLWORD_BITS: List[Tuple[int, str]] = [
    (0, "Switch on"),
    (1, "Enable voltage"),
    (2, "Quick stop"),
    (3, "Enable operation"),
    (4, "Op mode specific"),
    (5, "Op mode specific"),
    (6, "Op mode specific"),
    (7, "Fault reset"),
    (8, "Halt"),
    (9, "Op mode specific"),
    (10, "Reserved"),
    (11, "Manufacturer"),
    (12, "Manufacturer"),
    (13, "Manufacturer"),
    (14, "Manufacturer"),
    (15, "Manufacturer"),
]

# CiA 402 state from statusword bits 0..6 (x=don't care patterns)
_CIA402_STATES = (
    (0b01001111, 0b00000000, "Not ready to switch on"),
    (0b01001111, 0b01000000, "Switch on disabled"),
    (0b01101111, 0b00100001, "Ready to switch on"),
    (0b01101111, 0b00100011, "Switched on"),
    (0b01101111, 0b00100111, "Operation enabled"),
    (0b01101111, 0b00000111, "Quick stop active"),
    (0b01001111, 0b00001111, "Fault reaction active"),
    (0b01001111, 0b00001000, "Fault"),
)

# CiA 305 LSS command (cs) — master→slave common set
LSS_CS: Dict[int, str] = {
    0x04: "Switch mode global",
    0x40: "Configure node-ID",
    0x41: "Configure bit timing",
    0x42: "Activate bit timing",
    0x43: "Store configuration",
    0x44: "Inquire identity vendor-ID",
    0x45: "Inquire identity product-code",
    0x46: "Inquire identity revision",
    0x47: "Inquire identity serial",
    0x4C: "Identify non-configured remote slave",
    0x4E: "Identify remote slave (fastscan)",
    0x5E: "Inquire node-ID",
    0x5F: "Identify remote slave",
}


def sdo_abort_text(code: int) -> str:
    return SDO_ABORT_CODES.get(int(code) & 0xFFFFFFFF,
                               "Abort 0x%08X" % (int(code) & 0xFFFFFFFF))


def emcy_error_text(code: int) -> str:
    c = int(code) & 0xFFFF
    if c in EMCY_ERROR_CODES:
        return EMCY_ERROR_CODES[c]
    # Match by high byte class (e.g. 0x21xx → Current, device input side)
    hi = c & 0xFF00
    if hi in EMCY_ERROR_CODES:
        return "%s (0x%04X)" % (EMCY_ERROR_CODES[hi], c)
    return "0x%04X" % c


def error_register_text(reg: int) -> str:
    bits = []
    for i, name in enumerate(ERROR_REGISTER_BITS):
        if reg & (1 << i) and name != "reserved":
            bits.append(name)
    return ",".join(bits) if bits else "none"


def statusword_state(sw: int) -> str:
    v = int(sw) & 0xFFFF
    for mask, expect, name in _CIA402_STATES:
        if (v & mask) == expect:
            return name
    return "Unknown"


def bitfield_summary(value: int, bits: List[Tuple[int, str]],
                     max_set: int = 6) -> str:
    set_names = [name for i, name in bits if value & (1 << i)]
    if not set_names:
        return "none"
    if len(set_names) > max_set:
        return ", ".join(set_names[:max_set]) + "…"
    return ", ".join(set_names)


def lss_cs_text(cs: int) -> str:
    return LSS_CS.get(int(cs) & 0xFF, "cs=0x%02X" % (int(cs) & 0xFF))
