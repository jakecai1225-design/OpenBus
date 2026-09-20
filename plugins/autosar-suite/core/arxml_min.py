# -*- coding: utf-8 -*-
"""Minimal AUTOSAR XML extract: I-SIGNAL, I-SIGNAL-I-PDU, CAN-FRAME, triggering.

Namespaces are ignored. This is not an ARXML schema validator.
"""

from __future__ import annotations

import xml.etree.ElementTree as ET

from core.ipdu import ENDIAN_INTEL, Ipdu, Signal, normalize_endian


def _ln(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _text(el, name: str, default: str = "") -> str:
    for c in list(el):
        if _ln(c.tag) == name and c.text and c.text.strip():
            return c.text.strip()
    return default


def _num(text: str, default: int = 0) -> int:
    t = (text or "").strip()
    if not t:
        return default
    try:
        if t.lower().startswith("0x") or t.lower().startswith("#x"):
            return int(t.replace("#x", "0x"), 16)
        return int(float(t))
    except ValueError:
        return default


def _float(text: str, default: float = 0.0) -> float:
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


def _ref_name(text: str) -> str:
    t = (text or "").strip().rstrip("/")
    if not t:
        return ""
    return t.split("/")[-1]


def _all(root, name: str):
    for el in root.iter():
        if _ln(el.tag) == name:
            yield el


def parse_arxml(path: str) -> list:
    tree = ET.parse(path)
    return parse_root(tree.getroot())


def parse_root(root) -> list:
    signals = {}
    for el in _all(root, "I-SIGNAL"):
        name = _text(el, "SHORT-NAME")
        if not name:
            continue
        signals[name] = {
            "length": _num(_text(el, "LENGTH"), 1),
            "factor": _float(_text(el, "FACTOR"), 1.0) or 1.0,
            "offset": _float(_text(el, "OFFSET"), 0.0),
            "unit": _text(el, "UNIT"),
        }

    pdus = {}
    for el in _all(root, "I-SIGNAL-I-PDU"):
        name = _text(el, "SHORT-NAME")
        if not name:
            continue
        maps = []
        for mp in el.iter():
            if _ln(mp.tag) != "I-SIGNAL-TO-PDU-MAPPING":
                continue
            sig_name = _ref_name(_text(mp, "I-SIGNAL-REF")) or _text(mp, "SHORT-NAME")
            order = _text(mp, "PACKING-BYTE-ORDER")
            maps.append({
                "signal": sig_name,
                "start": _num(_text(mp, "START-POSITION"), 0),
                "endian": normalize_endian(order) if order else ENDIAN_INTEL,
            })
        pdus[name] = {"dlc": _num(_text(el, "LENGTH"), 8), "maps": maps}

    frame_pdu = {}
    for el in _all(root, "CAN-FRAME"):
        name = _text(el, "SHORT-NAME")
        pref = ""
        for mp in el.iter():
            if _ln(mp.tag) == "PDU-TO-FRAME-MAPPING":
                pref = _ref_name(_text(mp, "PDU-REF"))
                break
        if name:
            frame_pdu[name] = pref or name

    out = []
    seen = set()
    for el in _all(root, "CAN-FRAME-TRIGGERING"):
        fname = _ref_name(_text(el, "FRAME-REF"))
        pname = frame_pdu.get(fname, fname)
        if pname in seen or pname not in pdus:
            continue
        seen.add(pname)
        info = pdus[pname]
        sigs = []
        for mp in info["maps"]:
            meta = signals.get(mp["signal"], {})
            sigs.append(Signal(
                name=mp["signal"] or "Signal",
                start_bit=mp["start"],
                length=int(meta.get("length") or 1),
                endian=mp["endian"],
                factor=float(meta.get("factor") or 1.0),
                offset=float(meta.get("offset") or 0.0),
                unit=str(meta.get("unit") or ""),
            ))
        out.append(Ipdu(
            name=pname,
            can_id=_num(_text(el, "IDENTIFIER"), 0),
            dlc=int(info["dlc"] or 8),
            signals=sigs,
        ))

    if not out:
        for name, info in pdus.items():
            sigs = []
            for mp in info["maps"]:
                meta = signals.get(mp["signal"], {})
                sigs.append(Signal(
                    name=mp["signal"] or "Signal",
                    start_bit=mp["start"],
                    length=int(meta.get("length") or 1),
                    endian=mp["endian"],
                    factor=float(meta.get("factor") or 1.0),
                    offset=float(meta.get("offset") or 0.0),
                    unit=str(meta.get("unit") or ""),
                ))
            out.append(Ipdu(name=name, can_id=0, dlc=int(info["dlc"] or 8), signals=sigs))
    return out


def _packing_order(endian: str) -> str:
    if normalize_endian(endian) == ENDIAN_INTEL:
        return "MOST-SIGNIFICANT-BYTE-LAST"
    return "MOST-SIGNIFICANT-BYTE-FIRST"


def serialize_arxml(ipdus: list, package: str = "OpenBus") -> str:
    """Write a COM-centric AUTOSAR extract that parse_root can reload."""
    el_root = ET.Element("AUTOSAR")
    pkgs = ET.SubElement(el_root, "AR-PACKAGES")
    pkg = ET.SubElement(pkgs, "AR-PACKAGE")
    ET.SubElement(pkg, "SHORT-NAME").text = package
    elements = ET.SubElement(pkg, "ELEMENTS")

    seen_sigs = set()
    for pdu in ipdus:
        for sig in pdu.signals:
            if sig.name in seen_sigs:
                continue
            seen_sigs.add(sig.name)
            sig_el = ET.SubElement(elements, "I-SIGNAL")
            ET.SubElement(sig_el, "SHORT-NAME").text = sig.name
            ET.SubElement(sig_el, "LENGTH").text = str(int(sig.length))
            ET.SubElement(sig_el, "FACTOR").text = str(sig.factor)
            ET.SubElement(sig_el, "OFFSET").text = str(sig.offset)
            if sig.unit:
                ET.SubElement(sig_el, "UNIT").text = sig.unit

    for pdu in ipdus:
        pdu_el = ET.SubElement(elements, "I-SIGNAL-I-PDU")
        ET.SubElement(pdu_el, "SHORT-NAME").text = pdu.name
        ET.SubElement(pdu_el, "LENGTH").text = str(int(pdu.dlc))
        maps = ET.SubElement(pdu_el, "I-SIGNAL-TO-PDU-MAPPINGS")
        for i, sig in enumerate(pdu.signals):
            mp = ET.SubElement(maps, "I-SIGNAL-TO-PDU-MAPPING")
            ET.SubElement(mp, "SHORT-NAME").text = "%sMap%d" % (sig.name, i)
            ET.SubElement(mp, "I-SIGNAL-REF").text = "/Signals/%s" % sig.name
            ET.SubElement(mp, "START-POSITION").text = str(int(sig.start_bit))
            ET.SubElement(mp, "PACKING-BYTE-ORDER").text = _packing_order(sig.endian)

        frame_name = "%sFrame" % pdu.name
        frame = ET.SubElement(elements, "CAN-FRAME")
        ET.SubElement(frame, "SHORT-NAME").text = frame_name
        ET.SubElement(frame, "FRAME-LENGTH").text = str(int(pdu.dlc))
        maps_f = ET.SubElement(frame, "PDU-TO-FRAME-MAPPINGS")
        mp_f = ET.SubElement(maps_f, "PDU-TO-FRAME-MAPPING")
        ET.SubElement(mp_f, "PDU-REF").text = "/Pdus/%s" % pdu.name
        ET.SubElement(mp_f, "START-POSITION").text = "0"

        trig = ET.SubElement(elements, "CAN-FRAME-TRIGGERING")
        ET.SubElement(trig, "SHORT-NAME").text = "%sTrig" % pdu.name
        ET.SubElement(trig, "IDENTIFIER").text = str(int(pdu.can_id))
        ET.SubElement(trig, "FRAME-REF").text = "/Frames/%s" % frame_name

    ET.indent(el_root, space="  ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(
        el_root, encoding="unicode")


def validate_ipdus(ipdus: list) -> list:
    """Return findings: {level, message, pdu, signal}."""
    findings = []
    names = {}
    ids = {}
    for pdu in ipdus:
        if not pdu.name:
            findings.append({"level": "error", "message": "PDU has empty name",
                             "pdu": "", "signal": ""})
        if pdu.name in names:
            findings.append({"level": "error",
                             "message": "Duplicate PDU name %s" % pdu.name,
                             "pdu": pdu.name, "signal": ""})
        names[pdu.name] = True
        if pdu.can_id in ids and pdu.can_id != 0:
            findings.append({
                "level": "warn",
                "message": "CAN id 0x%X also used by %s" % (pdu.can_id, ids[pdu.can_id]),
                "pdu": pdu.name, "signal": "",
            })
        ids[pdu.can_id] = pdu.name
        if pdu.dlc < 0 or pdu.dlc > 64:
            findings.append({"level": "error", "message": "DLC out of range",
                             "pdu": pdu.name, "signal": ""})
        bits = set()
        for sig in pdu.signals:
            if sig.length <= 0:
                findings.append({"level": "error", "message": "Length <= 0",
                                 "pdu": pdu.name, "signal": sig.name})
            end = sig.start_bit + sig.length - 1
            if normalize_endian(sig.endian) == ENDIAN_INTEL:
                for b in range(sig.start_bit, sig.start_bit + sig.length):
                    if b in bits:
                        findings.append({
                            "level": "error",
                            "message": "Bit %d overlaps" % b,
                            "pdu": pdu.name, "signal": sig.name,
                        })
                    bits.add(b)
                    if b // 8 >= pdu.dlc:
                        findings.append({
                            "level": "warn",
                            "message": "Bit %d outside DLC" % b,
                            "pdu": pdu.name, "signal": sig.name,
                        })
            else:
                # Motorola span is harder; only check start within DLC.
                if sig.start_bit // 8 >= pdu.dlc:
                    findings.append({
                        "level": "warn",
                        "message": "Start bit outside DLC",
                        "pdu": pdu.name, "signal": sig.name,
                    })
            if not sig.name:
                findings.append({"level": "error", "message": "Signal name empty",
                                 "pdu": pdu.name, "signal": ""})
    if not findings:
        findings.append({"level": "info", "message": "No issues", "pdu": "", "signal": ""})
    return findings


def export_dbc(ipdus: list, bus_name: str = "AUTOSAR") -> str:
    """Handoff to DBC Studio / CANdb++ style text."""
    lines = [
        "VERSION \"\"",
        "",
        "NS_ :",
        "",
        "BS_:",
        "",
        "BU_: OpenBus",
        "",
    ]
    for pdu in ipdus:
        lines.append("BO_ %d %s: %d OpenBus" % (int(pdu.can_id), pdu.name, int(pdu.dlc)))
        for sig in pdu.signals:
            intel = normalize_endian(sig.endian) == ENDIAN_INTEL
            order = "1+" if intel else "0-"
            lines.append(
                ' SG_ %s : %d|%d@%s (%g,%g) [0|0] "%s" OpenBus' % (
                    sig.name, int(sig.start_bit), int(sig.length),
                    order, float(sig.factor), float(sig.offset), sig.unit or ""))
        lines.append("")
    return "\n".join(lines)
