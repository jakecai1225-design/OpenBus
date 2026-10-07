# -*- coding: utf-8 -*-
"""CiA profile object stubs — communication + common device profiles."""

from __future__ import annotations

from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple

from _shared.edsparse import OdEntry

# Catalog row: (id, title, kind, category, blurb)
# kind: profile | pack
# category: Communication | Device | Pack
CatalogRow = Tuple[str, str, str, str, str]


def _e(index, sub, name, otype="0x7", dtype="0x0007", access="rw", default="",
       pdo=""):
    return OdEntry(
        index=index, subindex=sub, name=name, object_type=otype,
        data_type=dtype, access_type=access, default_value=default,
        pdo_mapping=pdo,
    )


def _rec(index, name, sub_number: int) -> OdEntry:
    return OdEntry(
        index=index, subindex=0, name=name, object_type="0x9",
        extra={"SubNumber": str(sub_number)})


def _arr(index, name, dtype="0x0005", access="ro") -> OdEntry:
    return OdEntry(
        index=index, subindex=0, name=name, object_type="0x8",
        data_type=dtype, access_type=access)


def _pdo_comm(index: int, cob: str, name: str) -> List[OdEntry]:
    return [
        OdEntry(index, 0, name, "0x9", extra={"SubNumber": "2"}),
        _e(index, 1, "COB-ID used by %s" % name, "0x7", "0x0007", "rw", cob),
        _e(index, 2, "Transmission type", "0x7", "0x0005", "rw", "255"),
    ]


def _pdo_map_empty(index: int, name: str) -> List[OdEntry]:
    return [
        OdEntry(
            index=index, subindex=0, name=name, object_type="0x7",
            data_type="0x0005", access_type="rw", default_value="0",
            extra={"SubNumber": "1"}),
    ]


# ---- CiA 301 Communication ----
CIA301_OBJECTS: List[OdEntry] = [
    _e(0x1000, 0, "Device type", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1001, 0, "Error register", "0x7", "0x0005", "ro", "0x00"),
    _e(0x1002, 0, "Manufacturer status register", "0x7", "0x0007", "ro", "0"),
    _arr(0x1003, "Pre-defined error field", "0x0007", "ro"),
    _e(0x1003, 1, "Standard error field", "0x7", "0x0007", "ro", "0"),
    _e(0x1005, 0, "COB-ID SYNC", "0x7", "0x0007", "rw", "0x00000080"),
    _e(0x1006, 0, "Communication cycle period", "0x7", "0x0007", "rw", "0"),
    _e(0x1007, 0, "Synchronous window length", "0x7", "0x0007", "rw", "0"),
    _e(0x1008, 0, "Manufacturer device name", "0x7", "0x0009", "const"),
    _e(0x1009, 0, "Manufacturer hardware version", "0x7", "0x0009", "const"),
    _e(0x100A, 0, "Manufacturer software version", "0x7", "0x0009", "const"),
    _e(0x100C, 0, "Guard time", "0x7", "0x0006", "rw", "0"),
    _e(0x100D, 0, "Life time factor", "0x7", "0x0005", "rw", "0"),
    _arr(0x1010, "Store parameters", "", "rw"),
    _e(0x1010, 1, "Save all parameters", "0x7", "0x0007", "rw", "0"),
    _arr(0x1011, "Restore default parameters", "", "rw"),
    _e(0x1011, 1, "Restore all defaults", "0x7", "0x0007", "rw", "0"),
    _e(0x1014, 0, "COB-ID EMCY", "0x7", "0x0007", "rw"),
    _e(0x1015, 0, "Inhibit time EMCY", "0x7", "0x0006", "rw", "0"),
    _arr(0x1016, "Consumer heartbeat time", "", "rw"),
    _e(0x1016, 1, "Consumer heartbeat time", "0x7", "0x0007", "rw", "0"),
    _e(0x1017, 0, "Producer heartbeat time", "0x7", "0x0006", "rw", "1000"),
    OdEntry(0x1018, 0, "Identity object", "0x8", extra={"SubNumber": "4"}),
    _e(0x1018, 1, "Vendor-ID", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1018, 2, "Product code", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1018, 3, "Revision number", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1018, 4, "Serial number", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1029, 0, "Error behaviour object", "0x8", "", "rw"),
    _e(0x1029, 1, "Communication error", "0x7", "0x0005", "rw", "0"),
    _rec(0x1200, "SDO server parameter", 2),
    _e(0x1200, 1, "COB-ID Client → Server (rx)", "0x7", "0x0007", "ro", "0x600"),
    _e(0x1200, 2, "COB-ID Server → Client (tx)", "0x7", "0x0007", "ro", "0x580"),
]
CIA301_OBJECTS.extend(_pdo_comm(0x1400, "0x200", "RPDO communication parameter"))
CIA301_OBJECTS.extend(_pdo_comm(0x1401, "0x300", "RPDO communication parameter"))
CIA301_OBJECTS.extend(_pdo_comm(0x1402, "0x400", "RPDO communication parameter"))
CIA301_OBJECTS.extend(_pdo_comm(0x1403, "0x500", "RPDO communication parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1600, "RPDO mapping parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1601, "RPDO mapping parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1602, "RPDO mapping parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1603, "RPDO mapping parameter"))
CIA301_OBJECTS.extend(_pdo_comm(0x1800, "0x180", "TPDO communication parameter"))
CIA301_OBJECTS.extend([
    _e(0x1800, 3, "Inhibit time", "0x7", "0x0006", "rw", "0"),
    _e(0x1800, 5, "Event timer", "0x7", "0x0006", "rw", "0"),
])
for _e1800 in CIA301_OBJECTS:
    if _e1800.index == 0x1800 and _e1800.subindex == 0:
        _e1800.extra["SubNumber"] = "5"
        break
