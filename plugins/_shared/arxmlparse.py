# -*- coding: utf-8 -*-
"""arxmlparse — AUTOSAR CP communication ARXML (COM subset) + validate.

Shared by autosar-suite (AUTOSAR Studio). File-centric: System Description /
ECU Extract / I-SIGNAL / I-SIGNAL-I-PDU / CAN-FRAME / TRIGGERING.

Does NOT generate BSW/RTE code — ARXML is the handoff to DaVinci / tresos / …
"""

from __future__ import annotations

import copy
import os
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple


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


@dataclass
class ArxmlModel:
    """In-memory COM-centric ARXML document model."""
    ipdus: List[Ipdu] = field(default_factory=list)
    package: str = "OpenBus"
    path: str = ""
    notes: str = ""
    # Unknown AR-PACKAGE XML fragments preserved across rewrite serialize.
    foreign_packages: List[str] = field(default_factory=list)

    def clone(self) -> "ArxmlModel":
        return copy.deepcopy(self)


def normalize_endian(text: str) -> str:
    t = (text or "").strip().upper().replace("_", "-")
    if t in ("INTEL", "LITTLE", "LSB", "LITTLE-ENDIAN"):
        return ENDIAN_INTEL
    if "LAST" in t or "LITTLE" in t or t == "INTEL":
        return ENDIAN_INTEL
    if t in ("",):
        return ENDIAN_INTEL
    return ENDIAN_MOTOROLA


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


def parse_arxml_model(path: str) -> ArxmlModel:
    tree = ET.parse(path)
    root = tree.getroot()
    model = ArxmlModel(path=path)
    model.ipdus = parse_root(root)
    model.foreign_packages = _collect_foreign_packages(root)
    return model


# Packages we rewrite from the COM model — everything else is preserved.
_COM_PACKAGE_NAMES = {
    "OpenBus", "Signals", "Pdus", "Frames", "Communication",
    "ECUC", "Ecuc", "ActiveEcuC",
}


def _collect_foreign_packages(root) -> List[str]:
    """Serialize AR-PACKAGE nodes that are not part of the COM rewrite set."""
    out: List[str] = []
    for pkg in _all(root, "AR-PACKAGE"):
        name = _text(pkg, "SHORT-NAME")
        if not name or name in _COM_PACKAGE_NAMES:
            continue
        # Skip nested packages already covered by parent walk? Keep top-level
        # under AR-PACKAGES only.
        parent = None
        # ElementTree has no getparent in stdlib — keep all named packages
        # not in COM set; duplicates on nested are acceptable for handoff.
        try:
            xml = ET.tostring(pkg, encoding="unicode")
        except Exception:
            continue
        if xml and xml not in out:
            out.append(xml)
    return out


def parse_arxml(path: str) -> List[Ipdu]:
    tree = ET.parse(path)
    return parse_root(tree.getroot())


def parse_root(root) -> List[Ipdu]:
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

    out: List[Ipdu] = []
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
            out.append(Ipdu(
                name=name, can_id=0, dlc=int(info["dlc"] or 8), signals=sigs))
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


def serialize_model(model: ArxmlModel) -> str:
    """Serialize COM extract and re-attach preserved foreign AR-PACKAGE XML."""
    body = serialize_arxml(model.ipdus, package=model.package or "OpenBus")
    foreign = list(getattr(model, "foreign_packages", None) or [])
    if not foreign:
        return body
    try:
        root = ET.fromstring(body)
    except ET.ParseError:
        return body
    pkgs = None
    for el in root:
        if _ln(el.tag) == "AR-PACKAGES":
            pkgs = el
            break
    if pkgs is None:
        pkgs = ET.SubElement(root, "AR-PACKAGES")
    for frag in foreign:
        try:
            node = ET.fromstring(frag)
        except ET.ParseError:
            continue
        pkgs.append(node)
    ET.indent(root, space="  ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(
        root, encoding="unicode")


def validate_ipdus(ipdus: list, deep: bool = True) -> List[dict]:
    """Findings: level, severity, rule, message, pdu, signal, fix (optional)."""
    findings: List[dict] = []

    def add(level, rule, message, pdu="", signal="", fix=""):
        sev = {"error": "error", "warn": "warning", "warning": "warning",
               "info": "info"}.get(level, "info")
        findings.append({
            "level": level if level != "warn" else "warn",
            "severity": sev,
            "rule": rule,
            "message": message,
            "pdu": pdu,
            "signal": signal,
            "fix": fix,
            "location": ("%s/%s" % (pdu, signal)).strip("/"),
        })

    names: Dict[str, bool] = {}
    ids: Dict[int, str] = {}
    for pdu in ipdus:
        if not pdu.name:
            add("error", "pdu_name", "PDU has empty SHORT-NAME",
                fix="Set a unique I-SIGNAL-I-PDU SHORT-NAME")
        if pdu.name in names:
            add("error", "pdu_dup", "Duplicate PDU name %s" % pdu.name,
                pdu=pdu.name, fix="Rename one of the colliding PDUs")
        names[pdu.name] = True
        if pdu.can_id in ids and pdu.can_id != 0:
            add("warn", "can_id_dup",
                "CAN id 0x%X also used by %s" % (pdu.can_id, ids[pdu.can_id]),
                pdu=pdu.name,
                fix="Assign a unique IDENTIFIER on CAN-FRAME-TRIGGERING")
        ids[pdu.can_id] = pdu.name
        if pdu.dlc < 0 or pdu.dlc > 64:
            add("error", "dlc_range", "DLC out of range (0–64)",
                pdu=pdu.name, fix="Set I-SIGNAL-I-PDU LENGTH to a valid byte count")
        if deep and pdu.can_id == 0:
            add("warn", "can_id_zero",
                "CAN IDENTIFIER is 0 — often incomplete extract",
                pdu=pdu.name, fix="Set CAN-FRAME-TRIGGERING IDENTIFIER")
        bits = set()
        for sig in pdu.signals:
            if not sig.name:
                add("error", "sig_name", "Signal name empty", pdu=pdu.name,
                    fix="Set I-SIGNAL SHORT-NAME")
            if sig.length <= 0:
                add("error", "sig_length", "LENGTH <= 0",
                    pdu=pdu.name, signal=sig.name,
                    fix="Set I-SIGNAL LENGTH to bits > 0")
            if deep and sig.length > 64:
                add("warn", "sig_length_wide",
                    "Signal length %d bits is unusual for COM" % sig.length,
                    pdu=pdu.name, signal=sig.name)
            if normalize_endian(sig.endian) == ENDIAN_INTEL:
                for b in range(sig.start_bit, sig.start_bit + max(0, sig.length)):
                    if b in bits:
                        add("error", "bit_overlap", "Bit %d overlaps" % b,
                            pdu=pdu.name, signal=sig.name,
                            fix="Adjust START-POSITION or LENGTH to remove overlap")
                    bits.add(b)
                    if b // 8 >= pdu.dlc:
                        add("warn", "bit_outside_dlc",
                            "Bit %d outside DLC" % b,
                            pdu=pdu.name, signal=sig.name,
                            fix="Increase I-SIGNAL-I-PDU LENGTH or move signal")
            else:
                if sig.start_bit // 8 >= pdu.dlc:
                    add("warn", "start_outside_dlc", "Start bit outside DLC",
                        pdu=pdu.name, signal=sig.name,
                        fix="Increase DLC or lower START-POSITION")
        if deep and pdu.signals:
            used = max(
                (s.start_bit + max(0, s.length) for s in pdu.signals
                 if normalize_endian(s.endian) == ENDIAN_INTEL),
                default=0)
            need_bytes = (used + 7) // 8
            if need_bytes > pdu.dlc:
                add("warn", "dlc_short",
                    "DLC %d < required ~%d bytes for mapped bits" % (
                        pdu.dlc, need_bytes),
                    pdu=pdu.name,
                    fix="Set LENGTH to at least %d" % need_bytes)

    if not findings:
        add("info", "ok", "No issues")
    return findings


