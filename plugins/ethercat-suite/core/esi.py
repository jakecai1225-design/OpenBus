# -*- coding: utf-8 -*-
"""EtherCAT Slave Information (ESI) XML subset.

Reads Vendor, Device Type/Name, Sm, RxPdo/TxPdo entries and CoE objects.
"""

from __future__ import annotations

import xml.etree.ElementTree as ET
from dataclasses import dataclass, field


def _ln(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _num(text: str, default: int = 0) -> int:
    t = (text or "").strip()
    if not t:
        return default
    t = t.replace("#x", "0x")
    try:
        return int(t, 16) if t.lower().startswith("0x") else int(t, 0)
    except ValueError:
        try:
            return int(float(t))
        except ValueError:
            return default


def _text(el) -> str:
    return (el.text or "").strip() if el is not None and el.text else ""


@dataclass
class PdoEntry:
    index: int
    subindex: int
    bit_len: int
    name: str


@dataclass
class Pdo:
    index: int
    name: str
    direction: str
    sm: int
    entries: list = field(default_factory=list)


@dataclass
class CoeObject:
    index: int
    name: str
    type_name: str
    bit_size: int


@dataclass
class EsiDevice:
    name: str
    type_name: str
    vendor_id: int
    vendor_name: str
    product_code: int
    revision: int
    rx_pdos: list = field(default_factory=list)
    tx_pdos: list = field(default_factory=list)
    objects: list = field(default_factory=list)


def _pdo(el, direction: str) -> Pdo:
    index = 0
    name = ""
    entries = []
    for c in list(el):
        tag = _ln(c.tag)
        if tag == "Index":
            index = _num(_text(c))
        elif tag == "Name":
            name = _text(c)
        elif tag == "Entry":
            e_index = e_sub = e_len = 0
            e_name = ""
            for f in list(c):
                ft = _ln(f.tag)
                if ft == "Index":
                    e_index = _num(_text(f))
                elif ft == "SubIndex":
                    e_sub = _num(_text(f))
                elif ft == "BitLen":
                    e_len = _num(_text(f))
                elif ft == "Name":
                    e_name = _text(f)
            if e_len or e_name:
                entries.append(PdoEntry(e_index, e_sub, e_len, e_name or "Entry"))
    sm = _num(el.attrib.get("Sm", "-1"), -1)
    return Pdo(index=index, name=name or direction, direction=direction, sm=sm, entries=entries)


def parse_esi(path: str) -> EsiDevice:
    root = ET.parse(path).getroot()
    vendor_id = 0
    vendor_name = ""
    for el in root.iter():
        if _ln(el.tag) != "Vendor":
            continue
        for c in list(el):
            if _ln(c.tag) == "Id":
                vendor_id = _num(_text(c))
            elif _ln(c.tag) == "Name":
                vendor_name = _text(c)
        break

    device = None
    for el in root.iter():
        if _ln(el.tag) == "Device":
            device = el
            break
    if device is None:
        raise ValueError("ESI has no Device")

    type_name = ""
    product = 0
    revision = 0
    name = ""
    rx, tx, objects = [], [], []
    for c in list(device):
        tag = _ln(c.tag)
        if tag == "Type":
            type_name = _text(c)
            product = _num(c.attrib.get("ProductCode", "0"))
            revision = _num(c.attrib.get("RevisionNo", "0"))
        elif tag == "Name" and not name:
            name = _text(c)
        elif tag == "RxPdo":
            rx.append(_pdo(c, "Rx"))
        elif tag == "TxPdo":
            tx.append(_pdo(c, "Tx"))
        elif tag == "Profile":
            for obj_el in c.iter():
                if _ln(obj_el.tag) != "Object":
                    continue
                idx = 0
                oname = ""
                otype = ""
                bits = 0
                for f in list(obj_el):
                    ft = _ln(f.tag)
                    if ft == "Index":
                        idx = _num(_text(f))
                    elif ft == "Name":
                        oname = _text(f)
                    elif ft == "Type":
                        otype = _text(f)
                    elif ft == "BitSize":
                        bits = _num(_text(f))
                if idx:
                    objects.append(CoeObject(idx, oname or ("0x%04X" % idx), otype, bits))

    return EsiDevice(
        name=name or type_name or "Slave",
        type_name=type_name,
        vendor_id=vendor_id,
        vendor_name=vendor_name,
        product_code=product,
        revision=revision,
        rx_pdos=rx,
        tx_pdos=tx,
        objects=objects,
    )


def _hex_attr(value: int) -> str:
    return "#x%08X" % (int(value) & 0xFFFFFFFF)


def _hex_index(value: int) -> str:
    return "#x%04X" % (int(value) & 0xFFFF)


def serialize_esi(device: EsiDevice) -> str:
    """Write EtherCATInfo that parse_esi can reload."""
    root = ET.Element("EtherCATInfo")
    vendor = ET.SubElement(root, "Vendor")
    ET.SubElement(vendor, "Id").text = str(int(device.vendor_id))
    ET.SubElement(vendor, "Name").text = device.vendor_name or "Vendor"
    desc = ET.SubElement(root, "Descriptions")
    devices = ET.SubElement(desc, "Devices")
    el = ET.SubElement(devices, "Device")
    type_el = ET.SubElement(el, "Type")
    type_el.text = device.type_name or device.name
    type_el.set("ProductCode", _hex_attr(device.product_code))
    type_el.set("RevisionNo", _hex_attr(device.revision))
    ET.SubElement(el, "Name").text = device.name

    def write_pdo(pdo, tag):
        p_el = ET.SubElement(el, tag)
        if pdo.sm >= 0:
            p_el.set("Sm", str(pdo.sm))
        p_el.set("Fixed", "1")
        ET.SubElement(p_el, "Index").text = _hex_index(pdo.index)
        ET.SubElement(p_el, "Name").text = pdo.name
        for entry in pdo.entries:
            e_el = ET.SubElement(p_el, "Entry")
            ET.SubElement(e_el, "Index").text = _hex_index(entry.index)
            ET.SubElement(e_el, "SubIndex").text = str(int(entry.subindex))
            ET.SubElement(e_el, "BitLen").text = str(int(entry.bit_len))
            ET.SubElement(e_el, "Name").text = entry.name

    for pdo in device.rx_pdos:
        write_pdo(pdo, "RxPdo")
    for pdo in device.tx_pdos:
        write_pdo(pdo, "TxPdo")

    profile = ET.SubElement(el, "Profile")
    dictionary = ET.SubElement(profile, "Dictionary")
    objects = ET.SubElement(dictionary, "Objects")
    for obj in device.objects:
        o_el = ET.SubElement(objects, "Object")
        ET.SubElement(o_el, "Index").text = _hex_index(obj.index)
        ET.SubElement(o_el, "Name").text = obj.name
        ET.SubElement(o_el, "Type").text = obj.type_name or "UDINT"
        ET.SubElement(o_el, "BitSize").text = str(int(obj.bit_size))

    ET.indent(root, space="  ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(
        root, encoding="unicode")


def validate_esi(device: EsiDevice) -> list:
    findings = []
    if not device.name:
        findings.append({"level": "error", "message": "Device name empty", "where": "Device"})
    if device.vendor_id == 0:
        findings.append({"level": "warn", "message": "Vendor Id is 0", "where": "Vendor"})
    if not device.rx_pdos and not device.tx_pdos:
        findings.append({"level": "warn", "message": "No PDO defined", "where": "PDO"})
    for pdo in list(device.rx_pdos) + list(device.tx_pdos):
        if not pdo.entries:
            findings.append({
                "level": "warn",
                "message": "%s has no entries" % pdo.name,
                "where": "0x%04X" % pdo.index,
            })
        for entry in pdo.entries:
            if entry.bit_len <= 0:
                findings.append({
                    "level": "error",
                    "message": "BitLen <= 0 on %s" % entry.name,
                    "where": "0x%04X" % pdo.index,
                })
    idxs = set()
    for obj in device.objects:
        if obj.index in idxs:
            findings.append({
                "level": "error",
                "message": "Duplicate object 0x%04X" % obj.index,
                "where": "OD",
            })
        idxs.add(obj.index)
    if 0x1000 not in idxs:
        findings.append({
            "level": "warn",
            "message": "Missing 0x1000 Device type",
            "where": "OD",
        })
    if not findings:
        findings.append({"level": "info", "message": "No issues", "where": ""})
    return findings