CIA301_OBJECTS.extend(_pdo_comm(0x1801, "0x280", "TPDO communication parameter"))
CIA301_OBJECTS.extend(_pdo_comm(0x1802, "0x380", "TPDO communication parameter"))
CIA301_OBJECTS.extend(_pdo_comm(0x1803, "0x480", "TPDO communication parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1A00, "TPDO mapping parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1A01, "TPDO mapping parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1A02, "TPDO mapping parameter"))
CIA301_OBJECTS.extend(_pdo_map_empty(0x1A03, "TPDO mapping parameter"))


# ---- CiA 302 Network management (manager / flying master lite) ----
CIA302_OBJECTS: List[OdEntry] = [
    _e(0x1F80, 0, "NMT startup", "0x7", "0x0007", "rw", "0x00000000"),
    _e(0x1F81, 0, "NMT slave assignment", "0x8", "", "rw"),
    _e(0x1F81, 1, "Slave assignment entry", "0x7", "0x0007", "rw", "0"),
    _e(0x1F82, 0, "Request NMT", "0x8", "", "rw"),
    _e(0x1F84, 0, "Device type identification", "0x8", "", "ro"),
    _e(0x1F85, 0, "Vendor identification", "0x8", "", "ro"),
    _e(0x1F86, 0, "Product code", "0x8", "", "ro"),
    _e(0x1F87, 0, "Revision number", "0x8", "", "ro"),
    _e(0x1F88, 0, "Serial number", "0x8", "", "ro"),
    _e(0x1F89, 0, "Boot time", "0x7", "0x0007", "rw", "0"),
]


# ---- Device profiles ----
CIA401_OBJECTS: List[OdEntry] = [
    _arr(0x6000, "Read input 8-bit", "0x0005", "ro"),
    _e(0x6000, 1, "Digital inputs 1-8", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6000, 2, "Digital inputs 9-16", "0x7", "0x0005", "ro", "0", "1"),
    _arr(0x6100, "Read input 16-bit", "0x0006", "ro"),
    _e(0x6100, 1, "Digital inputs 1-16", "0x7", "0x0006", "ro", "0", "1"),
    _arr(0x6200, "Write output 8-bit", "0x0005", "rw"),
    _e(0x6200, 1, "Digital outputs 1-8", "0x7", "0x0005", "rw", "0", "1"),
    _e(0x6200, 2, "Digital outputs 9-16", "0x7", "0x0005", "rw", "0", "1"),
    _arr(0x6300, "Write output 16-bit", "0x0006", "rw"),
    _e(0x6300, 1, "Digital outputs 1-16", "0x7", "0x0006", "rw", "0", "1"),
    _arr(0x6401, "Read analogue input 16-bit", "0x0003", "ro"),
    _e(0x6401, 1, "Analogue input 1", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6401, 2, "Analogue input 2", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6401, 3, "Analogue input 3", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6401, 4, "Analogue input 4", "0x7", "0x0003", "ro", "0", "1"),
    _arr(0x6411, "Write analogue output 16-bit", "0x0003", "rw"),
    _e(0x6411, 1, "Analogue output 1", "0x7", "0x0003", "rw", "0", "1"),
    _e(0x6411, 2, "Analogue output 2", "0x7", "0x0003", "rw", "0", "1"),
]


CIA402_OBJECTS: List[OdEntry] = [
    _e(0x6007, 0, "Abort connection option code", "0x7", "0x0003", "rw", "3"),
    _e(0x603F, 0, "Error code", "0x7", "0x0006", "ro", "0"),
    _e(0x6040, 0, "Controlword", "0x7", "0x0006", "rw", "0", "1"),
    _e(0x6041, 0, "Statusword", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x605A, 0, "Quick stop option code", "0x7", "0x0003", "rw", "2"),
    _e(0x605B, 0, "Shutdown option code", "0x7", "0x0003", "rw", "0"),
    _e(0x605C, 0, "Disable operation option code", "0x7", "0x0003", "rw", "1"),
    _e(0x605D, 0, "Halt option code", "0x7", "0x0003", "rw", "1"),
    _e(0x605E, 0, "Fault reaction option code", "0x7", "0x0003", "rw", "2"),
    _e(0x6060, 0, "Modes of operation", "0x7", "0x0002", "rw", "8"),
    _e(0x6061, 0, "Modes of operation display", "0x7", "0x0002", "ro", "8"),
    _e(0x6062, 0, "Position demand value", "0x7", "0x0004", "ro", "0"),
    _e(0x6063, 0, "Position actual value (internal)", "0x7", "0x0004", "ro", "0"),
    _e(0x6064, 0, "Position actual value", "0x7", "0x0004", "ro", "0", "1"),
    _e(0x6065, 0, "Following error window", "0x7", "0x0007", "rw", "0"),
    _e(0x6066, 0, "Following error time out", "0x7", "0x0006", "rw", "0"),
    _e(0x6067, 0, "Position window", "0x7", "0x0007", "rw", "0"),
    _e(0x6068, 0, "Position window time", "0x7", "0x0006", "rw", "0"),
    _e(0x606B, 0, "Velocity demand value", "0x7", "0x0004", "ro", "0"),
    _e(0x606C, 0, "Velocity actual value", "0x7", "0x0004", "ro", "0", "1"),
    _e(0x606D, 0, "Velocity window", "0x7", "0x0006", "rw", "0"),
    _e(0x606F, 0, "Velocity threshold", "0x7", "0x0006", "rw", "0"),
    _e(0x6071, 0, "Target torque", "0x7", "0x0003", "rw", "0", "1"),
    _e(0x6072, 0, "Max torque", "0x7", "0x0006", "rw", "1000"),
    _e(0x6077, 0, "Torque actual value", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x607A, 0, "Target position", "0x7", "0x0004", "rw", "0", "1"),
    _e(0x607C, 0, "Home offset", "0x7", "0x0004", "rw", "0"),
    _e(0x607D, 0, "Software position limit", "0x8", "", "rw"),
    _e(0x607D, 1, "Min position limit", "0x7", "0x0004", "rw", "0"),
    _e(0x607D, 2, "Max position limit", "0x7", "0x0004", "rw", "0"),
    _e(0x6081, 0, "Profile velocity", "0x7", "0x0007", "rw", "0"),
    _e(0x6083, 0, "Profile acceleration", "0x7", "0x0007", "rw", "0"),
    _e(0x6084, 0, "Profile deceleration", "0x7", "0x0007", "rw", "0"),
    _e(0x6085, 0, "Quick stop deceleration", "0x7", "0x0007", "rw", "0"),
    _e(0x6086, 0, "Motion profile type", "0x7", "0x0002", "rw", "0"),
    _e(0x6098, 0, "Homing method", "0x7", "0x0002", "rw", "35"),
    _e(0x6099, 0, "Homing speeds", "0x8", "", "rw"),
    _e(0x6099, 1, "Speed during search for switch", "0x7", "0x0007", "rw", "0"),
    _e(0x6099, 2, "Speed during search for zero", "0x7", "0x0007", "rw", "0"),
    _e(0x609A, 0, "Homing acceleration", "0x7", "0x0007", "rw", "0"),
    _e(0x60C2, 0, "Interpolation time period", "0x8", "", "rw"),
    _e(0x60C2, 1, "Interpolation time period value", "0x7", "0x0005", "rw", "1"),
    _e(0x60C2, 2, "Interpolation time index", "0x7", "0x0002", "rw", "-3"),
    _e(0x60FF, 0, "Target velocity", "0x7", "0x0004", "rw", "0", "1"),
    _e(0x6502, 0, "Supported drive modes", "0x7", "0x0007", "ro", "0x000003AD"),
]


CIA403_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Keyboard input", "0x8", "", "ro"),
    _e(0x6000, 1, "Key state", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6200, 0, "LED output", "0x8", "", "rw"),
    _e(0x6200, 1, "LED pattern", "0x7", "0x0005", "rw", "0", "1"),
    _e(0x6300, 0, "Display text", "0x7", "0x0009", "rw", ""),
    _e(0x6400, 0, "Buzzer control", "0x7", "0x0005", "rw", "0"),
]


CIA404_OBJECTS: List[OdEntry] = [
    _arr(0x6100, "Input process value (16-bit)", "", "ro"),
    _e(0x6100, 1, "Process value channel 1", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6100, 2, "Process value channel 2", "0x7", "0x0003", "ro", "0", "1"),
    _arr(0x6110, "Input scaling 1st parameter", "", "rw"),
    _e(0x6110, 1, "Scaling numerator ch1", "0x7", "0x0004", "rw", "1"),
    _arr(0x6112, "Input filter constant", "", "rw"),
    _e(0x6112, 1, "Filter constant ch1", "0x7", "0x0007", "rw", "0"),
    _arr(0x6120, "Physical unit", "", "rw"),
    _e(0x6120, 1, "Unit channel 1", "0x7", "0x0007", "rw", "0"),
    _arr(0x6130, "Input operating mode", "", "rw"),
    _e(0x6130, 1, "Operating mode ch1", "0x7", "0x0005", "rw", "0"),
    _e(0x6140, 0, "Input status", "0x7", "0x0005", "ro", "0"),
]


CIA405_OBJECTS: List[OdEntry] = [
    _e(0x1F50, 0, "Program download", "0x8", "", "rw"),
    _e(0x1F51, 0, "Program control", "0x7", "0x0005", "rw", "0"),
    _e(0x1F52, 0, "Program status", "0x7", "0x0005", "ro", "0"),
    _e(0x6001, 0, "PLC control", "0x7", "0x0005", "rw", "0"),
    _arr(0x6100, "Digital inputs (PLC)", "0x0005", "ro"),
    _e(0x6100, 1, "Input byte 1", "0x7", "0x0005", "ro", "0", "1"),
    _arr(0x6200, "Digital outputs (PLC)", "0x0005", "rw"),
    _e(0x6200, 1, "Output byte 1", "0x7", "0x0005", "rw", "0", "1"),
]


CIA406_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Operating parameters", "0x7", "0x0007", "rw", "0"),
    _e(0x6001, 0, "Measuring units per revolution", "0x7", "0x0007", "rw", "1024"),
    _e(0x6002, 0, "Total measuring range (revolution)", "0x7", "0x0007", "rw", "1"),
    _e(0x6003, 0, "Preset value", "0x7", "0x0007", "rw", "0"),
    _e(0x6004, 0, "Position value", "0x7", "0x0007", "ro", "0", "1"),
    _e(0x6500, 0, "Operating status", "0x7", "0x0007", "ro", "0"),
    _e(0x6501, 0, "Single-turn resolution", "0x7", "0x0007", "ro", "1024"),
    _e(0x6502, 0, "Number of distinguishable revolutions", "0x7", "0x0007", "ro", "1"),
]