def validate_model(model: ArxmlModel, deep: bool = True) -> List[dict]:
    return validate_ipdus(model.ipdus, deep=deep)


def export_dbc(ipdus: list, bus_name: str = "AUTOSAR") -> str:
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
        lines.append("BO_ %d %s: %d OpenBus" % (
            int(pdu.can_id), pdu.name, int(pdu.dlc)))
        for sig in pdu.signals:
            intel = normalize_endian(sig.endian) == ENDIAN_INTEL
            order = "1+" if intel else "0-"
            lines.append(
                ' SG_ %s : %d|%d@%s (%g,%g) [0|0] "%s" OpenBus' % (
                    sig.name, int(sig.start_bit), int(sig.length),
                    order, float(sig.factor), float(sig.offset), sig.unit or ""))
        lines.append("")
    return "\n".join(lines)


def empty_model() -> ArxmlModel:
    model = ArxmlModel(package="OpenBus")
    model.ipdus = [
        Ipdu(
            name="DemoPdu",
            can_id=0x100,
            dlc=8,
            signals=[
                Signal("DemoSignal", 0, 16, ENDIAN_INTEL, 1.0, 0.0, ""),
            ],
        ),
    ]
    model.notes = "Starter COM extract — edit SHORT-NAME / mapping, then Validate"
    return model


def diff_models(a: ArxmlModel, b: ArxmlModel) -> List[dict]:
    ka = {p.name: p for p in a.ipdus}
    kb = {p.name: p for p in b.ipdus}
    rows = []
    for name in sorted(set(ka) | set(kb)):
        pa, pb = ka.get(name), kb.get(name)
        if pa and not pb:
            rows.append({
                "kind": "removed", "pdu": name, "signal": "",
                "detail": "only in A",
            })
        elif pb and not pa:
            rows.append({
                "kind": "added", "pdu": name, "signal": "",
                "detail": "only in B",
            })
        else:
            if pa.can_id != pb.can_id or pa.dlc != pb.dlc:
                rows.append({
                    "kind": "changed", "pdu": name, "signal": "",
                    "detail": "id/dlc %s/%s → %s/%s" % (
                        pa.can_id, pa.dlc, pb.can_id, pb.dlc),
                })
            sa = {s.name: s for s in pa.signals}
            sb = {s.name: s for s in pb.signals}
            for sn in sorted(set(sa) | set(sb)):
                xa, xb = sa.get(sn), sb.get(sn)
                if xa and not xb:
                    rows.append({
                        "kind": "removed", "pdu": name, "signal": sn,
                        "detail": "signal only in A",
                    })
                elif xb and not xa:
                    rows.append({
                        "kind": "added", "pdu": name, "signal": sn,
                        "detail": "signal only in B",
                    })
                elif (xa.start_bit, xa.length, xa.endian) != (
                        xb.start_bit, xb.length, xb.endian):
                    rows.append({
                        "kind": "changed", "pdu": name, "signal": sn,
                        "detail": "layout %d|%d@%s → %d|%d@%s" % (
                            xa.start_bit, xa.length, xa.endian,
                            xb.start_bit, xb.length, xb.endian),
                    })
    return rows


# Spec glossary — parameter encyclopedia (tresos-style tips)
SPEC_GLOSSARY: List[dict] = [
    {
        "id": "i-signal.short-name",
        "title": "I-SIGNAL / SHORT-NAME",
        "category": "Communication",
        "summary": "Unique name of a system signal in the ARXML package.",
        "detail":
            "AUTOSAR SHORT-NAME must be unique within its package. "
            "Used by I-SIGNAL-REF in PDU mappings. Prefer stable OEM naming.",
        "range": "1–128 chars, AUTOSAR identifier rules",
    },
    {
        "id": "i-signal.length",
        "title": "I-SIGNAL / LENGTH",
        "category": "Communication",
        "summary": "Signal length in bits.",
        "detail":
            "Defines how many bits are packed into the I-PDU. "
            "Must fit inside the PDU LENGTH (DLC) after mapping.",
        "range": "1–64 typical for COM signals",
    },
    {
        "id": "i-signal-i-pdu.length",
        "title": "I-SIGNAL-I-PDU / LENGTH",
        "category": "Communication",
        "summary": "PDU payload length in bytes (DLC).",
        "detail":
            "Classic CAN: 0–8. CAN FD: up to 64. Must cover all mapped bits.",
        "range": "0–64",
    },
    {
        "id": "mapping.start-position",
        "title": "I-SIGNAL-TO-PDU-MAPPING / START-POSITION",
        "category": "Communication",
        "summary": "Bit position of the signal inside the PDU.",
        "detail":
            "With MOST-SIGNIFICANT-BYTE-LAST (intel), start is the LSB. "
            "With MOST-SIGNIFICANT-BYTE-FIRST (motorola), start is the MSB.",
        "range": "0 .. (DLC*8 - 1)",
    },
    {
        "id": "mapping.packing-byte-order",
        "title": "PACKING-BYTE-ORDER",
        "category": "Communication",
        "summary": "Byte order used when packing the signal into the PDU.",
        "detail":
            "MOST-SIGNIFICANT-BYTE-LAST ≈ little-endian/intel. "
            "MOST-SIGNIFICANT-BYTE-FIRST ≈ big-endian/motorola.",
        "range": "MOST-SIGNIFICANT-BYTE-LAST | MOST-SIGNIFICANT-BYTE-FIRST",
    },
    {
        "id": "can-frame-triggering.identifier",
        "title": "CAN-FRAME-TRIGGERING / IDENTIFIER",
        "category": "Communication",
        "summary": "CAN identifier (11-bit or 29-bit raw value).",
        "detail":
            "Binds a frame to the bus. Must be unique per cluster for Tx frames.",
        "range": "0x000–0x7FF (std) or 0x00000000–0x1FFFFFFF (ext)",
    },
    {
        "id": "can-frame.frame-length",
        "title": "CAN-FRAME / FRAME-LENGTH",
        "category": "Communication",
        "summary": "Frame length in bytes on the bus.",
        "detail": "Usually matches the mapped I-PDU LENGTH.",
        "range": "0–64",
    },
    {
        "id": "ar-package.short-name",
        "title": "AR-PACKAGE / SHORT-NAME",
        "category": "Structure",
        "summary": "Top-level package name in the AUTOSAR XML.",
        "detail":
            "Organizes ELEMENTS. Paths like /Package/Element use this name.",
        "range": "AUTOSAR identifier",
    },
]


