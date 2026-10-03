# -*- coding: utf-8 -*-
"""Codegen golden fixture — CANopenNode V4 OD.h/OD.c symbols for minimal OD."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from _shared import edsparse  # noqa: E402
from _shared.canopen_codegen import generate  # noqa: E402


GOLDEN_MARKERS_H = (
    "OD_RAM_t",
    "OD_entryList",
    "#ifndef OD_H",
)
GOLDEN_MARKERS_C = (
    "0x1000",
    "0x1018",
    "ODA_SDO_R",
)


def test_codegen_golden_minimal():
    doc = edsparse.empty_document()
    files = generate(doc, "canopennode_v4", node_name="Top3")
    assert set(files) == {"OD.h", "OD.c"}
    h, c = files["OD.h"], files["OD.c"]
    for m in GOLDEN_MARKERS_H:
        assert m in h, m
    for m in GOLDEN_MARKERS_C:
        assert m in c, m
    # Unsupported items documented by absence of block SDO / FD markers
    assert "CANopen FD" not in h
    assert "SRDO" not in c


def test_codegen_golden_with_pdo():
    doc = edsparse.empty_document()
    doc.entries.append(edsparse.OdEntry(
        0x2000, 0, "App value", "0x0007", "0x0007", "rw", "0", "0"))
    doc.entries.append(edsparse.OdEntry(
        0x1A00, 0, "TPDO1 map", "0x9", "", "",
        extra={"SubNumber": "2"}))
    doc.entries.append(edsparse.OdEntry(
        0x1A00, 1, "Mapped object 1", "0x7", "0x0007", "rw",
        "0x20000020", "1"))
    files = generate(doc, "canopennode_v4", node_name="Top3")
    assert "0x1A00" in files["OD.c"]
    assert "0x2000" in files["OD.c"]


if __name__ == "__main__":
    test_codegen_golden_minimal()
    test_codegen_golden_with_pdo()
    print("PASS codegen_golden")