CIA408_OBJECTS: List[OdEntry] = [
    _e(0x6040, 0, "Controlword", "0x7", "0x0006", "rw", "0", "1"),
    _e(0x6041, 0, "Statusword", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6060, 0, "Modes of operation", "0x7", "0x0002", "rw", "1"),
    _e(0x6130, 0, "Valve demand value", "0x7", "0x0003", "rw", "0", "1"),
    _e(0x6131, 0, "Valve actual value", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6140, 0, "Pressure demand", "0x7", "0x0006", "rw", "0"),
    _e(0x6141, 0, "Pressure actual", "0x7", "0x0006", "ro", "0", "1"),
]


CIA410_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Operating parameters", "0x7", "0x0007", "rw", "0"),
    _e(0x6010, 0, "Inclination X", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6011, 0, "Inclination Y", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6012, 0, "Inclination Z", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6020, 0, "Angular velocity X", "0x7", "0x0003", "ro", "0"),
    _e(0x6100, 0, "Sensor resolution", "0x7", "0x0006", "ro", "1000"),
]


CIA412_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Device status", "0x7", "0x0005", "ro", "0"),
    _e(0x6010, 0, "Patient ID", "0x7", "0x0009", "rw", ""),
    _e(0x6020, 0, "Measurement value", "0x7", "0x0004", "ro", "0", "1"),
    _e(0x6021, 0, "Measurement unit", "0x7", "0x0007", "ro", "0"),
    _e(0x6030, 0, "Alarm status", "0x7", "0x0005", "ro", "0"),
]