# BSWMD-lite tips (tresos-style) for COM stack ECUC params
BSWMD_LITE: List[dict] = [
    {
        "id": "ecuc.com.ComIPduSize",
        "title": "Com / ComIPduSize",
        "category": "ECUC Com",
        "summary": "I-PDU payload length in bytes (matches COM DLC).",
        "detail":
            "Derived from I-SIGNAL-I-PDU LENGTH. Increase when signals "
            "overflow the PDU; keep in sync with CanIfTxPduDlc.",
        "range": "0–64",
    },
    {
        "id": "ecuc.com.ComBitPosition",
        "title": "Com / ComBitPosition",
        "category": "ECUC Com",
        "summary": "Start bit of the COM signal inside the I-PDU.",
        "detail": "Mirrors I-SIGNAL-TO-PDU-MAPPING START-POSITION.",
        "range": "0 .. (size*8 - 1)",
    },
    {
        "id": "ecuc.com.ComBitSize",
        "title": "Com / ComBitSize",
        "category": "ECUC Com",
        "summary": "Signal length in bits.",
        "detail": "Mirrors I-SIGNAL LENGTH. Must not overlap neighbors.",
        "range": "1–64 typical",
    },
    {
        "id": "ecuc.com.ComSignalEndianness",
        "title": "Com / ComSignalEndianness",
        "category": "ECUC Com",
        "summary": "LITTLE_ENDIAN or BIG_ENDIAN packing.",
        "detail": "Maps from PACKING-BYTE-ORDER on the COM extract.",
        "range": "LITTLE_ENDIAN | BIG_ENDIAN",
    },
    {
        "id": "ecuc.canif.CanIfTxPduCanId",
        "title": "CanIf / CanIfTxPduCanId",
        "category": "ECUC CanIf",
        "summary": "CAN identifier for the Tx PDU.",
        "detail":
            "From CAN-FRAME-TRIGGERING IDENTIFIER. Must be unique per cluster.",
        "range": "0x000–0x1FFFFFFF",
    },
    {
        "id": "ecuc.canif.CanIfTxPduRef",
        "title": "CanIf / CanIfTxPduRef",
        "category": "ECUC CanIf",
        "summary": "Reference to the COM / PduR PDU short name.",
        "detail": "Dangling refs fail Validate (canif_dangling).",
        "range": "existing COM PDU SHORT-NAME",
    },
    {
        "id": "ecuc.pdur.PduRSrcPduRef",
        "title": "PduR / PduRSrcPduRef",
        "category": "ECUC PduR",
        "summary": "Routing source PDU reference.",
        "detail": "Stub route Com→CanIf for each I-PDU in the extract.",
        "range": "COM PDU name",
    },
    {
        "id": "ecuc.cannm.CanNmMainFunctionPeriod",
        "title": "CanNm / CanNmMainFunctionPeriod",
        "category": "ECUC CanNm",
        "summary": "NM main function period in milliseconds.",
        "detail": "Starter stub — tune for your cluster timing.",
        "range": "1–100 typical",
    },
]


def search_spec(query: str) -> List[dict]:
    q = (query or "").strip().lower()
    pool = list(SPEC_GLOSSARY) + list(BSWMD_LITE)
    if not q:
        return pool
    out = []
    for row in pool:
        blob = " ".join([
            row.get("id", ""), row.get("title", ""),
            row.get("summary", ""), row.get("detail", ""),
            row.get("category", ""),
        ]).lower()
        if q in blob:
            out.append(row)
    return out


def tip_for_field(field_id: str) -> Optional[dict]:
    for row in SPEC_GLOSSARY:
        if row["id"] == field_id:
            return row
    for row in BSWMD_LITE:
        if row["id"] == field_id:
            return row
    return None


# ---------------------------------------------------------------------------
# ECUC-lite (Com / CanIf / PduR / CanNm) — engineering intermediate, not codegen
# ---------------------------------------------------------------------------


@dataclass
class EcucParam:
    name: str
    value: str = ""
    definition: str = ""
    kind: str = "string"  # numerical|string|boolean|enumeration|reference

    def to_dict(self) -> dict:
        return {
            "name": self.name, "value": self.value,
            "definition": self.definition, "kind": self.kind,
        }

    @staticmethod
    def from_dict(d: dict) -> "EcucParam":
        return EcucParam(
            name=str(d.get("name") or "Param"),
            value=str(d.get("value") or ""),
            definition=str(d.get("definition") or ""),
            kind=str(d.get("kind") or "string"),
        )


@dataclass
class EcucContainer:
    name: str
    definition: str = ""
    params: List[EcucParam] = field(default_factory=list)
    children: List["EcucContainer"] = field(default_factory=list)
    link_pdu: str = ""
    link_signal: str = ""

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "definition": self.definition,
            "params": [p.to_dict() for p in self.params],
            "children": [c.to_dict() for c in self.children],
            "link_pdu": self.link_pdu,
            "link_signal": self.link_signal,
        }

    @staticmethod
    def from_dict(d: dict) -> "EcucContainer":
        return EcucContainer(
            name=str(d.get("name") or "Container"),
            definition=str(d.get("definition") or ""),
            params=[EcucParam.from_dict(p) for p in (d.get("params") or [])],
            children=[
                EcucContainer.from_dict(c) for c in (d.get("children") or [])],
            link_pdu=str(d.get("link_pdu") or ""),
            link_signal=str(d.get("link_signal") or ""),
        )


