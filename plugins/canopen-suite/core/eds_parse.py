# -*- coding: utf-8 -*-
"""EDS/DCF parser with FileInfo / DeviceInfo round-trip and OD lint.

Recognises [xxxx] / [xxxxsubN] sections and common keys.
Competes with CANeds / emotas DeviceExplorer at the description-file layer.
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
}


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
    extra: Dict[str, str] = field(default_factory=dict)

    @property
    def key(self) -> Tuple[int, int]:
        return (self.index, self.subindex)

    def display_index(self) -> str:
        if self.subindex:
            return "0x%04X:%02X" % (self.index, self.subindex)
        return "0x%04X" % self.index


@dataclass
class EdsDocument:
    entries: List[OdEntry] = field(default_factory=list)
    file_info: Dict[str, str] = field(default_factory=dict)
    device_info: Dict[str, str] = field(default_factory=dict)
    other_meta: Dict[str, Dict[str, str]] = field(default_factory=dict)


def parse_eds(text: str) -> List[OdEntry]:
    return parse_eds_document(text).entries


def parse_eds_document(text: str) -> EdsDocument:
    doc = EdsDocument()
    entries: Dict[Tuple[int, int], OdEntry] = {}
    current_entry: Optional[OdEntry] = None
    current_meta: Optional[str] = None
    meta_bucket: Optional[Dict[str, str]] = None

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        m = SECTION_RE.match(line)
        if m:
            current_entry = None
            current_meta = None
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
            current_meta = name
            if name == "fileinfo":
                meta_bucket = doc.file_info
            elif name == "deviceinfo":
                meta_bucket = doc.device_info
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
            elif kl == "pdomapping":
                current_entry.pdo_mapping = val
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
        return parse_eds_document(f.read())


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
    ("0x0011", "REAL64"),
)

ACCESS_TYPES = ("ro", "wo", "rw", "const", "rwr", "rww")


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
    device_name: str = "CANopen Suite Export",
    file_info: Optional[Dict[str, str]] = None,
    device_info: Optional[Dict[str, str]] = None,
    other_meta: Optional[Dict[str, Dict[str, str]]] = None,
) -> str:
    """Serialize OD entries; preserve FileInfo/DeviceInfo when provided."""
    fi = dict(file_info or {})
    di = dict(device_info or {})
    if not fi:
        fi = {
            "FileName": file_name,
            "FileVersion": "1",
            "FileRevision": "0",
            "EDSVersion": "4.0",
            "Description": "Generated by CANopen Suite",
            "CreationDate": "01-01-2026",
            "CreationTime": "00:00AM",
            "CreatedBy": "sin",
        }
    else:
        fi.setdefault("FileName", file_name)
    if not di:
        di = {"VendorName": "sin", "ProductName": device_name}
    else:
        di.setdefault("ProductName", device_name)

    lines: List[str] = []
    _emit_section(lines, "FileInfo", fi)
    _emit_section(lines, "DeviceInfo", di)
    for title, data in (other_meta or {}).items():
        if title in ("fileinfo", "deviceinfo", "mandatoryobjects",
                     "optionalobjects", "manufacturerobjects"):
            continue
        nice = title[:1].upper() + title[1:] if title else title
        for key in ("DummyUsage", "Comments"):
            if key.lower() == title:
                nice = key
                break
        _emit_section(lines, nice, data)

    indices = sorted({e.index for e in entries})
    mandatory = [i for i in indices if i in (0x1000, 0x1001, 0x1018)]
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
        if e.pdo_mapping:
            lines.append("PDOMapping=%s" % e.pdo_mapping)
        for k, v in sorted(e.extra.items()):
            lines.append("%s=%s" % (k, v))
        lines.append("")
    return "\n".join(lines)


def validate_eds(entries: List[OdEntry]) -> List[dict]:
    findings = []
    keys = set()
    for e in entries:
        if e.key in keys:
            findings.append({
                "level": "error",
                "message": "Duplicate %s" % e.display_index(),
                "index": e.display_index(),
            })
        keys.add(e.key)
        if not e.name:
            findings.append({
                "level": "warn",
                "message": "Missing ParameterName",
                "index": e.display_index(),
            })
    have = {e.index for e in entries}
    for req in (0x1000, 0x1018):
        if req not in have:
            findings.append({
                "level": "warn",
                "message": "Missing mandatory 0x%04X" % req,
                "index": "0x%04X" % req,
            })
    if not findings:
        findings.append({"level": "info", "message": "No issues", "index": ""})
    return findings


def entries_to_dict(entries: List[OdEntry]) -> Dict[Tuple[int, int], OdEntry]:
    return {e.key: e for e in entries}


def find_entry(
    entries: List[OdEntry], index: int, subindex: int = 0
) -> Optional[OdEntry]:
    for e in entries:
        if e.index == index and e.subindex == subindex:
            return e
    return None