CIA413_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Gateway status", "0x7", "0x0005", "ro", "0"),
    _e(0x6010, 0, "J1939 PGN map count", "0x7", "0x0005", "rw", "0"),
    _arr(0x6020, "Mapped PGN list", "", "rw"),
    _e(0x6020, 1, "PGN entry 1", "0x7", "0x0007", "rw", "0"),
    _e(0x6100, 0, "Truck network baud", "0x7", "0x0007", "rw", "250"),
]


CIA414_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Loom status", "0x7", "0x0005", "ro", "0"),
    _e(0x6010, 0, "Weft density", "0x7", "0x0006", "rw", "0"),
    _e(0x6020, 0, "Warp tension", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6030, 0, "Machine speed", "0x7", "0x0006", "ro", "0", "1"),
]


CIA415_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Machine status", "0x7", "0x0005", "ro", "0"),
    _e(0x6010, 0, "Drum speed", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6020, 0, "Paver screed height", "0x7", "0x0003", "rw", "0"),
    _e(0x6030, 0, "Material flow", "0x7", "0x0006", "ro", "0"),
]


CIA416_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Door status", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6001, 0, "Door control", "0x7", "0x0005", "rw", "0", "1"),
    _e(0x6010, 0, "Open position", "0x7", "0x0006", "rw", "0"),
    _e(0x6011, 0, "Close position", "0x7", "0x0006", "rw", "0"),
    _e(0x6020, 0, "Obstacle detection", "0x7", "0x0005", "ro", "0"),
]