@dataclass
class EcucModule:
    name: str
    definition: str = ""
    containers: List[EcucContainer] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "definition": self.definition,
            "containers": [c.to_dict() for c in self.containers],
        }

    @staticmethod
    def from_dict(d: dict) -> "EcucModule":
        return EcucModule(
            name=str(d.get("name") or "Module"),
            definition=str(d.get("definition") or ""),
            containers=[
                EcucContainer.from_dict(c) for c in (d.get("containers") or [])],
        )


@dataclass
class EcucModel:
    """Lightweight ECUC for COM stack intermediates."""
    modules: List[EcucModule] = field(default_factory=list)
    package: str = "Ecuc"
    path: str = ""
    derived_from_com: bool = False

    def clone(self) -> "EcucModel":
        return copy.deepcopy(self)

    def to_dict(self) -> dict:
        return {
            "package": self.package,
            "path": self.path,
            "derived_from_com": self.derived_from_com,
            "modules": [m.to_dict() for m in self.modules],
        }

    @staticmethod
    def from_dict(d: dict) -> "EcucModel":
        return EcucModel(
            package=str(d.get("package") or "Ecuc"),
            path=str(d.get("path") or ""),
            derived_from_com=bool(d.get("derived_from_com")),
            modules=[EcucModule.from_dict(m) for m in (d.get("modules") or [])],
        )


def empty_ecuc() -> EcucModel:
    return EcucModel(package="Ecuc", modules=[])


def derive_ecuc_from_com(
        model: ArxmlModel, ecu_name: str = "Ecu") -> EcucModel:
    """Build Com / CanIf / PduR / CanNm stubs from COM I-PDUs."""
    com_children: List[EcucContainer] = []
    canif_children: List[EcucContainer] = []
    pdur_children: List[EcucContainer] = []

    for pdu in model.ipdus:
        sig_children = []
        for sig in pdu.signals:
            endian_v = (
                "LITTLE_ENDIAN"
                if normalize_endian(sig.endian) == ENDIAN_INTEL
                else "BIG_ENDIAN")
            sig_children.append(EcucContainer(
                name=sig.name,
                definition="/AUTOSAR/EcucDefs/Com/ComConfig/ComSignal",
                link_pdu=pdu.name,
                link_signal=sig.name,
                params=[
                    EcucParam(
                        "ComBitPosition", str(sig.start_bit),
                        "/AUTOSAR/EcucDefs/Com/ComConfig/ComSignal/"
                        "ComBitPosition"),
                    EcucParam(
                        "ComBitSize", str(sig.length),
                        "/AUTOSAR/EcucDefs/Com/ComConfig/ComSignal/ComBitSize"),
                    EcucParam(
                        "ComSignalEndianness", endian_v,
                        "/AUTOSAR/EcucDefs/Com/ComConfig/ComSignal/"
                        "ComSignalEndianness"),
                ],
            ))
        com_children.append(EcucContainer(
            name=pdu.name,
            definition="/AUTOSAR/EcucDefs/Com/ComConfig/ComIPdu",
            link_pdu=pdu.name,
            params=[
                EcucParam(
                    "ComIPduDirection", "SEND",
                    "/AUTOSAR/EcucDefs/Com/ComConfig/ComIPdu/ComIPduDirection"),
                EcucParam(
                    "ComIPduSize", str(pdu.dlc),
                    "/AUTOSAR/EcucDefs/Com/ComConfig/ComIPdu/ComIPduSize"),
            ],
            children=sig_children,
        ))
        canif_children.append(EcucContainer(
            name="%sHth" % pdu.name,
            definition="/AUTOSAR/EcucDefs/CanIf/CanIfInitCfg/CanIfTxPduCfg",
            link_pdu=pdu.name,
            params=[
                EcucParam(
                    "CanIfTxPduCanId", "0x%X" % int(pdu.can_id),
                    "/AUTOSAR/EcucDefs/CanIf/CanIfInitCfg/CanIfTxPduCfg/"
                    "CanIfTxPduCanId"),
                EcucParam(
                    "CanIfTxPduDlc", str(pdu.dlc),
                    "/AUTOSAR/EcucDefs/CanIf/CanIfInitCfg/CanIfTxPduCfg/"
                    "CanIfTxPduDlc"),
                EcucParam(
                    "CanIfTxPduRef", pdu.name,
                    "/AUTOSAR/EcucDefs/CanIf/CanIfInitCfg/CanIfTxPduCfg/"
                    "CanIfTxPduRef"),
            ],
        ))
        pdur_children.append(EcucContainer(
            name="%sRoute" % pdu.name,
            definition=(
                "/AUTOSAR/EcucDefs/PduR/PduRRoutingTables/PduRRoutingTable"),
            link_pdu=pdu.name,
            params=[
                EcucParam(
                    "PduRSrcPduRef", pdu.name,
                    "/AUTOSAR/EcucDefs/PduR/PduRRoutingTables/PduRSrcPduRef"),
                EcucParam(
                    "PduRDestPduRef", pdu.name,
                    "/AUTOSAR/EcucDefs/PduR/PduRRoutingTables/PduRDestPduRef"),
            ],
        ))

    modules = [
        EcucModule(
            name="Com",
            definition="/AUTOSAR/EcucDefs/Com",
            containers=[EcucContainer(
                name="ComConfig",
                definition="/AUTOSAR/EcucDefs/Com/ComConfig",
                children=com_children,
            )],
        ),
        EcucModule(
            name="CanIf",
            definition="/AUTOSAR/EcucDefs/CanIf",
            containers=[EcucContainer(
                name="CanIfInitCfg",
                definition="/AUTOSAR/EcucDefs/CanIf/CanIfInitCfg",
                children=canif_children,
            )],
        ),
        EcucModule(
            name="PduR",
            definition="/AUTOSAR/EcucDefs/PduR",
            containers=[EcucContainer(
                name="PduRRoutingTables",
                definition="/AUTOSAR/EcucDefs/PduR/PduRRoutingTables",
                children=pdur_children,
            )],
        ),
        EcucModule(
            name="CanNm",
            definition="/AUTOSAR/EcucDefs/CanNm",
            containers=[EcucContainer(
                name="CanNmGlobalConfig",
                definition="/AUTOSAR/EcucDefs/CanNm/CanNmGlobalConfig",
                params=[
                    EcucParam(
                        "CanNmMainFunctionPeriod", "10",
                        "/AUTOSAR/EcucDefs/CanNm/CanNmGlobalConfig/"
                        "CanNmMainFunctionPeriod"),
                    EcucParam(
                        "CanNmEcuName", ecu_name,
                        "/AUTOSAR/EcucDefs/CanNm/CanNmGlobalConfig/"
                        "CanNmEcuName"),
                ],
            )],
        ),
    ]
    return EcucModel(
        modules=modules, package="Ecuc", derived_from_com=True)


