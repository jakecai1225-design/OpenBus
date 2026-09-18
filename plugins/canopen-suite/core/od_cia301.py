# -*- coding: utf-8 -*-
"""CiA 301 communication profile — searchable OD stub library."""

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


# Representative CiA 301 objects (stubs for browse / insert into EDS draft).
CIA301_OBJECTS: List[OdEntry] = [
    _e(0x1000, 0, "Device type", "0x7", "0x0007", "ro", "0x00000000"),
    _e(0x1001, 0, "Error register", "0x7", "0x0005", "ro", "0x00"),
    _e(0x1003, 0, "Pre-defined error field", "0x8", "0x0007", "ro"),
    _e(0x1005, 0, "COB-ID SYNC", "0x7", "0x0007", "rw", "0x00000080"),
    _e(0x1006, 0, "Communication cycle period", "0x7", "0x0007", "rw", "0"),
    _e(0x1007, 0, "Synchronous window length", "0x7", "0x0007", "rw", "0"),
    _e(0x1008, 0, "Manufacturer device name", "0x7", "0x0009", "const"),
    _e(0x1009, 0, "Manufacturer hardware version", "0x7", "0x0009", "const"),
    _e(0x100A, 0, "Manufacturer software version", "0x7", "0x0009", "const"),
    _e(0x100C, 0, "Guard time", "0x7", "0x0006", "rw", "0"),
    _e(0x100D, 0, "Life time factor", "0x7", "0x0005", "rw", "0"),
    _e(0x1010, 0, "Store parameters", "0x8", "0x0007", "rw"),
    _e(0x1011, 0, "Restore default parameters", "0x8", "0x0007", "rw"),
    _e(0x1014, 0, "COB-ID EMCY", "0x7", "0x0007", "rw"),
    _e(0x1015, 0, "Inhibit time EMCY", "0x7", "0x0006", "rw", "0"),
    _e(0x1016, 0, "Consumer heartbeat time", "0x8", "0x0007", "rw"),
    _e(0x1017, 0, "Producer heartbeat time", "0x7", "0x0006", "rw", "0"),
    _e(0x1018, 0, "Identity object", "0x9", "0x0007", "ro"),
    _e(0x1018, 1, "Vendor-ID", "0x7", "0x0007", "ro"),
    _e(0x1018, 2, "Product code", "0x7", "0x0007", "ro"),
    _e(0x1018, 3, "Revision number", "0x7", "0x0007", "ro"),
    _e(0x1018, 4, "Serial number", "0x7", "0x0007", "ro"),
    _e(0x1019, 0, "Synchronous counter overflow value", "0x7", "0x0005", "rw"),
    # SDO server parameter
    _e(0x1200, 0, "SDO server parameter", "0x9", "0x0007", "ro"),
    _e(0x1200, 1, "COB-ID Client to Server (Rx)", "0x7", "0x0007", "ro"),
    _e(0x1200, 2, "COB-ID Server to Client (Tx)", "0x7", "0x0007", "ro"),
    # RPDO 1 communication / mapping
    _e(0x1400, 0, "RPDO 1 communication parameter", "0x9", "0x0007", "rw"),
    _e(0x1400, 1, "COB-ID used by RPDO 1", "0x7", "0x0007", "rw"),
    _e(0x1400, 2, "Transmission type", "0x7", "0x0005", "rw"),
    _e(0x1600, 0, "RPDO 1 mapping parameter", "0x9", "0x0007", "rw"),
    _e(0x1600, 1, "RPDO 1 mapping entry 1", "0x7", "0x0007", "rw"),
    # TPDO 1
    _e(0x1800, 0, "TPDO 1 communication parameter", "0x9", "0x0007", "rw"),
    _e(0x1800, 1, "COB-ID used by TPDO 1", "0x7", "0x0007", "rw"),
    _e(0x1800, 2, "Transmission type", "0x7", "0x0005", "rw"),
    _e(0x1800, 3, "Inhibit time", "0x7", "0x0006", "rw"),
    _e(0x1800, 5, "Event timer", "0x7", "0x0006", "rw"),
    _e(0x1A00, 0, "TPDO 1 mapping parameter", "0x9", "0x0007", "rw"),
    _e(0x1A00, 1, "TPDO 1 mapping entry 1", "0x7", "0x0007", "rw"),
    # RPDO2 / TPDO2 stubs
    _e(0x1401, 0, "RPDO 2 communication parameter", "0x9", "0x0007", "rw"),
    _e(0x1601, 0, "RPDO 2 mapping parameter", "0x9", "0x0007", "rw"),
    _e(0x1801, 0, "TPDO 2 communication parameter", "0x9", "0x0007", "rw"),
    _e(0x1A01, 0, "TPDO 2 mapping parameter", "0x9", "0x0007", "rw"),
]


def search_cia301(query: str) -> List[OdEntry]:
    q = (query or "").strip().lower()
    if not q:
        return list(CIA301_OBJECTS)
    out = []
    for e in CIA301_OBJECTS:
        hay = "0x%04x %s %s" % (e.index, e.name.lower(), e.display_index().lower())
        if q in hay or q in ("%04x" % e.index) or q in ("0x%04x" % e.index):
            out.append(e)
    return out