CIA417_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Lift status", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6001, 0, "Lift command", "0x7", "0x0005", "rw", "0", "1"),
    _e(0x6010, 0, "Current floor", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6011, 0, "Target floor", "0x7", "0x0005", "rw", "0"),
    _e(0x6020, 0, "Door state", "0x7", "0x0005", "ro", "0"),
    _e(0x6030, 0, "Car load", "0x7", "0x0006", "ro", "0"),
]


CIA418_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Battery status", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6001, 0, "Battery temperature", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6002, 0, "Battery voltage", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6003, 0, "Battery current", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6010, 0, "State of charge", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6011, 0, "State of health", "0x7", "0x0005", "ro", "100"),
    _e(0x6020, 0, "Nominal voltage", "0x7", "0x0006", "const", "4800"),
    _e(0x6021, 0, "Nominal capacity", "0x7", "0x0006", "const", "100"),
]


CIA419_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Charger status", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6001, 0, "Charger control", "0x7", "0x0005", "rw", "0"),
    _e(0x6010, 0, "Output voltage", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6011, 0, "Output current", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6020, 0, "Target voltage", "0x7", "0x0006", "rw", "0"),
    _e(0x6021, 0, "Target current", "0x7", "0x0006", "rw", "0"),
]


CIA420_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Extruder status", "0x7", "0x0005", "ro", "0"),
    _e(0x6010, 0, "Screw speed", "0x7", "0x0006", "rw", "0", "1"),
    _e(0x6020, 0, "Melt pressure", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6030, 0, "Melt temperature", "0x7", "0x0003", "ro", "0", "1"),
    _e(0x6040, 0, "Zone temperature setpoints", "0x8", "", "rw"),
    _e(0x6040, 1, "Zone 1 setpoint", "0x7", "0x0003", "rw", "0"),
]


CIA421_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Downstream status", "0x7", "0x0005", "ro", "0"),
    _e(0x6010, 0, "Haul-off speed", "0x7", "0x0006", "rw", "0", "1"),
    _e(0x6020, 0, "Film thickness", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6030, 0, "Winder tension", "0x7", "0x0006", "rw", "0"),
]


CIA422_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Vehicle status", "0x7", "0x0005", "ro", "0", "1"),
    _e(0x6010, 0, "PTO control", "0x7", "0x0005", "rw", "0", "1"),
    _e(0x6020, 0, "Body function bitmask", "0x7", "0x0007", "rw", "0"),
    _e(0x6030, 0, "Hydraulic pressure", "0x7", "0x0006", "ro", "0", "1"),
]


CIA434_OBJECTS: List[OdEntry] = [
    _e(0x6000, 0, "Instrument status", "0x7", "0x0005", "ro", "0"),
    _e(0x6010, 0, "Measured value", "0x7", "0x0004", "ro", "0", "1"),
    _e(0x6020, 0, "Method ID", "0x7", "0x0009", "rw", ""),
    _e(0x6030, 0, "Sample ID", "0x7", "0x0009", "rw", ""),
]