def serialize_ecuc_lite(ecuc: EcucModel) -> str:
    """Write a readable ECUC-lite ARXML intermediate (not vendor GenData)."""
    el_root = ET.Element("AUTOSAR")
    pkgs = ET.SubElement(el_root, "AR-PACKAGES")
    pkg = ET.SubElement(pkgs, "AR-PACKAGE")
    ET.SubElement(pkg, "SHORT-NAME").text = ecuc.package or "Ecuc"
    elements = ET.SubElement(pkg, "ELEMENTS")
    conf = ET.SubElement(elements, "ECUC-MODULE-CONFIGURATION-VALUES")
    ET.SubElement(conf, "SHORT-NAME").text = "ActiveEcuC"
    mods = ET.SubElement(conf, "CONTAINERS")

    def write_container(parent, cont: EcucContainer):
        cel = ET.SubElement(parent, "ECUC-CONTAINER-VALUE")
        ET.SubElement(cel, "SHORT-NAME").text = cont.name
        if cont.definition:
            ET.SubElement(cel, "DEFINITION-REF").text = cont.definition
        if cont.link_pdu:
            ET.SubElement(cel, "LINK-PDU").text = cont.link_pdu
        if cont.link_signal:
            ET.SubElement(cel, "LINK-SIGNAL").text = cont.link_signal
        if cont.params:
            vals = ET.SubElement(cel, "PARAMETER-VALUES")
            for p in cont.params:
                kind = (p.kind or "string").lower()
                tag = {
                    "numerical": "ECUC-NUMERICAL-PARAM-VALUE",
                    "boolean": "ECUC-BOOLEAN-PARAM-VALUE",
                    "enumeration": "ECUC-ENUMERATION-PARAM-VALUE",
                    "reference": "ECUC-REFERENCE-VALUE",
                    "string": "ECUC-TEXTUAL-PARAM-VALUE",
                    "textual": "ECUC-TEXTUAL-PARAM-VALUE",
                }.get(kind, "ECUC-TEXTUAL-PARAM-VALUE")
                pel = ET.SubElement(vals, tag)
                ET.SubElement(pel, "SHORT-NAME").text = p.name
                if p.definition:
                    ET.SubElement(pel, "DEFINITION-REF").text = p.definition
                if kind == "reference":
                    ET.SubElement(pel, "VALUE-REF").text = p.value
                else:
                    ET.SubElement(pel, "VALUE").text = p.value
        if cont.children:
            sub = ET.SubElement(cel, "SUB-CONTAINERS")
            for ch in cont.children:
                write_container(sub, ch)

    for mod in ecuc.modules:
        mel = ET.SubElement(mods, "ECUC-CONTAINER-VALUE")
        ET.SubElement(mel, "SHORT-NAME").text = mod.name
        if mod.definition:
            ET.SubElement(mel, "DEFINITION-REF").text = mod.definition
        if mod.containers:
            sub = ET.SubElement(mel, "SUB-CONTAINERS")
            for c in mod.containers:
                write_container(sub, c)

    ET.indent(el_root, space="  ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(
        el_root, encoding="unicode")


def parse_ecuc_lite(path: str) -> EcucModel:
    tree = ET.parse(path)
    root = tree.getroot()
    modules: List[EcucModule] = []

    def parse_container(el) -> EcucContainer:
        params = []
        for vals in list(el):
            if _ln(vals.tag) != "PARAMETER-VALUES":
                continue
            for pel in list(vals):
                tag = _ln(pel.tag)
                kind_map = {
                    "ECUC-NUMERICAL-PARAM-VALUE": "numerical",
                    "ECUC-BOOLEAN-PARAM-VALUE": "boolean",
                    "ECUC-ENUMERATION-PARAM-VALUE": "enumeration",
                    "ECUC-REFERENCE-VALUE": "reference",
                    "ECUC-TEXTUAL-PARAM-VALUE": "string",
                }
                if tag not in kind_map:
                    continue
                kind = kind_map[tag]
                val = _text(pel, "VALUE-REF") if kind == "reference" else _text(
                    pel, "VALUE")
                params.append(EcucParam(
                    name=_text(pel, "SHORT-NAME"),
                    value=val,
                    definition=_text(pel, "DEFINITION-REF"),
                    kind=kind,
                ))
        children = []
        for sub in list(el):
            if _ln(sub.tag) != "SUB-CONTAINERS":
                continue
            for ch in list(sub):
                if _ln(ch.tag) == "ECUC-CONTAINER-VALUE":
                    children.append(parse_container(ch))
        return EcucContainer(
            name=_text(el, "SHORT-NAME"),
            definition=_text(el, "DEFINITION-REF"),
            params=params,
            children=children,
            link_pdu=_text(el, "LINK-PDU"),
            link_signal=_text(el, "LINK-SIGNAL"),
        )

    for conf in _all(root, "ECUC-MODULE-CONFIGURATION-VALUES"):
        for conts in list(conf):
            if _ln(conts.tag) != "CONTAINERS":
                continue
            for mel in list(conts):
                if _ln(mel.tag) != "ECUC-CONTAINER-VALUE":
                    continue
                mname = _text(mel, "SHORT-NAME")
                mdef = _text(mel, "DEFINITION-REF")
                mchildren = []
                for sub in list(mel):
                    if _ln(sub.tag) != "SUB-CONTAINERS":
                        continue
                    for ch in list(sub):
                        if _ln(ch.tag) == "ECUC-CONTAINER-VALUE":
                            mchildren.append(parse_container(ch))
                modules.append(EcucModule(
                    name=mname, definition=mdef, containers=mchildren))

    return EcucModel(modules=modules, path=path)


def _ecuc_link_pdus(ecuc: EcucModel) -> set:
    found = set()

    def walk(c: EcucContainer):
        if c.link_pdu:
            found.add(c.link_pdu)
        for ch in c.children:
            walk(ch)

    for m in ecuc.modules:
        for c in m.containers:
            walk(c)
    return found


def _ecuc_canif_refs(ecuc: EcucModel) -> List[Tuple[str, str]]:
    refs = []

    def walk(c: EcucContainer):
        for p in c.params:
            if p.name == "CanIfTxPduRef":
                refs.append((c.name, p.value))
        for ch in c.children:
            walk(ch)

    for m in ecuc.modules:
        if m.name != "CanIf":
            continue
        for c in m.containers:
            walk(c)
    return refs


