# -*- coding: utf-8 -*-
"""Beginner device templates + profile assemble — shared by EDS Studio / Suite.

Each starter returns an edsparse.EdsDocument with FileInfo / DeviceInfo
filled and a sensible minimal OD (mandatory + profile + PDO).
"""

from __future__ import annotations

import copy
from typing import Callable, Dict, Iterable, List, Optional, Sequence, Tuple

from _shared import edsparse
from _shared.edsparse import OdEntry

from ._catalog import (
    CIA402_OBJECTS,
    CIA404_OBJECTS,
    CIA406_OBJECTS,
    CIA418_OBJECTS,
    COMM_TEMPLATES,
    objects_for,
)

# Device type defaults when assembling from a CiA profile id
_DEVICE_TYPE_HINTS = {
    "401": "0x00040191",
    "402": "0x00020192",
    "404": "0x00040400",
    "406": "0x00040600",
    "418": "0x00041800",
}


def _e(index, sub, name, otype="0x7", dtype="0x0007", access="rw", default=""):
    return OdEntry(
        index=index, subindex=sub, name=name,
        object_type=otype, data_type=dtype,
        access_type=access, default_value=default,
    )


def merge_entries(*lists: Iterable[OdEntry]) -> List[OdEntry]:
    by_key: Dict[tuple, OdEntry] = {}
    for lst in lists:
        for e in lst:
            by_key[(e.index, e.subindex)] = copy.deepcopy(e)
    return sorted(by_key.values(), key=lambda x: (x.index, x.subindex))


def _base_meta(product: str, description: str, device_type: str,
               *, created_by: str = "openbus EDS template") -> edsparse.EdsDocument:
    doc = edsparse.empty_document()
    doc.file_info.update({
        "FileName": product.replace(" ", "_") + ".eds",
        "Description": description,
        "CreatedBy": created_by,
    })
    doc.device_info.update({
        "VendorName": "sin",
        "ProductName": product,
    })
    e1000 = doc.entry(0x1000)
    if e1000:
        e1000.default_value = device_type
    return doc


def _set_heartbeat(entries: List[OdEntry], ms: str = "1000") -> None:
    for e in entries:
        if e.index == 0x1017 and e.subindex == 0:
            e.default_value = ms
            return
    entries.append(_e(0x1017, 0, "Producer heartbeat time",
                      "0x7", "0x0006", "rw", ms))


def _map_entry(index: int, sub: int, mapped_index: int, mapped_sub: int,
               bits: int, name: str) -> OdEntry:
    raw = ((mapped_index & 0xFFFF) << 16) | ((mapped_sub & 0xFF) << 8) | (bits & 0xFF)
    return _e(index, sub, name, "0x7", "0x0007", "rw", "0x%08X" % raw)


def assemble_document(
        *,
        base: str = "301",
        device: Optional[str] = None,
        packs: Sequence[str] = (),
        product: str = "New Device",
        description: str = "",
        device_type: Optional[str] = None,
        meta: Optional[dict] = None,
) -> edsparse.EdsDocument:
    """Build an EdsDocument from CiA profiles + packs (CANeds assemble path).

    *base* — communication profile id (default ``301``).
    *device* — optional device profile id (``401``, ``402``, …).
    *packs* — pack ids from COMM_TEMPLATES / catalog (``RPDO1``, ``SDO server``, …).
    """
    dtype = device_type or _DEVICE_TYPE_HINTS.get(device or "", "0x00000000")
    desc = description or (
        "Assembled from CiA %s%s"
        % (base, (" + CiA %s" % device) if device else "")
    )
    doc = _base_meta(product, desc, dtype)
    chunks: List[List[OdEntry]] = [list(objects_for(base))]
    if device:
        chunks.append(list(objects_for(device)))
    for pid in packs:
        chunks.append(list(objects_for(pid)))
    doc.entries = merge_entries(doc.entries, *chunks)
    # Ensure 0x1000 device type after merge
    e1000 = doc.entry(0x1000)
    if e1000:
        e1000.default_value = dtype
    if meta:
        if "file_info" in meta:
            doc.file_info.update(meta["file_info"])
        if "device_info" in meta:
            doc.device_info.update(meta["device_info"])
        if "device_commissioning" in meta:
            doc.device_commissioning.update(meta["device_commissioning"])
    return doc


# ---- Starter builders ----------------------------------------------------

def tpl_generic_node() -> edsparse.EdsDocument:
    """Minimal CiA 301 node — best first document for beginners."""
    doc = _base_meta(
        "Generic Node",
        "Minimal CANopen slave: mandatory objects + heartbeat + SDO",
        "0x00000000",
    )
    extra = [
        _e(0x1008, 0, "Manufacturer device name", "0x7", "0x0009", "const",
           "Generic Node"),
        _e(0x1017, 0, "Producer heartbeat time", "0x7", "0x0006", "rw", "1000"),
    ]
    extra += COMM_TEMPLATES["SDO server"]
    doc.entries = merge_entries(doc.entries, extra)
    return doc