CIA437_OBJECTS: List[OdEntry] = [
    _e(0x6040, 0, "Controlword", "0x7", "0x0006", "rw", "0", "1"),
    _e(0x6041, 0, "Statusword", "0x7", "0x0006", "ro", "0", "1"),
    _e(0x6060, 0, "Modes of operation", "0x7", "0x0002", "rw", "1"),
    _e(0x6071, 0, "Target torque", "0x7", "0x0003", "rw", "0", "1"),
    _e(0x60FF, 0, "Target velocity", "0x7", "0x0004", "rw", "0", "1"),
    _e(0x6402, 0, "Motor type", "0x7", "0x0005", "ro", "0"),
    _e(0x6410, 0, "Motor rated current", "0x7", "0x0006", "ro", "0"),
]


COMM_TEMPLATES: Dict[str, List[OdEntry]] = {
    "RPDO1": _pdo_comm(0x1400, "0x200", "RPDO1") + _pdo_map_empty(
        0x1600, "RPDO1 mapping parameter"),
    "RPDO2": _pdo_comm(0x1401, "0x300", "RPDO2") + _pdo_map_empty(
        0x1601, "RPDO2 mapping parameter"),
    "RPDO3": _pdo_comm(0x1402, "0x400", "RPDO3") + _pdo_map_empty(
        0x1602, "RPDO3 mapping parameter"),
    "RPDO4": _pdo_comm(0x1403, "0x500", "RPDO4") + _pdo_map_empty(
        0x1603, "RPDO4 mapping parameter"),
    "TPDO1": _pdo_comm(0x1800, "0x180", "TPDO1") + _pdo_map_empty(
        0x1A00, "TPDO1 mapping parameter"),
    "TPDO2": _pdo_comm(0x1801, "0x280", "TPDO2") + _pdo_map_empty(
        0x1A01, "TPDO2 mapping parameter"),
    "TPDO3": _pdo_comm(0x1802, "0x380", "TPDO3") + _pdo_map_empty(
        0x1A02, "TPDO3 mapping parameter"),
    "TPDO4": _pdo_comm(0x1803, "0x480", "TPDO4") + _pdo_map_empty(
        0x1A03, "TPDO4 mapping parameter"),
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
        _e(0x1200, 1, "COB-ID Client → Server (rx)", "0x7", "0x0007", "ro", "0x600"),
        _e(0x1200, 2, "COB-ID Server → Client (tx)", "0x7", "0x0007", "ro", "0x580"),
    ],
    "Identity": [
        OdEntry(0x1018, 0, "Identity object", "0x8", extra={"SubNumber": "4"}),
        _e(0x1018, 1, "Vendor-ID", "0x7", "0x0007", "ro", "0x00000000"),
        _e(0x1018, 2, "Product code", "0x7", "0x0007", "ro", "0x00000000"),
        _e(0x1018, 3, "Revision number", "0x7", "0x0007", "ro", "0x00000000"),
        _e(0x1018, 4, "Serial number", "0x7", "0x0007", "ro", "0x00000000"),
    ],
    "EMCY": [
        _e(0x1014, 0, "COB-ID EMCY", "0x7", "0x0007", "rw"),
        _e(0x1015, 0, "Inhibit time EMCY", "0x7", "0x0006", "rw", "0"),
        _e(0x1001, 0, "Error register", "0x7", "0x0005", "ro", "0x00"),
    ],
}


_PROFILE_MAP: Dict[str, List[OdEntry]] = {
    "301": CIA301_OBJECTS,
    "302": CIA302_OBJECTS,
    "401": CIA401_OBJECTS,
    "402": CIA402_OBJECTS,
    "403": CIA403_OBJECTS,
    "404": CIA404_OBJECTS,
    "405": CIA405_OBJECTS,
    "406": CIA406_OBJECTS,
    "408": CIA408_OBJECTS,
    "410": CIA410_OBJECTS,
    "412": CIA412_OBJECTS,
    "413": CIA413_OBJECTS,
    "414": CIA414_OBJECTS,
    "415": CIA415_OBJECTS,
    "416": CIA416_OBJECTS,
    "417": CIA417_OBJECTS,
    "418": CIA418_OBJECTS,
    "419": CIA419_OBJECTS,
    "420": CIA420_OBJECTS,
    "421": CIA421_OBJECTS,
    "422": CIA422_OBJECTS,
    "434": CIA434_OBJECTS,
    "437": CIA437_OBJECTS,
}