def validate_project(
        com: ArxmlModel, ecuc: Optional[EcucModel] = None,
        roles: Optional[dict] = None) -> List[dict]:
    """COM checks plus cross-artifact ECUC consistency."""
    findings = [
        f for f in validate_model(com, deep=True) if f.get("rule") != "ok"]
    roles = roles or {}

    def add(level, rule, message, pdu="", signal="", fix="", artifact="com"):
        sev = {"error": "error", "warn": "warning", "warning": "warning",
               "info": "info"}.get(level, "info")
        findings.append({
            "level": level if level != "warn" else "warn",
            "severity": sev,
            "rule": rule,
            "message": message,
            "pdu": pdu,
            "signal": signal,
            "fix": fix,
            "artifact": artifact,
            "location": ("%s/%s" % (pdu, signal)).strip("/") or artifact,
        })

    if roles.get("com") and not roles.get("com_exists", True):
        add("warn", "role_missing", "COM role file missing",
            fix="Write intermediates or assign work/com.arxml",
            artifact="project")

    if ecuc is None or not ecuc.modules:
        add("warn", "ecuc_empty",
            "ECUC-lite not derived — run Derive from COM on Project page",
            fix="Project → Derive ECUC", artifact="ecuc")
    else:
        com_names = {p.name for p in com.ipdus}
        linked = _ecuc_link_pdus(ecuc)
        for name in sorted(com_names - linked):
            add("error", "ecuc_missing_pdu",
                "COM PDU %s has no ECUC link" % name,
                pdu=name, fix="Re-derive ECUC from COM", artifact="ecuc")
        for name in sorted(linked - com_names):
            add("warn", "ecuc_orphan_pdu",
                "ECUC links PDU %s not in COM" % name,
                pdu=name,
                fix="Remove orphan container or restore COM PDU",
                artifact="ecuc")
        for cname, ref in _ecuc_canif_refs(ecuc):
            if ref and ref not in com_names:
                add("error", "canif_dangling",
                    "CanIf %s refs missing PDU %s" % (cname, ref),
                    pdu=ref, fix="Fix CanIfTxPduRef or re-derive",
                    artifact="ecuc")

        names_seen: Dict[str, str] = {}

        def walk_names(c: EcucContainer, mod: str):
            key = c.name
            if key in names_seen and names_seen[key] != mod:
                add("warn", "ecuc_name_clash",
                    "SHORT-NAME %s in %s and %s" % (
                        key, names_seen[key], mod),
                    fix="Rename one container", artifact="ecuc")
            else:
                names_seen[key] = mod
            for ch in c.children:
                walk_names(ch, mod)

        for m in ecuc.modules:
            for c in m.containers:
                walk_names(c, m.name)

    if not findings:
        add("info", "ok", "No issues", artifact="project")
    return findings


def findings_to_sarif(
        findings: List[dict], tool: str = "ARXML Studio") -> dict:
    results = []
    for f in findings:
        sev = f.get("severity") or "note"
        level = {"error": "error", "warning": "warning",
                 "warn": "warning"}.get(sev, "note")
        results.append({
            "ruleId": f.get("rule") or "arxml",
            "level": level,
            "message": {"text": "%s — %s" % (
                f.get("message", ""), f.get("fix", ""))},
            "properties": {
                "artifact": f.get("artifact", ""),
                "location": f.get("location", ""),
                "acked": bool(f.get("acked")),
                "ackKey": f.get("ack_key") or "",
            },
        })
    return {
        "version": "2.1.0",
        "runs": [{
            "tool": {"driver": {"name": tool, "version": "1.0.0"}},
            "results": results,
        }],
    }


# ---------------------------------------------------------------------------
# Phase 4 — BSWMD-lite import, fix recipes, SWC/port mapping lite
# ---------------------------------------------------------------------------

# Extra tips loaded at runtime via load_bswmd_lite / merge_bswmd_tips
_BSWMD_EXTRA: List[dict] = []


def merge_bswmd_tips(rows: List[dict]) -> int:
    """Merge imported BSWMD tip rows into the runtime Spec pool."""
    global _BSWMD_EXTRA
    by_id = {r.get("id"): r for r in _BSWMD_EXTRA if r.get("id")}
    added = 0
    for row in rows:
        rid = row.get("id")
        if not rid:
            continue
        if rid not in by_id:
            added += 1
        by_id[rid] = row
    _BSWMD_EXTRA = list(by_id.values())
    return added


def clear_bswmd_extra() -> None:
    global _BSWMD_EXTRA
    _BSWMD_EXTRA = []


def bswmd_extra_tips() -> List[dict]:
    return list(_BSWMD_EXTRA)


def load_bswmd_lite(path: str) -> List[dict]:
    """Load a BSWMD-lite JSON file into tip rows.

    Expected shape::
        {"modules": [{"name": "Com", "params": [
            {"name": "ComIPduSize", "summary": "...", "detail": "...",
             "range": "...", "definition": "/AUTOSAR/..."}]}]}

    Also accepts EcucDefs-shaped packs with ``containers[].params[]``
    (same as ``ecuc_schemas/*.json``) — those are converted to tips and
    optionally imported as schema structure via
    ``arxml_ecuc_schema.import_bswmd_schema``.
    """
    import json
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    rows = []
    # EcucDefs-shaped single module file
    if data.get("module") and data.get("containers"):
        mname = str(data.get("module") or "Module")
        for c in data.get("containers") or []:
            cname = str(c.get("shortName") or c.get("name") or "Container")
            rows.append({
                "id": "ecuc.%s.%s" % (mname.lower(), cname),
                "title": "%s / %s" % (mname, cname),
                "category": "BSWMD %s" % mname,
                "summary": str(c.get("summary") or cname),
                "detail": str(c.get("detail") or ""),
                "range": "container",
                "definition": str(c.get("definition") or ""),
            })
            for p in c.get("params") or []:
                pname = str(p.get("shortName") or p.get("name") or "Param")
                rows.append({
                    "id": "ecuc.%s.%s" % (mname.lower(), pname),
                    "title": "%s / %s" % (mname, pname),
                    "category": "BSWMD %s" % mname,
                    "summary": str(p.get("summary") or pname),
                    "detail": str(p.get("detail") or ""),
                    "range": str(p.get("range") or ""),
                    "definition": str(p.get("definition") or ""),
                })
        merge_bswmd_tips(rows)
        return rows
    for mod in data.get("modules") or []:
        mname = str(mod.get("name") or "Module")
        for p in mod.get("params") or []:
            pname = str(p.get("name") or "Param")
            rid = str(p.get("id") or ("ecuc.%s.%s" % (mname.lower(), pname)))
            rows.append({
                "id": rid,
                "title": "%s / %s" % (mname, pname),
                "category": "BSWMD %s" % mname,
                "summary": str(p.get("summary") or pname),
                "detail": str(p.get("detail") or p.get("description") or ""),
                "range": str(p.get("range") or ""),
                "definition": str(p.get("definition") or ""),
            })
        for c in mod.get("containers") or []:
            cname = str(c.get("name") or "Container")
            rid = str(c.get("id") or ("ecuc.%s.%s" % (mname.lower(), cname)))
            rows.append({
                "id": rid,
                "title": "%s / %s" % (mname, cname),
                "category": "BSWMD %s" % mname,
                "summary": str(c.get("summary") or cname),
                "detail": str(c.get("detail") or ""),
                "range": str(c.get("range") or "container"),
                "definition": str(c.get("definition") or ""),
            })
            # Nested params under container (structure import shape)
            for p in c.get("params") or []:
                pname = str(p.get("name") or p.get("shortName") or "Param")
                rows.append({
                    "id": "ecuc.%s.%s" % (mname.lower(), pname),
                    "title": "%s / %s" % (mname, pname),
                    "category": "BSWMD %s" % mname,
                    "summary": str(p.get("summary") or pname),
                    "detail": str(p.get("detail") or ""),
                    "range": str(p.get("range") or ""),
                    "definition": str(p.get("definition") or ""),
                })
    merge_bswmd_tips(rows)
    return rows


