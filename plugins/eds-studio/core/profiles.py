# -*- coding: utf-8 -*-
"""CiA profile object stubs for Library insert.

Covers communication profile plus common device profiles beginners meet
first: 301, 401 I/O, 402 drive, 404 measuring, 406 encoder, 418 battery.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from _shared.edsparse import OdEntry


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


def _rec(index, name, sub_number: int) -> OdEntry:
    return OdEntry(
        index=index, subindex=0, name=name, object_type="0x9",
        extra={"SubNumber": str(sub_number)})


def _pdo_comm(index: int, cob: str, name: str) -> List[OdEntry]:
    return [
        OdEntry(index, 0, name, "0x9", extra={"SubNumber": "2"}),
        _e(index, 1, "COB-ID used by %s" % name, "0x7", "0x0007", "rw", cob),
        _e(index, 2, "Transmission type", "0x7", "0x0005", "rw", "255"),
    ]


def _pdo_map_empty(index: int, name: str) -> List[OdEntry]:
    return [
        OdEntry(
            index=index, subindex=0, name=name,
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="0", extra={"SubNumber": "1"}),
    ]


CIA301_OBJECTS: List[OdEntry] = [
    _e(0x1000, 0, "Device type", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1001, 0, "Error register", "0x7", "0x0005", "ro", "0x00"),
    _e(0x1002, 0, "Manufacturer status register", "0x7", "0x0007", "ro", "0"),
    _e(0x1003, 0, "Pre-defined error field", "0x8", "", "ro"),
    _e(0x1005, 0, "COB-ID SYNC", "0x7", "0x0007", "rw", "0x00000080"),
    _e(0x1006, 0, "Communication cycle period", "0x7", "0x0007", "rw", "0"),
    _e(0x1007, 0, "Synchronous window length", "0x7", "0x0007", "rw", "0"),
    _e(0x1008, 0, "Manufacturer device name", "0x7", "0x0009", "const"),
    _e(0x1009, 0, "Manufacturer hardware version", "0x7", "0x0009", "const"),
    _e(0x100A, 0, "Manufacturer software version", "0x7", "0x0009", "const"),
    _e(0x100C, 0, "Guard time", "0x7", "0x0006", "rw", "0"),
    _e(0x100D, 0, "Life time factor", "0x7", "0x0005", "rw", "0"),
    _e(0x1010, 0, "Store parameters", "0x8", "", "rw"),
    _e(0x1011, 0, "Restore default parameters", "0x8", "", "rw"),
    _e(0x1014, 0, "COB-ID EMCY", "0x7", "0x0007", "rw"),
    _e(0x1015, 0, "Inhibit time EMCY", "0x7", "0x0006", "rw", "0"),
    _e(0x1016, 0, "Consumer heartbeat time", "0x8", "", "rw"),
    _e(0x1017, 0, "Producer heartbeat time", "0x7", "0x0006", "rw", "1000"),
    OdEntry(0x1018, 0, "Identity object", "0x8", extra={"SubNumber": "4"}),
    _e(0x1018, 1, "Vendor-ID", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1018, 2, "Product code", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1018, 3, "Revision number", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1018, 4, "Serial number", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1029, 0, "Error behaviour object", "0x8", "", "rw"),
    _rec(0x1200, "SDO server parameter", 2),
    _e(0x1200, 1, "COB-ID Client to Server (rx)", "0x7", "0x0007", "ro", "0x600"),
    _e(0x1200, 2, "COB-ID Server to Client (tx)", "0x7", "0x0007", "ro", "0x580"),
]
CIA301_OBJECTS.extend(_pdo_comm(0x1400, "0x200", "RPDO 1"))
CIA301_OBJECTS.extend(_pdo_comm(0x1401, "0x300", "RPDO 2"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1600, "RPDO 1 mapping parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1601, "RPDO 2 mapping parameter"))
CIA301_OBJECTS.extend(_pdo_comm(0x1800, "0x180", "TPDO 1"))
CIA301_OBJECTS.extend([
    _e(0x1800, 3, "Inhibit time", "0x7", "0x0006", "rw", "0"),
    _e(0x1800, 5, "Event timer", "0x7", "0x0006", "rw", "0"),
])
for _e1800 in CIA301_OBJECTS:
    if _e1800.index == 0x1800 and _e1800.subindex == 0:
        _e1800.extra["SubNumber"] = "5"
        break
CIA301_OBJECTS.extend(_pdo_comm(0x1801, "0x280", "TPDO 2"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1A00, "TPDO 1 mapping parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1A01, "TPDO 2 mapping parameter"))


CIA401_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Read input 8-bit", "0x8", "0x0005", "ro"),
    _e(0x6000, 1, "Digital inputs 1-8", "0x7", "0x0005", "ro", "0"),
    _e(0x6100, 0, "Read input 16-bit", "0x8", "0x0006", "ro"),
    _e(0x6100, 1, "Digital inputs 1-16", "0x7", "0x0006", "ro", "0"),
    _e(0x6200, 0, "Write output 8-bit", "0x8", "0x0005", "rw"),
    _e(0x6200, 1, "Digital outputs 1-8", "0x7", "0x0005", "rw", "0"),
    _e(0x6300, 0, "Write output 16-bit", "0x8", "0x0006", "rw"),
    _e(0x6300, 1, "Digital outputs 1-16", "0x7", "0x0006", "rw", "0"),
    _e(0x6401, 0, "Read analogue input 16-bit", "0x8", "0x0003", "ro"),
    _e(0x6401, 1, "Analogue input 1", "0x7", "0x0003", "ro", "0"),
    _e(0x6401, 2, "Analogue input 2", "0x7", "0x0003", "ro", "0"),
    _e(0x6411, 0, "Write analogue output 16-bit", "0x8", "0x0003", "rw"),
    _e(0x6411, 1, "Analogue output 1", "0x7", "0x0003", "rw", "0"),
    _e(0x6411, 2, "Analogue output 2", "0x7", "0x0003", "rw", "0"),
]


CIA402_OBJECTS: List[OdEntry] = [
    _e(0x6007, 0, "Abort connection option code", "0x7", "0x0003", "rw", "3"),
    _e(0x603F, 0, "Error code", "0x7", "0x0006", "ro", "0"),
    _e(0x6040, 0, "Controlword", "0x7", "0x0006", "rw", "0"),
    _e(0x6041, 0, "Statusword", "0x7", "0x0006", "ro", "0"),
    _e(0x605A, 0, "Quick stop option code", "0x7", "0x0003", "rw", "2"),
    _e(0x605B, 0, "Shutdown option code", "0x7", "0x0003", "rw", "0"),
    _e(0x605C, 0, "Disable operation option code", "0x7", "0x0003", "rw", "1"),
    _e(0x605D, 0, "Halt option code", "0x7", "0x0003", "rw", "1"),
    _e(0x605E, 0, "Fault reaction option code", "0x7", "0x0003", "rw", "2"),
    _e(0x6060, 0, "Modes of operation", "0x7", "0x0002", "rw", "8"),
    _e(0x6061, 0, "Modes of operation display", "0x7", "0x0002", "ro", "8"),
    _e(0x6062, 0, "Position demand value", "0x7", "0x0004", "ro", "0"),
    _e(0x6063, 0, "Position actual value (internal)", "0x7", "0x0004", "ro", "0"),
    _e(0x6064, 0, "Position actual value", "0x7", "0x0004", "ro", "0"),
    _e(0x6065, 0, "Following error window", "0x7", "0x0007", "rw", "0"),
    _e(0x6067, 0, "Position window", "0x7", "0x0007", "rw", "0"),
    _e(0x606C, 0, "Velocity actual value", "0x7", "0x0004", "ro", "0"),
    _e(0x6071, 0, "Target torque", "0x7", "0x0003", "rw", "0"),
    _e(0x6077, 0, "Torque actual value", "0x7", "0x0003", "ro", "0"),
    _e(0x607A, 0, "Target position", "0x7", "0x0004", "rw", "0"),
    _e(0x607C, 0, "Home offset", "0x7", "0x0004", "rw", "0"),
    _e(0x6081, 0, "Profile velocity", "0x7", "0x0007", "rw", "0"),
    _e(0x6083, 0, "Profile acceleration", "0x7", "0x0007", "rw", "0"),
    _e(0x6084, 0, "Profile deceleration", "0x7", "0x0007", "rw", "0"),
    _e(0x6085, 0, "Quick stop deceleration", "0x7", "0x0007", "rw", "0"),
    _e(0x6086, 0, "Motion profile type", "0x7", "0x0002", "rw", "0"),
    _e(0x6098, 0, "Homing method", "0x7", "0x0002", "rw", "35"),
    _e(0x60FF, 0, "Target velocity", "0x7", "0x0004", "rw", "0"),
    _e(0x6502, 0, "Supported drive modes", "0x7", "0x0007", "ro", "0x000003AD"),
]


CIA404_OBJECTS: List[OdEntry] = [
    _e(0x6100, 0, "Input process value (16-bit)", "0x8", "", "ro"),
    _e(0x6100, 1, "Process value channel 1", "0x7", "0x0003", "ro", "0"),
    _e(0x6100, 2, "Process value channel 2", "0x7", "0x0003", "ro", "0"),
    _e(0x6110, 0, "Input scaling 1st parameter", "0x8", "", "rw"),
    _e(0x6110, 1, "Scaling numerator ch1", "0x7", "0x0004", "rw", "1"),
    _e(0x6112, 0, "Input filter constant", "0x8", "", "rw"),
    _e(0x6112, 1, "Filter constant ch1", "0x7", "0x0007", "rw", "0"),
    _e(0x6120, 0, "Physical unit", "0x8", "", "rw"),
    _e(0x6120, 1, "Unit channel 1", "0x7", "0x0007", "rw", "0"),
    _e(0x6130, 0, "Input operating mode", "0x8", "", "rw"),
    _e(0x6130, 1, "Operating mode ch1", "0x7", "0x0005", "rw", "0"),
]


CIA406_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Operating parameters", "0x7", "0x0007", "rw", "0"),
    _e(0x6001, 0, "Measuring units per revolution", "0x7", "0x0007", "rw", "1024"),
    _e(0x6002, 0, "Total measuring range (revolution)", "0x7", "0x0007", "rw", "1"),
    _e(0x6003, 0, "Preset value", "0x7", "0x0007", "rw", "0"),
    _e(0x6004, 0, "Position value", "0x7", "0x0007", "ro", "0"),
    _e(0x6500, 0, "Operating status", "0x7", "0x0007", "ro", "0"),
    _e(0x6501, 0, "Single-turn resolution", "0x7", "0x0007", "ro", "1024"),
    _e(0x6502, 0, "Number of distinguishable revolutions", "0x7", "0x0007", "ro", "1"),
]


CIA418_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Battery status", "0x7", "0x0005", "ro", "0"),
    _e(0x6001, 0, "Battery temperature", "0x7", "0x0003", "ro", "0"),
    _e(0x6002, 0, "Battery voltage", "0x7", "0x0006", "ro", "0"),
    _e(0x6003, 0, "Battery current", "0x7", "0x0003", "ro", "0"),
    _e(0x6010, 0, "State of charge", "0x7", "0x0005", "ro", "0"),
    _e(0x6011, 0, "State of health", "0x7", "0x0005", "ro", "100"),
    _e(0x6020, 0, "Nominal voltage", "0x7", "0x0006", "const", "4800"),
    _e(0x6021, 0, "Nominal capacity", "0x7", "0x0006", "const", "100"),
]


COMM_TEMPLATES: Dict[str, List[OdEntry]] = {
    "RPDO1": _pdo_comm(0x1400, "0x200", "RPDO1") + _pdo_map_empty(
        0x1600, "RPDO1 mapping parameter"),
    "RPDO2": _pdo_comm(0x1401, "0x300", "RPDO2") + _pdo_map_empty(
        0x1601, "RPDO2 mapping parameter"),
    "TPDO1": _pdo_comm(0x1800, "0x180", "TPDO1") + _pdo_map_empty(
        0x1A00, "TPDO1 mapping parameter"),
    "TPDO2": _pdo_comm(0x1801, "0x280", "TPDO2") + _pdo_map_empty(
        0x1A01, "TPDO2 mapping parameter"),
    "RPDO1+TPDO1": (
        _pdo_comm(0x1400, "0x200", "RPDO1")
        + _pdo_map_empty(0x1600, "RPDO1 mapping parameter")
        + _pdo_comm(0x1800, "0x180", "TPDO1")
        + _pdo_map_empty(0x1A00, "TPDO1 mapping parameter")
    ),
    "Heartbeat producer": [
        _e(0x1017, 0, "Producer heartbeat time", "0x7", "0x0006", "rw", "1000"),
    ],
    "SDO server": [
        _rec(0x1200, "SDO server parameter", 2),
        _e(0x1200, 1, "COB-ID Client to Server (rx)", "0x7", "0x0007", "ro", "0x600"),
        _e(0x1200, 2, "COB-ID Server to Client (tx)", "0x7", "0x0007", "ro", "0x580"),
    ],
}


PROFILE_CATALOG: List[Tuple[str, str, str, str]] = [
    ("301", "CiA 301 — Communication", "profile",
     "Core CANopen objects every node needs (device type, identity, "
     "heartbeat, SDO, PDO shells). Start here."),
    ("401", "CiA 401 — Digital / analogue I/O", "profile",
     "Generic I/O module: digital in/out and analogue channels."),
    ("402", "CiA 402 — Drives and motion", "profile",
     "Servo / frequency inverter: controlword, statusword, position, velocity."),
    ("404", "CiA 404 — Measuring devices", "profile",
     "Sensors / transducers: process value, scaling, filter, units."),
    ("406", "CiA 406 — Encoders", "profile",
     "Incremental / absolute encoder position and resolution objects."),
    ("418", "CiA 418 — Battery modules", "profile",
     "Battery status, SoC/SoH, voltage and current for AGV / energy nodes."),
    ("RPDO1", "Pack — RPDO1", "pack",
     "Receive PDO 1 communication + empty mapping (COB 0x200+NodeID)."),
    ("RPDO2", "Pack — RPDO2", "pack",
     "Receive PDO 2 communication + empty mapping."),
    ("TPDO1", "Pack — TPDO1", "pack",
     "Transmit PDO 1 communication + empty mapping (COB 0x180+NodeID)."),
    ("TPDO2", "Pack — TPDO2", "pack",
     "Transmit PDO 2 communication + empty mapping."),
    ("RPDO1+TPDO1", "Pack — RPDO1 + TPDO1", "pack",
     "One receive and one transmit PDO — typical first wiring."),
    ("Heartbeat producer", "Pack — Heartbeat producer", "pack",
     "Sets Producer heartbeat time to 1000 ms."),
    ("SDO server", "Pack — SDO server", "pack",
     "Default SDO server COB-IDs (0x600 / 0x580 + NodeID)."),
]

_PROFILE_MAP: Dict[str, List[OdEntry]] = {
    "301": CIA301_OBJECTS,
    "401": CIA401_OBJECTS,
    "402": CIA402_OBJECTS,
    "404": CIA404_OBJECTS,
    "406": CIA406_OBJECTS,
    "418": CIA418_OBJECTS,
}


def catalog_entry(profile_id: str) -> Optional[Tuple[str, str, str, str]]:
    for row in PROFILE_CATALOG:
        if row[0] == profile_id:
            return row
    return None


def objects_for(profile_id: str) -> List[OdEntry]:
    if profile_id in COMM_TEMPLATES:
        return list(COMM_TEMPLATES[profile_id])
    return list(_PROFILE_MAP.get(profile_id, CIA301_OBJECTS))


def search_profile(query: str, profile: str = "301") -> List[OdEntry]:
    src = objects_for(profile)
    q = (query or "").strip().lower()
    if not q:
        return list(src)
    out = []
    for e in src:
        if q in e.name.lower() or q in e.display_index().lower():
            out.append(e)
    return out
