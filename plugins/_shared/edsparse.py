# -*- coding: utf-8 -*-
"""edsparse — CANopen EDS/DCF parse, serialize, and CiA 306-oriented validate.

Shared by eds-studio and canopen-suite. Competes with CANeds / emotas at the
description-file layer (CiA 306 EDS + DCF ParameterValue / DeviceCommissioning).
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple


SECTION_RE = re.compile(
    r"^\[([0-9A-Fa-f]{1,4}|[A-Za-z][A-Za-z0-9_]*)(?:sub([0-9A-Fa-f]+))?\]\s*$",
    re.IGNORECASE,
)
KEY_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*|[0-9]+)\s*=\s*(.*)$")

META_SECTIONS = {
    "fileinfo", "deviceinfo", "dummyusage", "comments",
    "mandatoryobjects", "optionalobjects", "manufacturerobjects",
    "devicecommissioning",
}

# CiA 301 communication profile — commonly required / expected
MANDATORY_INDEXES = (0x1000, 0x1001, 0x1018)

OBJECT_TYPES = (
    ("0x7", "VAR"),
    ("0x8", "ARRAY"),
    ("0x9", "RECORD"),
    ("0x2", "DOMAIN"),
)

DATA_TYPES = (
    ("0x0001", "BOOLEAN"),
    ("0x0002", "INTEGER8"),
    ("0x0003", "INTEGER16"),
    ("0x0004", "INTEGER32"),
    ("0x0005", "UNSIGNED8"),
    ("0x0006", "UNSIGNED16"),
    ("0x0007", "UNSIGNED32"),
    ("0x0008", "REAL32"),
    ("0x0009", "VISIBLE_STRING"),
    ("0x000A", "OCTET_STRING"),
    ("0x000B", "UNICODE_STRING"),
    ("0x000F", "DOMAIN"),
    ("0x0010", "INTEGER24"),
    ("0x0011", "REAL64"),
    ("0x0012", "INTEGER40"),
    ("0x0013", "INTEGER48"),
    ("0x0014", "INTEGER56"),
    ("0x0015", "INTEGER64"),
    ("0x0016", "UNSIGNED24"),
    ("0x0018", "UNSIGNED40"),
    ("0x0019", "UNSIGNED48"),
    ("0x001A", "UNSIGNED56"),
    ("0x001B", "UNSIGNED64"),
)

ACCESS_TYPES = ("ro", "wo", "rw", "const", "rwr", "rww")


@dataclass
class OdEntry:
    index: int
    subindex: int = 0
    name: str = ""
    object_type: str = ""
    data_type: str = ""
    access_type: str = ""
    default_value: str = ""
    pdo_mapping: str = ""
    parameter_value: str = ""  # DCF
    low_limit: str = ""
    high_limit: str = ""
    extra: Dict[str, str] = field(default_factory=dict)

    @property
    def key(self) -> Tuple[int, int]:
        return (self.index, self.subindex)

    def display_index(self) -> str:
        if self.subindex:
            return "0x%04X:%02X" % (self.index, self.subindex)
        return "0x%04X" % self.index

    def effective_value(self) -> str:
        return self.parameter_value or self.default_value or ""


@dataclass
class EdsDocument:
    entries: List[OdEntry] = field(default_factory=list)
    file_info: Dict[str, str] = field(default_factory=dict)
    device_info: Dict[str, str] = field(default_factory=dict)
    device_commissioning: Dict[str, str] = field(default_factory=dict)
    other_meta: Dict[str, Dict[str, str]] = field(default_factory=dict)
    path: str = ""
    is_dcf: bool = False

    def entry(self, index: int, subindex: int = 0) -> Optional[OdEntry]:
        return find_entry(self.entries, index, subindex)

    def indexes(self) -> List[int]:
        return sorted({e.index for e in self.entries})


def parse_eds(text: str) -> List[OdEntry]:
    return parse_eds_document(text).entries


def parse_eds_document(text: str, path: str = "") -> EdsDocument:
    doc = EdsDocument()
    doc.path = path or ""
    if path.lower().endswith(".dcf"):
        doc.is_dcf = True
    entries: Dict[Tuple[int, int], OdEntry] = {}
    current_entry: Optional[OdEntry] = None
    meta_bucket: Optional[Dict[str, str]] = None

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        m = SECTION_RE.match(line)
        if m:
            current_entry = None
            meta_bucket = None
            head = m.group(1)
            if re.fullmatch(r"[0-9A-Fa-f]{1,4}", head, re.IGNORECASE):
                idx = int(head, 16)
                sub = int(m.group(2), 16) if m.group(2) else 0
                entry = OdEntry(index=idx, subindex=sub)
                entries[(idx, sub)] = entry
                current_entry = entry
                continue
            name = head.lower()
            if name == "fileinfo":
                meta_bucket = doc.file_info
            elif name == "deviceinfo":
                meta_bucket = doc.device_info
            elif name == "devicecommissioning":
                meta_bucket = doc.device_commissioning
                doc.is_dcf = True
            else:
                doc.other_meta[name] = {}
                meta_bucket = doc.other_meta[name]
            continue
        km = KEY_RE.match(line)
        if not km:
            continue
        key, val = km.group(1), km.group(2).strip().strip('"')
        if current_entry is not None:
            kl = key.lower()
            if kl == "parametername":
                current_entry.name = val
            elif kl == "objecttype":
                current_entry.object_type = val
            elif kl == "datatype":
                current_entry.data_type = val
            elif kl == "accesstype":
                current_entry.access_type = val
            elif kl == "defaultvalue":
                current_entry.default_value = val
            elif kl == "parametervalue":
                current_entry.parameter_value = val
                doc.is_dcf = True
            elif kl == "pdomapping":
                current_entry.pdo_mapping = val
            elif kl == "lowlimit":
                current_entry.low_limit = val
            elif kl == "highlimit":
                current_entry.high_limit = val
            else:
                current_entry.extra[key] = val
        elif meta_bucket is not None:
            meta_bucket[key] = val

    doc.entries = sorted(entries.values(), key=lambda e: (e.index, e.subindex))
    return doc


def parse_eds_file(path: str) -> List[OdEntry]:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return parse_eds(f.read())


def parse_eds_file_document(path: str) -> EdsDocument:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return parse_eds_document(f.read(), path=path)


def _emit_section(lines: list, title: str, data: Dict[str, str]) -> None:
    if not data:
        return
    lines.append("[%s]" % title)
    for k, v in data.items():
        lines.append("%s=%s" % (k, v))
    lines.append("")


def _emit_supported(lines: list, title: str, indices: List[int]) -> None:
    lines.append("[%s]" % title)
    lines.append("SupportedObjects=%d" % len(indices))
    for n, idx in enumerate(indices, 1):
        lines.append("%d=0x%04X" % (n, idx))
    lines.append("")


def index_group(index: int) -> str:
    if 0x1000 <= index <= 0x1FFF:
        return "Communication profile"
    if 0x2000 <= index <= 0x5FFF:
        return "Manufacturer"
    if 0x6000 <= index <= 0x9FFF:
        return "Device profile"
    return "Other"


def object_type_label(value: str) -> str:
    raw = (value or "").strip().lower()
    for code, name in OBJECT_TYPES:
        if raw in (code.lower(), name.lower()):
            return name
    return value or "VAR"


def data_type_label(value: str) -> str:
    raw = (value or "").strip().lower()
    for code, name in DATA_TYPES:
        if raw in (code.lower(), name.lower()):
            return name
    return value or "UNSIGNED32"


def export_eds_text(
    entries: List[OdEntry],
    file_name: str = "export.eds",
    device_name: str = "EDS Studio Export",
    file_info: Optional[Dict[str, str]] = None,
    device_info: Optional[Dict[str, str]] = None,
    other_meta: Optional[Dict[str, Dict[str, str]]] = None,
    device_commissioning: Optional[Dict[str, str]] = None,
    as_dcf: bool = False,
) -> str:
    """Serialize OD entries; preserve meta when provided."""
    fi = dict(file_info or {})
    di = dict(device_info or {})
    if not fi:
        fi = {
            "FileName": file_name,
            "FileVersion": "1",
            "FileRevision": "0",
            "EDSVersion": "4.0",
            "Description": "Generated by EDS Studio",
            "CreationDate": "01-01-2026",
            "CreationTime": "00:00AM",
            "CreatedBy": "sin",
        }
    else:
        fi.setdefault("FileName", file_name)
        fi.setdefault("Description", fi.get("Description") or "EDS Studio")
    if not di:
        di = {"VendorName": "sin", "ProductName": device_name}
    else:
        di.setdefault("ProductName", device_name)

    lines: List[str] = []
    _emit_section(lines, "FileInfo", fi)
    _emit_section(lines, "DeviceInfo", di)
    if as_dcf or device_commissioning:
        dc = dict(device_commissioning or {})
        dc.setdefault("NodeID", "1")
        dc.setdefault("NodeName", device_name)
        dc.setdefault("BaudRate", "500")
        dc.setdefault("NetNumber", "0")
        dc.setdefault("LSS_SerialNumber", "0")
        _emit_section(lines, "DeviceCommissioning", dc)

    for title, data in (other_meta or {}).items():
        if title in (
            "fileinfo", "deviceinfo", "devicecommissioning",
            "mandatoryobjects", "optionalobjects", "manufacturerobjects",
        ):
            continue
        nice = title[:1].upper() + title[1:] if title else title
        for key in ("DummyUsage", "Comments"):
            if key.lower() == title:
                nice = key
                break
        _emit_section(lines, nice, data)

    indices = sorted({e.index for e in entries})
    mandatory = [i for i in indices if i in MANDATORY_INDEXES]
    manufacturer = [i for i in indices if 0x2000 <= i <= 0x5FFF]
    covered = set(mandatory) | set(manufacturer)
    optional = [i for i in indices if i not in covered]
    _emit_supported(lines, "MandatoryObjects", mandatory)
    _emit_supported(lines, "OptionalObjects", optional)
    _emit_supported(lines, "ManufacturerObjects", manufacturer)

    for e in sorted(entries, key=lambda x: (x.index, x.subindex)):
        if e.subindex:
            lines.append("[%04Xsub%X]" % (e.index, e.subindex))
        else:
            lines.append("[%04X]" % e.index)
        if e.name:
            lines.append("ParameterName=%s" % e.name)
        if e.object_type:
            lines.append("ObjectType=%s" % e.object_type)
        if e.data_type:
            lines.append("DataType=%s" % e.data_type)
        if e.access_type:
            lines.append("AccessType=%s" % e.access_type)
        if e.default_value:
            lines.append("DefaultValue=%s" % e.default_value)
        if as_dcf or e.parameter_value:
            pv = e.parameter_value if e.parameter_value != "" else e.default_value
            if pv:
                lines.append("ParameterValue=%s" % pv)
        if e.pdo_mapping:
            lines.append("PDOMapping=%s" % e.pdo_mapping)
        if e.low_limit:
            lines.append("LowLimit=%s" % e.low_limit)
        if e.high_limit:
            lines.append("HighLimit=%s" % e.high_limit)
        for k, v in sorted(e.extra.items()):
            lines.append("%s=%s" % (k, v))
        lines.append("")
    return "\n".join(lines)


def export_document(doc: EdsDocument, as_dcf: Optional[bool] = None) -> str:
    use_dcf = doc.is_dcf if as_dcf is None else as_dcf
    name = os_basename(doc.path) if doc.path else (
        "export.dcf" if use_dcf else "export.eds")
    return export_eds_text(
        doc.entries,
        file_name=name,
        device_name=doc.device_info.get("ProductName") or "Device",
        file_info=doc.file_info,
        device_info=doc.device_info,
        other_meta=doc.other_meta,
        device_commissioning=doc.device_commissioning,
        as_dcf=use_dcf,
    )


def os_basename(path: str) -> str:
    import os
    return os.path.basename(path) if path else "export.eds"


def _norm_access(value: str) -> str:
    return (value or "").strip().lower()


def _norm_dtype(value: str) -> str:
    return data_type_label(value).upper()


def _parse_map_dword(text: str) -> Optional[Tuple[int, int, int]]:
    """Decode PDO mapping entry: index(16) | sub(8) | bitlen(8)."""
    s = (text or "").strip().lower().replace("0x", "")
    if not s:
        return None
    try:
        raw = int(s, 16)
    except ValueError:
        return None
    bitlen = raw & 0xFF
    sub = (raw >> 8) & 0xFF
    idx = (raw >> 16) & 0xFFFF
    return idx, sub, bitlen


def validate_eds(
    entries: List[OdEntry],
    *,
    file_info: Optional[Dict[str, str]] = None,
    device_info: Optional[Dict[str, str]] = None,
    device_commissioning: Optional[Dict[str, str]] = None,
    deep: bool = True,
) -> List[dict]:
    """Return findings: level error|warn|info, message, index, can jump via index."""
    findings: List[dict] = []
    keys = set()
    by_key = {}
    for e in entries:
        if e.key in keys:
            findings.append({
                "level": "error",
                "severity": "error",
                "rule": "duplicate",
                "message": "Duplicate %s" % e.display_index(),
                "index": e.display_index(),
                "od_index": e.index,
                "od_sub": e.subindex,
            })
        keys.add(e.key)
        by_key[e.key] = e
        if not e.name:
            findings.append({
                "level": "warn",
                "severity": "warning",
                "rule": "parameter_name",
                "message": "Missing ParameterName",
                "index": e.display_index(),
                "od_index": e.index,
                "od_sub": e.subindex,
            })
        if deep and e.access_type and _norm_access(e.access_type) not in ACCESS_TYPES:
            findings.append({
                "level": "warn",
                "severity": "warning",
                "rule": "access_type",
                "message": "Unknown AccessType %s" % e.access_type,
                "index": e.display_index(),
                "od_index": e.index,
                "od_sub": e.subindex,
            })
        if deep and e.data_type:
            known = {_norm_dtype(c) for c, _ in DATA_TYPES} | {
                n.upper() for _, n in DATA_TYPES}
            if _norm_dtype(e.data_type) not in known and e.data_type.strip():
                # allow raw hex not in table
                raw = e.data_type.strip().lower().replace("0x", "")
                if not re.fullmatch(r"[0-9a-f]{1,4}", raw):
                    findings.append({
                        "level": "warn",
                        "severity": "warning",
                        "rule": "data_type",
                        "message": "Unrecognized DataType %s" % e.data_type,
                        "index": e.display_index(),
                        "od_index": e.index,
                        "od_sub": e.subindex,
                    })

    have = {e.index for e in entries}
    for req in MANDATORY_INDEXES:
        if req not in have:
            findings.append({
                "level": "error" if req == 0x1000 else "warn",
                "severity": "error" if req == 0x1000 else "warning",
                "rule": "mandatory",
                "message": "Missing mandatory object 0x%04X" % req,
                "index": "0x%04X" % req,
                "od_index": req,
                "od_sub": 0,
            })

    if deep:
        # SubNumber consistency for ARRAY/RECORD parents
        parents = [e for e in entries if e.subindex == 0]
        for p in parents:
            ot = object_type_label(p.object_type).upper()
            if ot not in ("ARRAY", "RECORD"):
                continue
            subs = [e for e in entries if e.index == p.index and e.subindex > 0]
            declared = p.extra.get("SubNumber") or p.extra.get("subnumber")
            if declared:
                try:
                    n = int(str(declared).replace("0x", ""), 0)
                    if n != len(subs) and n != len(subs) + 1:
                        # SubNumber often includes sub0
                        findings.append({
                            "level": "warn",
                            "severity": "warning",
                            "rule": "sub_number",
                            "message": "SubNumber=%s but %d sub-entries"
                            % (declared, len(subs)),
                            "index": p.display_index(),
                            "od_index": p.index,
                            "od_sub": 0,
                        })
                except ValueError:
                    pass

        # PDO mapping references
        for e in entries:
            if not (0x1600 <= e.index <= 0x17FF or 0x1A00 <= e.index <= 0x1BFF):
                continue
            if e.subindex == 0:
                continue
            mapped = _parse_map_dword(e.default_value or e.parameter_value)
            if not mapped:
                continue
            idx, sub, _bits = mapped
            if idx == 0:
                continue
            if (idx, sub) not in by_key and (idx, 0) not in by_key:
                findings.append({
                    "level": "error",
                    "severity": "error",
                    "rule": "pdo_map_ref",
                    "message": "PDO map points to missing 0x%04X:%02X"
                    % (idx, sub),
                    "index": e.display_index(),
                    "od_index": e.index,
                    "od_sub": e.subindex,
                })

    if file_info is not None and not file_info.get("FileName"):
        findings.append({
            "level": "warn",
            "severity": "warning",
            "rule": "file_info",
            "message": "FileInfo.FileName is empty",
            "index": "",
            "od_index": None,
            "od_sub": None,
        })
    if device_info is not None and not device_info.get("VendorName"):
        findings.append({
            "level": "warn",
            "severity": "warning",
            "rule": "device_info",
            "message": "DeviceInfo.VendorName is empty",
            "index": "",
            "od_index": None,
            "od_sub": None,
        })
    if device_commissioning:
        nid = device_commissioning.get("NodeID") or device_commissioning.get("NodeId")
        if nid:
            try:
                n = int(str(nid), 0)
                if not (1 <= n <= 127):
                    findings.append({
                        "level": "error",
                        "severity": "error",
                        "rule": "node_id",
                        "message": "DeviceCommissioning.NodeID out of range",
                        "index": "",
                        "od_index": None,
                        "od_sub": None,
                    })
            except ValueError:
                findings.append({
                    "level": "error",
                    "severity": "error",
                    "rule": "node_id",
                    "message": "DeviceCommissioning.NodeID is not a number",
                    "index": "",
                    "od_index": None,
                    "od_sub": None,
                })

    if not findings:
        findings.append({
            "level": "info",
            "severity": "info",
            "rule": "ok",
            "message": "No issues",
            "index": "",
            "od_index": None,
            "od_sub": None,
        })
    return findings


def validate_document(doc: EdsDocument, deep: bool = True) -> List[dict]:
    return validate_eds(
        doc.entries,
        file_info=doc.file_info,
        device_info=doc.device_info,
        device_commissioning=doc.device_commissioning or None,
        deep=deep,
    )


def entries_to_dict(entries: List[OdEntry]) -> Dict[Tuple[int, int], OdEntry]:
    return {e.key: e for e in entries}


def find_entry(
    entries: List[OdEntry], index: int, subindex: int = 0
) -> Optional[OdEntry]:
    for e in entries:
        if e.index == index and e.subindex == subindex:
            return e
    return None


def diff_documents(a: EdsDocument, b: EdsDocument) -> List[dict]:
    """Return list of {kind, index, name, detail}."""
    ka = {(e.index, e.subindex): e for e in a.entries}
    kb = {(e.index, e.subindex): e for e in b.entries}
    rows = []
    for key in sorted(set(ka) | set(kb)):
        ea, eb = ka.get(key), kb.get(key)
        idx = "0x%04X" % key[0] + ((":%02X" % key[1]) if key[1] else "")
        if ea and not eb:
            rows.append({
                "kind": "removed", "index": idx,
                "name": ea.name, "detail": "only in A",
            })
        elif eb and not ea:
            rows.append({
                "kind": "added", "index": idx,
                "name": eb.name, "detail": "only in B",
            })
        else:
            changes = []
            for attr in (
                "name", "object_type", "data_type", "access_type",
                "default_value", "parameter_value", "pdo_mapping",
            ):
                if getattr(ea, attr) != getattr(eb, attr):
                    changes.append("%s: %s → %s" % (
                        attr, getattr(ea, attr), getattr(eb, attr)))
            if changes:
                rows.append({
                    "kind": "changed", "index": idx,
                    "name": eb.name or ea.name,
                    "detail": "; ".join(changes[:4]),
                })
    return rows


def empty_document() -> EdsDocument:
    doc = EdsDocument()
    doc.file_info = {
        "FileName": "Untitled.eds",
        "FileVersion": "1",
        "FileRevision": "0",
        "EDSVersion": "4.0",
        "Description": "New EDS",
        "CreatedBy": "sin",
    }
    doc.device_info = {
        "VendorName": "sin",
        "ProductName": "New Device",
    }
    doc.entries = [
        OdEntry(0x1000, 0, "Device type", "0x7", "0x0007", "ro", "0x00000000"),
        OdEntry(0x1001, 0, "Error register", "0x7", "0x0005", "ro", "0x00"),
        OdEntry(0x1018, 0, "Identity object", "0x8", "", "", "",
                extra={"SubNumber": "4"}),
        OdEntry(0x1018, 1, "Vendor-ID", "0x7", "0x0007", "ro", "0x00000000"),
        OdEntry(0x1018, 2, "Product code", "0x7", "0x0007", "ro", "0x00000000"),
        OdEntry(0x1018, 3, "Revision number", "0x7", "0x0007", "ro", "0x00000000"),
        OdEntry(0x1018, 4, "Serial number", "0x7", "0x0007", "ro", "0x00000000"),
    ]
    return doc