def default_bswmd_pack() -> dict:
    """Built-in Com/CanIf/PduR/CanNm BSWMD-lite pack (no vendor file needed)."""
    return {
        "modules": [
            {
                "name": "Com",
                "params": [
                    {
                        "name": "ComIPduSize",
                        "summary": "I-PDU size in bytes",
                        "detail": "Must cover all mapped signal bits.",
                        "range": "0-64",
                        "definition":
                            "/AUTOSAR/EcucDefs/Com/ComConfig/ComIPdu/ComIPduSize",
                    },
                    {
                        "name": "ComGwIPdu",
                        "summary": "Gateway I-PDU flag",
                        "detail": "True when PDU is routed via PduR gateway.",
                        "range": "true|false",
                    },
                ],
            },
            {
                "name": "CanIf",
                "params": [
                    {
                        "name": "CanIfTxPduCanIdType",
                        "summary": "STANDARD or EXTENDED CAN ID",
                        "detail": "Matches frame triggering id type.",
                        "range": "STANDARD|EXTENDED",
                    },
                ],
            },
            {
                "name": "PduR",
                "params": [
                    {
                        "name": "PduRMaxRoutingTableEntries",
                        "summary": "Max routing table size",
                        "detail": "Upper bound for generated routes.",
                        "range": "1-1024",
                    },
                ],
            },
            {
                "name": "CanNm",
                "params": [
                    {
                        "name": "CanNmTimeoutTime",
                        "summary": "NM timeout in ms",
                        "detail": "Bus-sleep transition timer.",
                        "range": "100-10000",
                    },
                ],
            },
        ],
    }


def load_default_bswmd_pack() -> int:
    pack = default_bswmd_pack()
    rows = []
    for mod in pack.get("modules") or []:
        mname = str(mod.get("name") or "Module")
        for p in mod.get("params") or []:
            pname = str(p.get("name") or "Param")
            rid = str(p.get("id") or ("ecuc.%s.%s" % (mname.lower(), pname)))
            rows.append({
                "id": rid,
                "title": "%s / %s" % (mname, pname),
                "category": "BSWMD %s" % mname,
                "summary": str(p.get("summary") or pname),
                "detail": str(p.get("detail") or ""),
                "range": str(p.get("range") or ""),
                "definition": str(p.get("definition") or ""),
            })
    return merge_bswmd_tips(rows)


# Redefine Spec search to include imported BSWMD extras
def search_spec(query: str) -> List[dict]:
    q = (query or "").strip().lower()
    pool = list(SPEC_GLOSSARY) + list(BSWMD_LITE) + list(_BSWMD_EXTRA)
    if not q:
        return pool
    out = []
    for row in pool:
        blob = " ".join([
            row.get("id", ""), row.get("title", ""),
            row.get("summary", ""), row.get("detail", ""),
            row.get("category", ""),
        ]).lower()
        if q in blob:
            out.append(row)
    return out


def tip_for_field(field_id: str) -> Optional[dict]:
    for row in SPEC_GLOSSARY:
        if row["id"] == field_id:
            return row
    for row in BSWMD_LITE:
        if row["id"] == field_id:
            return row
    for row in _BSWMD_EXTRA:
        if row["id"] == field_id:
            return row
    short = (field_id or "").rsplit(".", 1)[-1]
    for row in list(BSWMD_LITE) + list(_BSWMD_EXTRA):
        title = row.get("title", "")
        if row.get("id", "").endswith("." + short) or title.endswith(
                "/ " + short):
            return row
    return None


FIX_RECIPES: Dict[str, dict] = {
    "bump_dlc": {
        "title": "Bump DLC to cover bits",
        "detail": "Set I-PDU LENGTH to fit intel-mapped signals.",
    },
    "unique_can_ids": {
        "title": "Assign unique CAN IDs",
        "detail": "Replace duplicate/zero IDs with 0x100+index.",
    },
    "derive_ecuc": {
        "title": "Re-derive ECUC-lite",
        "detail": "Rebuild Com/CanIf/PduR/CanNm from COM.",
    },
    "name_empty_signals": {
        "title": "Name empty signals",
        "detail": "Fill blank SHORT-NAME with SigN.",
    },
}


def apply_fix_recipes(
        com: ArxmlModel, ecuc: Optional[EcucModel],
        recipes: List[str],
        ecu_name: str = "Ecu") -> Tuple[ArxmlModel, EcucModel, List[str]]:
    """Apply named recipe pack. Returns (com, ecuc, log lines)."""
    model = copy.deepcopy(com)
    ecu = copy.deepcopy(ecuc) if ecuc else empty_ecuc()
    log: List[str] = []
    for rid in recipes:
        if rid == "bump_dlc":
            n = 0
            for pdu in model.ipdus:
                need = 0
                for sig in pdu.signals:
                    if normalize_endian(sig.endian) == ENDIAN_INTEL:
                        need = max(need, sig.start_bit + max(0, sig.length))
                need_b = (need + 7) // 8
                if need_b > pdu.dlc:
                    pdu.dlc = need_b
                    n += 1
            log.append("bump_dlc: %d PDU(s)" % n)
        elif rid == "unique_can_ids":
            used = set()
            n = 0
            for i, pdu in enumerate(model.ipdus):
                if pdu.can_id == 0 or pdu.can_id in used:
                    cand = 0x100 + i
                    while cand in used:
                        cand += 1
                    pdu.can_id = cand
                    n += 1
                used.add(pdu.can_id)
            log.append("unique_can_ids: %d PDU(s)" % n)
        elif rid == "name_empty_signals":
            n = 0
            for pdu in model.ipdus:
                for j, sig in enumerate(pdu.signals):
                    if not (sig.name or "").strip():
                        sig.name = "Sig%d" % j
                        n += 1
            log.append("name_empty_signals: %d" % n)
        elif rid == "derive_ecuc":
            ecu = derive_ecuc_from_com(model, ecu_name=ecu_name)
            log.append("derive_ecuc: %d modules" % len(ecu.modules))
        else:
            log.append("unknown recipe: %s" % rid)
    return model, ecu, log


