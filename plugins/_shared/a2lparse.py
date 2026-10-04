# -*- coding: utf-8 -*-
"""ASAP2 / A2L subset parser (MEASUREMENT, CHARACTERISTIC, AXIS_PTS, COMPU_*).

Enough for Symbol Explorer + XCP address lookup. Not a full ASAP2 compiler.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple


@dataclass
class A2lSymbol:
    kind: str  # MEASUREMENT | CHARACTERISTIC | AXIS_PTS | COMPU_METHOD | COMPU_VTAB
    name: str
    description: str = ""
    datatype: str = ""
    conversion: str = ""
    address: int = 0
    address_ext: int = 0
    char_type: str = ""  # VALUE | CURVE | MAP | …
    lower_limit: str = ""
    upper_limit: str = ""
    record_layout: str = ""
    axis_ref: str = ""
    module: str = ""
    raw_block: str = ""
    extra: Dict[str, str] = field(default_factory=dict)

    @property
    def key(self) -> Tuple[str, str]:
        return (self.kind, self.name)


@dataclass
class A2lDocument:
    path: str = ""
    project: str = ""
    module: str = ""
    symbols: List[A2lSymbol] = field(default_factory=list)
    dirty: bool = False
    source_text: str = ""

    def by_kind(self, kind: str) -> List[A2lSymbol]:
        return [s for s in self.symbols if s.kind == kind]

    def find(self, kind: str, name: str) -> Optional[A2lSymbol]:
        for s in self.symbols:
            if s.kind == kind and s.name == name:
                return s
        return None

    def measurements(self) -> List[A2lSymbol]:
        return self.by_kind("MEASUREMENT")

    def characteristics(self) -> List[A2lSymbol]:
        return self.by_kind("CHARACTERISTIC")


_BEGIN = re.compile(
    r"/begin\s+(MEASUREMENT|CHARACTERISTIC|AXIS_PTS|COMPU_METHOD|COMPU_VTAB)\s+"
    r"([A-Za-z_][\w.]*)\s*(?:\"([^\"]*)\")?",
    re.I,
)
_END = re.compile(
    r"/end\s+(MEASUREMENT|CHARACTERISTIC|AXIS_PTS|COMPU_METHOD|COMPU_VTAB)",
    re.I,
)
_ADDR = re.compile(r"\bECU_ADDRESS(?:_EXTENSION)?\s+(0x[0-9A-Fa-f]+|\d+)", re.I)
_ADDR_EXT = re.compile(r"\bECU_ADDRESS_EXTENSION\s+(0x[0-9A-Fa-f]+|\d+)", re.I)
_PROJECT = re.compile(r"/begin\s+PROJECT\s+([A-Za-z_][\w.]*)", re.I)
_MODULE = re.compile(r"/begin\s+MODULE\s+([A-Za-z_][\w.]*)", re.I)


def _parse_int(text: str) -> int:
    t = (text or "").strip()
    if not t:
        return 0
    try:
        return int(t, 0)
    except ValueError:
        return 0


def _first_tokens(body: str, n: int = 8) -> List[str]:
    # Strip nested /begin.../end noise for simple keyword scan
    parts = re.findall(r'"[^"]*"|[^\s"]+', body)
    return parts[:n]


def _parse_block(kind: str, name: str, desc: str, body: str, module: str) -> A2lSymbol:
    sym = A2lSymbol(
        kind=kind.upper(), name=name, description=desc or "",
        module=module, raw_block=body.strip())
    for m in _ADDR.finditer(body):
        # Prefer first ECU_ADDRESS (not EXTENSION) — EXTENSION matched separately
        key = m.group(0).upper()
        if "EXTENSION" in key:
            continue
        sym.address = _parse_int(m.group(1))
        break
    m_ext = _ADDR_EXT.search(body)
    if m_ext:
        sym.address_ext = _parse_int(m_ext.group(1))

    toks = _first_tokens(body, 12)
    # ASAP2: after name/desc, datatype / type often appear early
    if kind.upper() == "MEASUREMENT" and toks:
        # UBYTE NO_COMPU_METHOD 0 0 0 255
        if len(toks) >= 1 and not toks[0].startswith("/"):
            sym.datatype = toks[0]
        if len(toks) >= 2:
            sym.conversion = toks[1]
    elif kind.upper() == "CHARACTERISTIC" and toks:
        # VALUE NO_COMPU_METHOD 0 NO_RECORD_LAYOUT …
        if len(toks) >= 1:
            sym.char_type = toks[0]
        if len(toks) >= 2:
            sym.conversion = toks[1]
        if len(toks) >= 4:
            sym.record_layout = toks[3]
    elif kind.upper() == "COMPU_METHOD" and toks:
        if toks:
            sym.datatype = toks[0]  # reuse field as conversion type TAB_INTP etc.
    elif kind.upper() == "AXIS_PTS" and toks:
        if toks:
            sym.datatype = toks[0]

    m_axis = re.search(r"\bAXIS_PTS_REF\s+([A-Za-z_][\w.]*)", body, re.I)
    if m_axis:
        sym.axis_ref = m_axis.group(1)
    m_lo = re.search(r"\bLOWER_LIMIT\s+(\S+)", body, re.I)
    m_hi = re.search(r"\bUPPER_LIMIT\s+(\S+)", body, re.I)
    if m_lo:
        sym.lower_limit = m_lo.group(1)
    if m_hi:
        sym.upper_limit = m_hi.group(1)
    return sym


def parse_a2l_text(text: str, path: str = "") -> A2lDocument:
    doc = A2lDocument(path=path or "", source_text=text or "")
    m_proj = _PROJECT.search(text or "")
    if m_proj:
        doc.project = m_proj.group(1)
    m_mod = _MODULE.search(text or "")
    if m_mod:
        doc.module = m_mod.group(1)

    module = doc.module
    i = 0
    src = text or ""
    while True:
        m = _BEGIN.search(src, i)
        if not m:
            break
        kind, name, desc = m.group(1), m.group(2), m.group(3) or ""
        start = m.end()
        depth = 1
        pos = start
        end_body = -1
        while depth > 0:
            mb = _BEGIN.search(src, pos)
            me = _END.search(src, pos)
            if me is None:
                break
            if mb and mb.start() < me.start():
                depth += 1
                pos = mb.end()
                continue
            depth -= 1
            if depth == 0:
                end_body = me.start()
                i = me.end()
                break
            pos = me.end()
        if end_body < 0:
            i = start
            continue
        body = src[start:end_body]
        doc.symbols.append(_parse_block(kind, name, desc, body, module))
    return doc


def parse_a2l_file(path: str) -> A2lDocument:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    return parse_a2l_text(text, path=path)


def validate_document(doc: A2lDocument) -> List[dict]:
    """Return findings: {severity, rule, message, name, kind}."""
    findings: List[dict] = []
    seen: Dict[Tuple[str, str], int] = {}
    conversions = {s.name for s in doc.by_kind("COMPU_METHOD")}
    conversions |= {s.name for s in doc.by_kind("COMPU_VTAB")}
    conversions.add("NO_COMPU_METHOD")

    for s in doc.symbols:
        key = (s.kind, s.name)
        seen[key] = seen.get(key, 0) + 1
        if s.kind in ("MEASUREMENT", "CHARACTERISTIC", "AXIS_PTS"):
            if s.address == 0 and "ECU_ADDRESS" not in (s.raw_block or "").upper():
                findings.append({
                    "severity": "warning",
                    "rule": "missing_address",
                    "message": "%s %s has no ECU_ADDRESS" % (s.kind, s.name),
                    "name": s.name, "kind": s.kind,
                })
        if s.conversion and s.conversion not in conversions:
            if s.kind in ("MEASUREMENT", "CHARACTERISTIC"):
                findings.append({
                    "severity": "error",
                    "rule": "bad_conversion",
                    "message": "%s %s references missing COMPU %s"
                    % (s.kind, s.name, s.conversion),
                    "name": s.name, "kind": s.kind,
                })
        if s.axis_ref:
            if not doc.find("AXIS_PTS", s.axis_ref):
                findings.append({
                    "severity": "error",
                    "rule": "bad_axis_ref",
                    "message": "%s %s AXIS_PTS_REF %s missing"
                    % (s.kind, s.name, s.axis_ref),
                    "name": s.name, "kind": s.kind,
                })

    for key, n in seen.items():
        if n > 1:
            findings.append({
                "severity": "error",
                "rule": "duplicate",
                "message": "Duplicate %s %s (%d)" % (key[0], key[1], n),
                "name": key[1], "kind": key[0],
            })
    if not doc.symbols:
        findings.append({
            "severity": "error",
            "rule": "empty",
            "message": "No MEASUREMENT/CHARACTERISTIC/AXIS/COMPU blocks found",
            "name": "", "kind": "",
        })
    return findings


def export_symbol_csv(doc: A2lDocument) -> List[List[str]]:
    rows = [["Kind", "Name", "Address", "Datatype", "Conversion", "CharType", "Description"]]
    for s in doc.symbols:
        rows.append([
            s.kind, s.name,
            ("0x%X" % s.address) if s.address else "",
            s.datatype, s.conversion, s.char_type, s.description,
        ])
    return rows


def export_slim_a2l(doc: A2lDocument) -> str:
    """Rewrite a minimal ASAP2-looking document from parsed symbols."""
    lines = [
        "ASAP2_VERSION 1 71",
        "/begin PROJECT %s \"\"" % (doc.project or "OpenBus"),
        "  /begin MODULE %s \"\"" % (doc.module or "Module"),
    ]
    for s in doc.symbols:
        lines.append("    /begin %s %s \"%s\"" % (s.kind, s.name, s.description))
        if s.kind == "MEASUREMENT":
            lines.append(
                "      %s %s 0 0 0 0" % (
                    s.datatype or "UBYTE", s.conversion or "NO_COMPU_METHOD"))
        elif s.kind == "CHARACTERISTIC":
            lines.append(
                "      %s %s 0 %s 0 0 0" % (
                    s.char_type or "VALUE",
                    s.conversion or "NO_COMPU_METHOD",
                    s.record_layout or "NO_RECORD_LAYOUT"))
        elif s.kind == "AXIS_PTS":
            lines.append("      %s" % (s.datatype or "UBYTE"))
        elif s.kind in ("COMPU_METHOD", "COMPU_VTAB"):
            lines.append("      %s" % (s.datatype or "TAB_NOINTP"))
        if s.address:
            lines.append("      ECU_ADDRESS 0x%X" % s.address)
        if s.axis_ref:
            lines.append("      AXIS_PTS_REF %s" % s.axis_ref)
        lines.append("    /end %s" % s.kind)
    lines += ["  /end MODULE", "/end PROJECT", ""]
    return "\n".join(lines)


def empty_document() -> A2lDocument:
    text = export_slim_a2l(A2lDocument(project="Untitled", module="ECU"))
    return parse_a2l_text(text)


def compare_documents(a: A2lDocument, b: A2lDocument) -> List[dict]:
    """Diff by (kind, name): added / removed / address_changed."""
    ka = {s.key: s for s in a.symbols}
    kb = {s.key: s for s in b.symbols}
    out: List[dict] = []
    for k, sa in ka.items():
        if k not in kb:
            out.append({"change": "removed", "kind": k[0], "name": k[1]})
        else:
            sb = kb[k]
            if sa.address != sb.address:
                out.append({
                    "change": "address",
                    "kind": k[0], "name": k[1],
                    "a": "0x%X" % sa.address, "b": "0x%X" % sb.address,
                })
    for k in kb:
        if k not in ka:
            out.append({"change": "added", "kind": k[0], "name": k[1]})
    return out