PROFILE_CATALOG: List[CatalogRow] = [
    ("301", "CiA 301 — Communication", "profile", "Communication",
     "Core CANopen objects every node needs (device type, identity, "
     "heartbeat, SDO, 4× RPDO/TPDO shells). CANeds daily starting point."),
    ("302", "CiA 302 — Network management", "profile", "Communication",
     "NMT startup, slave assignment and identity checks for managers."),
    ("401", "CiA 401 — Digital / analogue I/O", "profile", "Device",
     "Generic I/O module: digital in/out and analogue channels."),
    ("402", "CiA 402 — Drives and motion", "profile", "Device",
     "Servo / inverter: controlword, statusword, PP/PV/PT/Homing objects."),
    ("403", "CiA 403 — Human-machine interface", "profile", "Device",
     "Keypads, LEDs, displays and buzzers."),
    ("404", "CiA 404 — Measuring devices", "profile", "Device",
     "Sensors / transducers: process value, scaling, filter, units."),
    ("405", "CiA 405 — IEC 61131-3", "profile", "Device",
     "Programmable devices / soft-PLC program control objects."),
    ("406", "CiA 406 — Encoders", "profile", "Device",
     "Incremental / absolute encoder position and resolution."),
    ("408", "CiA 408 — Fluid power / valves", "profile", "Device",
     "Hydraulic / pneumatic valve demand and pressure feedback."),
    ("410", "CiA 410 — Inclinometer", "profile", "Device",
     "Inclination and angular rate sensing."),
    ("412", "CiA 412 — Medical devices", "profile", "Device",
     "Simplified medical instrument measurement / alarm stubs."),
    ("413", "CiA 413 — Truck gateways", "profile", "Device",
     "J1939 / truck network gateway mapping stubs."),
    ("414", "CiA 414 — Weaving machines", "profile", "Device",
     "Textile loom process objects."),
    ("415", "CiA 415 — Road construction", "profile", "Device",
     "Paver / roller machine process objects."),
    ("416", "CiA 416 — Building door control", "profile", "Device",
     "Automatic door status and control."),
    ("417", "CiA 417 — Lift control", "profile", "Device",
     "Elevator car / floor / door objects."),
    ("418", "CiA 418 — Battery modules", "profile", "Device",
     "Battery status, SoC/SoH, voltage and current."),
    ("419", "CiA 419 — Battery chargers", "profile", "Device",
     "Charger control, output voltage / current setpoints."),
    ("420", "CiA 420 — Extruder upstream", "profile", "Device",
     "Extruder screw, melt pressure / temperature."),
    ("421", "CiA 421 — Extruder downstream", "profile", "Device",
     "Haul-off, thickness and winder objects."),
    ("422", "CiA 422 — Municipal vehicles", "profile", "Device",
     "PTO / body / hydraulic municipal vehicle stubs."),
    ("434", "CiA 434 — Laboratory automation", "profile", "Device",
     "Lab instrument measurement and method IDs."),
    ("437", "CiA 437 — Powertrain", "profile", "Device",
     "Additional powertrain / motor rated data alongside 402."),
    ("RPDO1", "Pack — RPDO1", "pack", "Pack",
     "Receive PDO 1 communication + empty mapping."),
    ("RPDO2", "Pack — RPDO2", "pack", "Pack",
     "Receive PDO 2 communication + empty mapping."),
    ("RPDO3", "Pack — RPDO3", "pack", "Pack",
     "Receive PDO 3 communication + empty mapping."),
    ("RPDO4", "Pack — RPDO4", "pack", "Pack",
     "Receive PDO 4 communication + empty mapping."),
    ("TPDO1", "Pack — TPDO1", "pack", "Pack",
     "Transmit PDO 1 communication + empty mapping."),
    ("TPDO2", "Pack — TPDO2", "pack", "Pack",
     "Transmit PDO 2 communication + empty mapping."),
    ("TPDO3", "Pack — TPDO3", "pack", "Pack",
     "Transmit PDO 3 communication + empty mapping."),
    ("TPDO4", "Pack — TPDO4", "pack", "Pack",
     "Transmit PDO 4 communication + empty mapping."),
    ("RPDO1+TPDO1", "Pack — RPDO1 + TPDO1", "pack", "Pack",
     "One receive and one transmit PDO — typical first wiring."),
    ("Heartbeat producer", "Pack — Heartbeat producer", "pack", "Pack",
     "Sets Producer heartbeat time to 1000 ms."),
    ("SDO server", "Pack — SDO server", "pack", "Pack",
     "Default SDO server COB-IDs (0x600 / 0x580 + NodeID)."),
    ("Identity", "Pack — Identity (0x1018)", "pack", "Pack",
     "Vendor-ID / product / revision / serial record."),
    ("EMCY", "Pack — Emergency", "pack", "Pack",
     "EMCY COB-ID, inhibit time and error register."),
]