@dataclass
class SwcPort:
    name: str
    direction: str = "Sender"  # Sender | Receiver
    signal: str = ""
    pdu: str = ""

    def to_dict(self) -> dict:
        return {
            "name": self.name, "direction": self.direction,
            "signal": self.signal, "pdu": self.pdu,
        }

    @staticmethod
    def from_dict(d: dict) -> "SwcPort":
        return SwcPort(
            name=str(d.get("name") or "Port"),
            direction=str(d.get("direction") or "Sender"),
            signal=str(d.get("signal") or ""),
            pdu=str(d.get("pdu") or ""),
        )


@dataclass
class SwcComponent:
    name: str
    ports: List[SwcPort] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "ports": [p.to_dict() for p in self.ports],
        }

    @staticmethod
    def from_dict(d: dict) -> "SwcComponent":
        return SwcComponent(
            name=str(d.get("name") or "Swc"),
            ports=[SwcPort.from_dict(p) for p in (d.get("ports") or [])],
        )


@dataclass
class SwcModel:
    """Lite SWC / port map — configuration only, no RTE codegen."""
    components: List[SwcComponent] = field(default_factory=list)
    package: str = "Swc"
    path: str = ""

    def clone(self) -> "SwcModel":
        return copy.deepcopy(self)

    def to_dict(self) -> dict:
        return {
            "package": self.package,
            "path": self.path,
            "components": [c.to_dict() for c in self.components],
        }

    @staticmethod
    def from_dict(d: dict) -> "SwcModel":
        return SwcModel(
            package=str(d.get("package") or "Swc"),
            path=str(d.get("path") or ""),
            components=[
                SwcComponent.from_dict(c) for c in (d.get("components") or [])],
        )


def empty_swc() -> SwcModel:
    return SwcModel(package="Swc", components=[])


def derive_swc_from_com(
        model: ArxmlModel, swc_name: str = "AppSwc") -> SwcModel:
    """One SWC with a Sender port per COM signal (mapping lite)."""
    ports = []
    for pdu in model.ipdus:
        for sig in pdu.signals:
            ports.append(SwcPort(
                name="P_%s" % sig.name,
                direction="Sender",
                signal=sig.name,
                pdu=pdu.name,
            ))
    return SwcModel(
        package="Swc",
        components=[SwcComponent(name=swc_name, ports=ports)],
    )


def serialize_swc_lite(swc: SwcModel) -> str:
    el_root = ET.Element("AUTOSAR")
    pkgs = ET.SubElement(el_root, "AR-PACKAGES")
    pkg = ET.SubElement(pkgs, "AR-PACKAGE")
    ET.SubElement(pkg, "SHORT-NAME").text = swc.package or "Swc"
    elements = ET.SubElement(pkg, "ELEMENTS")
    for comp in swc.components:
        cel = ET.SubElement(elements, "APPLICATION-SW-COMPONENT-TYPE")
        ET.SubElement(cel, "SHORT-NAME").text = comp.name
        ports_el = ET.SubElement(cel, "PORTS")
        for p in comp.ports:
            pel = ET.SubElement(ports_el, "P-PORT-PROTOTYPE"
                               if p.direction == "Sender"
                               else "R-PORT-PROTOTYPE")
            ET.SubElement(pel, "SHORT-NAME").text = p.name
            if p.signal:
                ET.SubElement(pel, "SIGNAL-REF").text = p.signal
            if p.pdu:
                ET.SubElement(pel, "PDU-REF").text = p.pdu
    ET.indent(el_root, space="  ")
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + ET.tostring(
        el_root, encoding="unicode")


def parse_swc_lite(path: str) -> SwcModel:
    tree = ET.parse(path)
    root = tree.getroot()
    comps = []
    for el in _all(root, "APPLICATION-SW-COMPONENT-TYPE"):
        name = _text(el, "SHORT-NAME") or "Swc"
        ports = []
        for pel in el.iter():
            tag = _ln(pel.tag)
            if tag not in ("P-PORT-PROTOTYPE", "R-PORT-PROTOTYPE"):
                continue
            ports.append(SwcPort(
                name=_text(pel, "SHORT-NAME") or "Port",
                direction="Sender" if tag.startswith("P-") else "Receiver",
                signal=_text(pel, "SIGNAL-REF"),
                pdu=_text(pel, "PDU-REF"),
            ))
        comps.append(SwcComponent(name=name, ports=ports))
    return SwcModel(components=comps, path=path)


def validate_swc(com: ArxmlModel, swc: SwcModel) -> List[dict]:
    findings = []
    sigs = {s.name for p in com.ipdus for s in p.signals}
    pdus = {p.name for p in com.ipdus}

    def add(level, rule, message, fix=""):
        sev = {"error": "error", "warn": "warning"}.get(level, "info")
        findings.append({
            "level": level, "severity": sev, "rule": rule,
            "message": message, "fix": fix, "artifact": "swc",
            "pdu": "", "signal": "", "location": "swc",
        })

    if not swc.components:
        add("warn", "swc_empty",
            "No SWC components — derive from COM on SWC page",
            fix="SWC → Derive from COM")
    for c in swc.components:
        for p in c.ports:
            if p.signal and p.signal not in sigs:
                add("error", "swc_signal_missing",
                    "Port %s refs missing signal %s" % (p.name, p.signal),
                    fix="Re-derive SWC or fix SIGNAL-REF")
            if p.pdu and p.pdu not in pdus:
                add("warn", "swc_pdu_missing",
                    "Port %s refs missing PDU %s" % (p.name, p.pdu),
                    fix="Re-derive SWC or fix PDU-REF")
    if not findings:
        add("info", "ok", "No SWC issues")
    return findings