def tpl_digital_io() -> edsparse.EdsDocument:
    """CiA 401 digital I/O with one TPDO (inputs) and one RPDO (outputs)."""
    doc = _base_meta(
        "Digital I/O Module",
        "CiA 401 starter: 8 digital in + 8 digital out, mapped on PDO1",
        "0x00040191",
    )
    io = [
        _e(0x6000, 1, "Digital inputs 1-8", "0x7", "0x0005", "ro", "0"),
        _e(0x6200, 1, "Digital outputs 1-8", "0x7", "0x0005", "rw", "0"),
    ]
    pdo = COMM_TEMPLATES["RPDO1"] + COMM_TEMPLATES["TPDO1"]
    mapped = [
        OdEntry(
            index=0x1600, subindex=0, name="Number of mapped objects RPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="1", extra={"SubNumber": "1"}),
        _map_entry(0x1600, 1, 0x6200, 1, 8, "Mapping: digital outputs"),
        OdEntry(
            index=0x1A00, subindex=0, name="Number of mapped objects TPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="1", extra={"SubNumber": "1"}),
        _map_entry(0x1A00, 1, 0x6000, 1, 8, "Mapping: digital inputs"),
    ]
    _set_heartbeat(doc.entries)
    doc.entries = merge_entries(doc.entries, io, pdo, mapped)
    return doc


def tpl_analog_io() -> edsparse.EdsDocument:
    doc = _base_meta(
        "Analogue I/O Module",
        "CiA 401 starter: 2 analogue in + 2 analogue out",
        "0x00040192",
    )
    io = [
        _e(0x6401, 1, "Analogue input 1", "0x7", "0x0003", "ro", "0"),
        _e(0x6401, 2, "Analogue input 2", "0x7", "0x0003", "ro", "0"),
        _e(0x6411, 1, "Analogue output 1", "0x7", "0x0003", "rw", "0"),
        _e(0x6411, 2, "Analogue output 2", "0x7", "0x0003", "rw", "0"),
    ]
    pdo = COMM_TEMPLATES["RPDO1+TPDO1"]
    mapped = [
        OdEntry(
            index=0x1600, subindex=0, name="Number of mapped objects RPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="2", extra={"SubNumber": "2"}),
        _map_entry(0x1600, 1, 0x6411, 1, 16, "Mapping: analogue out 1"),
        _map_entry(0x1600, 2, 0x6411, 2, 16, "Mapping: analogue out 2"),
        OdEntry(
            index=0x1A00, subindex=0, name="Number of mapped objects TPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="2", extra={"SubNumber": "2"}),
        _map_entry(0x1A00, 1, 0x6401, 1, 16, "Mapping: analogue in 1"),
        _map_entry(0x1A00, 2, 0x6401, 2, 16, "Mapping: analogue in 2"),
    ]
    _set_heartbeat(doc.entries)
    doc.entries = merge_entries(doc.entries, io, pdo, mapped)
    return doc


def tpl_servo_drive() -> edsparse.EdsDocument:
    """CiA 402 drive with controlword/statusword/position mapped."""
    doc = _base_meta(
        "Servo Drive",
        "CiA 402 starter: CSP-ready objects + RPDO/TPDO for CW/SW/position",
        "0x00020192",
    )
    drive = [
        e for e in CIA402_OBJECTS
        if e.index in (
            0x6040, 0x6041, 0x6060, 0x6061, 0x6064, 0x607A, 0x60FF, 0x6502,
            0x603F, 0x6081, 0x6083, 0x6084,
        )
    ]
    pdo = COMM_TEMPLATES["RPDO1+TPDO1"]
    mapped = [
        OdEntry(
            index=0x1600, subindex=0, name="Number of mapped objects RPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="2", extra={"SubNumber": "2"}),
        _map_entry(0x1600, 1, 0x6040, 0, 16, "Mapping: Controlword"),
        _map_entry(0x1600, 2, 0x607A, 0, 32, "Mapping: Target position"),
        OdEntry(
            index=0x1A00, subindex=0, name="Number of mapped objects TPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="2", extra={"SubNumber": "2"}),
        _map_entry(0x1A00, 1, 0x6041, 0, 16, "Mapping: Statusword"),
        _map_entry(0x1A00, 2, 0x6064, 0, 32, "Mapping: Position actual"),
    ]
    _set_heartbeat(doc.entries)
    doc.entries = merge_entries(doc.entries, drive, pdo, mapped)
    return doc


def tpl_encoder() -> edsparse.EdsDocument:
    doc = _base_meta(
        "Absolute Encoder",
        "CiA 406 starter: position value on TPDO1",
        "0x00040600",
    )
    enc = list(CIA406_OBJECTS)
    pdo = COMM_TEMPLATES["TPDO1"]
    mapped = [
        OdEntry(
            index=0x1A00, subindex=0, name="Number of mapped objects TPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="1", extra={"SubNumber": "1"}),
        _map_entry(0x1A00, 1, 0x6004, 0, 32, "Mapping: Position value"),
    ]
    _set_heartbeat(doc.entries)
    doc.entries = merge_entries(doc.entries, enc, pdo, mapped)
    return doc


def tpl_measuring() -> edsparse.EdsDocument:
    doc = _base_meta(
        "Measuring Device",
        "CiA 404 starter: process value channel on TPDO1",
        "0x00040400",
    )
    meas = [
        e for e in CIA404_OBJECTS
        if e.index in (0x6100, 0x6110, 0x6120)
    ]
    pdo = COMM_TEMPLATES["TPDO1"]
    mapped = [
        OdEntry(
            index=0x1A00, subindex=0, name="Number of mapped objects TPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="1", extra={"SubNumber": "1"}),
        _map_entry(0x1A00, 1, 0x6100, 1, 16, "Mapping: Process value ch1"),
    ]
    _set_heartbeat(doc.entries)
    doc.entries = merge_entries(doc.entries, meas, pdo, mapped)
    return doc


def tpl_battery() -> edsparse.EdsDocument:
    doc = _base_meta(
        "Battery Module",
        "CiA 418 starter: SoC / voltage / current on TPDO1",
        "0x00041800",
    )
    bat = list(CIA418_OBJECTS)
    pdo = COMM_TEMPLATES["TPDO1"]
    mapped = [
        OdEntry(
            index=0x1A00, subindex=0, name="Number of mapped objects TPDO1",
            object_type="0x7", data_type="0x0005", access_type="rw",
            default_value="3", extra={"SubNumber": "3"}),
        _map_entry(0x1A00, 1, 0x6010, 0, 8, "Mapping: State of charge"),
        _map_entry(0x1A00, 2, 0x6002, 0, 16, "Mapping: Battery voltage"),
        _map_entry(0x1A00, 3, 0x6003, 0, 16, "Mapping: Battery current"),
    ]
    _set_heartbeat(doc.entries)
    doc.entries = merge_entries(doc.entries, bat, pdo, mapped)
    return doc


def tpl_gateway_stub() -> edsparse.EdsDocument:
    """Empty manufacturer area reserved — for custom / gateway beginners."""
    doc = _base_meta(
        "Custom / Gateway Stub",
        "301 mandatory + empty manufacturer block at 0x2000 for your objects",
        "0x00000000",
    )
    custom = [
        _e(0x1008, 0, "Manufacturer device name", "0x7", "0x0009", "const",
           "Custom Device"),
        _e(0x2000, 0, "Manufacturer object (edit me)", "0x7", "0x0007", "rw", "0"),
        _e(0x2001, 0, "Manufacturer object 2 (edit me)", "0x7", "0x0007", "rw", "0"),
    ]
    custom += COMM_TEMPLATES["RPDO1+TPDO1"]
    _set_heartbeat(doc.entries)
    doc.entries = merge_entries(doc.entries, custom)
    return doc


# (id, title, blurb, builder)
STARTER_TEMPLATES: List[Tuple[str, str, str, Callable[[], edsparse.EdsDocument]]] = [
    ("generic", "Generic CANopen node",
     "Smallest valid slave: identity, heartbeat, SDO. Best first click.",
     tpl_generic_node),
    ("dio", "Digital I/O module (CiA 401)",
     "8 DI + 8 DO already mapped on TPDO1 / RPDO1. Open, edit names, save.",
     tpl_digital_io),
    ("aio", "Analogue I/O module (CiA 401)",
     "2 AI + 2 AO with PDO mapping. Good for sensor/actuator boards.",
     tpl_analog_io),
    ("servo", "Servo / drive (CiA 402)",
     "Controlword, statusword, target/actual position on PDO. Drive demo ready.",
     tpl_servo_drive),
    ("encoder", "Encoder (CiA 406)",
     "Position value on TPDO1. Absolute / incremental starter.",
     tpl_encoder),
    ("measure", "Measuring device (CiA 404)",
     "Process value channel + scaling stubs on TPDO1.",
     tpl_measuring),
    ("battery", "Battery module (CiA 418)",
     "SoC, voltage, current on TPDO1 for AGV / energy nodes.",
     tpl_battery),
    ("custom", "Custom / gateway stub",
     "301 base + empty 0x2000 manufacturer objects for your own OD.",
     tpl_gateway_stub),
]


def build_template(template_id: str) -> edsparse.EdsDocument:
    for tid, _title, _blurb, fn in STARTER_TEMPLATES:
        if tid == template_id:
            return fn()
    return tpl_generic_node()