def catalog_entry(profile_id: str) -> Optional[CatalogRow]:
    for row in PROFILE_CATALOG:
        if row[0] == profile_id:
            return row
    return None


def catalog_by_category() -> Dict[str, List[CatalogRow]]:
    out: Dict[str, List[CatalogRow]] = {}
    for row in PROFILE_CATALOG:
        out.setdefault(row[3], []).append(row)
    return out


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


def present_keys(entries: Iterable[OdEntry]) -> Set[Tuple[int, int]]:
    return {(e.index, e.subindex) for e in entries}


def missing_entries(
        profile_id: str,
        existing: Sequence[OdEntry]) -> List[OdEntry]:
    have = present_keys(existing)
    return [e for e in objects_for(profile_id)
            if (e.index, e.subindex) not in have]


# Key objects expected when a device profile is declared / dominant.
# Used by Validate (CANeds-style profile coverage), not a full CODB check.
PROFILE_EXPECTED: Dict[str, List[Tuple[int, int, str]]] = {
    "301": [
        (0x1000, 0, "Device type"),
        (0x1001, 0, "Error register"),
        (0x1017, 0, "Producer heartbeat time"),
        (0x1018, 0, "Identity object"),
        (0x1200, 0, "SDO server parameter"),
    ],
    "401": [
        (0x6000, 0, "Read input 8-bit"),
        (0x6200, 0, "Write output 8-bit"),
    ],
    "402": [
        (0x6040, 0, "Controlword"),
        (0x6041, 0, "Statusword"),
        (0x6060, 0, "Modes of operation"),
        (0x6064, 0, "Position actual value"),
    ],
    "404": [
        (0x6100, 0, "Input process value"),
    ],
    "406": [
        (0x6004, 0, "Position value"),
        (0x6501, 0, "Single-turn resolution"),
    ],
    "418": [
        (0x6000, 0, "Battery status"),
        (0x6010, 0, "State of charge"),
    ],
    "419": [
        (0x6000, 0, "Charger status"),
        (0x6010, 0, "Output voltage"),
    ],
}


def detect_device_profile(entries: Sequence[OdEntry]) -> Optional[str]:
    """Infer CiA device profile id from 0x1000 Device type (low 16 bits)."""
    for e in entries:
        if e.index == 0x1000 and e.subindex == 0:
            raw = (e.parameter_value or e.default_value or "").strip()
            if not raw:
                break
            try:
                val = int(raw, 0)
            except ValueError:
                break
            profile_num = val & 0xFFFF
            # Match catalog ids that are decimal strings of the profile number
            pid = str(profile_num)
            if catalog_entry(pid):
                return pid
            break
    return None


def validate_profile_coverage(
        entries: Sequence[OdEntry],
        profile_id: Optional[str] = None) -> List[dict]:
    """Return warn/info findings for missing profile key objects."""
    findings: List[dict] = []
    pid = profile_id or detect_device_profile(entries)
    if not pid:
        # Heuristic: if 402-ish objects present without 0x1000 profile bits
        have = {(e.index, e.subindex) for e in entries}
        if (0x6040, 0) in have or (0x6041, 0) in have:
            pid = "402"
        elif (0x6000, 1) in have and (0x6200, 1) in have:
            pid = "401"
        else:
            return findings

    expected = PROFILE_EXPECTED.get(pid)
    if not expected:
        # Fall back: first 8 top-level objects from catalog
        expected = [
            (e.index, e.subindex, e.name)
            for e in objects_for(pid)
            if e.subindex == 0
        ][:8]

    have = {(e.index, e.subindex) for e in entries}
    meta = catalog_entry(pid)
    title = meta[1] if meta else ("CiA %s" % pid)
    findings.append({
        "level": "info",
        "severity": "info",
        "rule": "profile",
        "message": "Checking coverage for %s" % title,
        "index": "",
        "od_index": None,
        "od_sub": None,
        "profile": pid,
    })
    for idx, sub, name in expected:
        if (idx, sub) not in have:
            findings.append({
                "level": "warn",
                "severity": "warning",
                "rule": "profile_object",
                "message": "Profile %s expects %s (0x%04X%s)"
                % (pid, name, idx, (":%02X" % sub) if sub else ""),
                "index": "0x%04X" % idx + ((":%02X" % sub) if sub else ""),
                "od_index": idx,
                "od_sub": sub,
                "profile": pid,
            })
    return findings
