# -*- coding: utf-8 -*-
"""Public API: generate OD C/H for CANopenNode V4 or CanFestival."""

from __future__ import annotations

import os
from typing import Dict, List, Optional, Union

from _shared.edsparse import EdsDocument, OdEntry, MANDATORY_INDEXES, validate_eds

from . import canfestival, canopennode_v4
from .types import c_ident

TARGETS = (
    ("canopennode_v4", "CANopenNode V4 (OD.h / OD.c)"),
    ("canfestival", "CanFestival (Node.c / Node.h)"),
)

TARGET_IDS = {t[0] for t in TARGETS}


def _as_document(
        source: Union[EdsDocument, List[OdEntry], None],
        *,
        file_info: Optional[dict] = None,
        device_info: Optional[dict] = None,
        device_commissioning: Optional[dict] = None,
        path: str = "",
) -> EdsDocument:
    if isinstance(source, EdsDocument):
        return source
    doc = EdsDocument()
    doc.entries = list(source or [])
    doc.file_info = dict(file_info or {})
    doc.device_info = dict(device_info or {})
    doc.device_commissioning = dict(device_commissioning or {})
    doc.path = path or ""
    return doc


def preflight(doc: EdsDocument) -> List[dict]:
    """Blocking checks before codegen (returns findings; empty = ok)."""
    findings = []
    if not doc.entries:
        findings.append({
            "level": "error",
            "index": "",
            "message": "Object dictionary is empty",
            "rule": "empty",
        })
        return findings
    present = {e.index for e in doc.entries}
    for idx in MANDATORY_INDEXES:
        if idx not in present:
            findings.append({
                "level": "error",
                "index": "0x%04X" % idx,
                "message": "Mandatory object 0x%04X missing" % idx,
                "rule": "mandatory",
            })
    # Soft validate (warnings do not block)
    soft = validate_eds(
        doc.entries,
        file_info=doc.file_info,
        device_info=doc.device_info,
        device_commissioning=doc.device_commissioning or None,
        deep=True,
    )
    for f in soft:
        if f.get("rule") == "ok":
            continue
        level = (f.get("level") or "warning").lower()
        if level == "error":
            findings.append(f)
    return findings


def generate(
        source: Union[EdsDocument, List[OdEntry]],
        target: str,
        node_name: str = "Node",
        *,
        file_info: Optional[dict] = None,
        device_info: Optional[dict] = None,
        device_commissioning: Optional[dict] = None,
        path: str = "",
        strict: bool = True,
) -> Dict[str, str]:
    """Return {filename: text} for the selected stack target.

    Raises ValueError on unknown target or (when strict) preflight errors.
    """
    tid = (target or "").strip().lower()
    if tid not in TARGET_IDS:
        raise ValueError(
            "Unknown target %r; expected one of %s"
            % (target, ", ".join(sorted(TARGET_IDS))))
    doc = _as_document(
        source, file_info=file_info, device_info=device_info,
        device_commissioning=device_commissioning, path=path)
    if strict:
        bad = preflight(doc)
        if bad:
            msgs = "; ".join(
                "%s: %s" % (f.get("index") or "-", f.get("message", ""))
                for f in bad[:8])
            raise ValueError("Codegen blocked: %s" % msgs)
    name = c_ident(node_name, "Node")
    if tid == "canopennode_v4":
        return canopennode_v4.generate(doc, node_name=name)
    return canfestival.generate(doc, node_name=name)


def generate_to_dir(
        directory: str,
        source: Union[EdsDocument, List[OdEntry]],
        target: str,
        node_name: str = "Node",
        **kwargs,
) -> List[str]:
    """Write generated files into directory; return absolute paths written."""
    files = generate(source, target, node_name=node_name, **kwargs)
    os.makedirs(directory, exist_ok=True)
    written = []
    for name, text in files.items():
        path = os.path.join(directory, name)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        written.append(os.path.abspath(path))
    return written
