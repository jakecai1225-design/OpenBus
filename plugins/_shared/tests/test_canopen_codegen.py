# -*- coding: utf-8 -*-
"""Tests for canopen_codegen (CANopenNode V4 + CanFestival)."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGINS = os.path.abspath(os.path.join(_HERE, "..", ".."))
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)

from _shared import edsparse  # noqa: E402
from _shared.canopen_codegen import TARGETS, generate, preflight  # noqa: E402
from _shared.canopen_codegen.types import (  # noqa: E402
    cf_type,
    ctype_info,
    parse_default,
)


def _minimal_doc():
    return edsparse.empty_document()


def test_targets_listed():
    ids = {t[0] for t in TARGETS}
    assert "canopennode_v4" in ids
    assert "canfestival" in ids


def test_ctype_mapping():
    assert ctype_info("0x0007").c_type == "uint32_t"
    assert ctype_info("UNSIGNED8").bit_length == 8
    assert cf_type("0x0005") == "UNS8"
    assert parse_default("0x1234", "0x0007") == "0x1234"


def test_preflight_empty():
    doc = edsparse.EdsDocument()
    bad = preflight(doc)
    assert bad and bad[0]["rule"] == "empty"


def test_canopennode_v4_symbols():
    doc = _minimal_doc()
    files = generate(doc, "canopennode_v4", node_name="Demo")
    assert set(files) == {"OD.h", "OD.c"}
    h, c = files["OD.h"], files["OD.c"]
    assert "OD_RAM_t" in h
    assert "OD_entryList" in h
    assert "0x1000" in c
    assert "0x1018" in c
    assert "ODA_SDO_R" in c


def test_canfestival_symbols():
    doc = _minimal_doc()
    files = generate(doc, "canfestival", node_name="Slave")
    assert "Slave.h" in files and "Slave.c" in files
    h, c = files["Slave.h"], files["Slave.c"]
    assert "Slave_objdict" in h
    assert "Slave_scanIndexOD" in h
    assert "Slave_Data" in c
    assert "CANOPEN_NODE_DATA_INITIALIZER(Slave)" in c
    assert "case 0x1000:" in c


def test_pdo_entry():
    doc = _minimal_doc()
    doc.entries.append(edsparse.OdEntry(
        0x1400, 0, "RPDO1 comm", "0x9", "", "",
        extra={"SubNumber": "2"}))
    doc.entries.append(edsparse.OdEntry(
        0x1400, 1, "COB-ID", "0x7", "0x0007", "rw", "0x200", "1"))
    files = generate(doc, "canopennode_v4", strict=True)
    assert "0x1400" in files["OD.c"]
    assert "ODA_RPDO" in files["OD.c"] or "ODA_SDO_W" in files["OD.c"]


if __name__ == "__main__":
    test_targets_listed()
    test_ctype_mapping()
    test_preflight_empty()
    test_canopennode_v4_symbols()
    test_canfestival_symbols()
    test_pdo_entry()
    print("ok")
