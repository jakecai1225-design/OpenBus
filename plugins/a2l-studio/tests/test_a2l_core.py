# -*- coding: utf-8 -*-
"""A2L parse / validate / compare / handoff (no Qt)."""

from __future__ import annotations

import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_PLUGIN)
for p in (_PLUGIN, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from _shared.a2lparse import (  # noqa: E402
    compare_documents,
    export_slim_a2l,
    export_symbol_csv,
    parse_a2l_text,
    validate_document,
)
from document import A2lDocumentSession  # noqa: E402

SAMPLE = """
ASAP2_VERSION 1 71
/begin PROJECT Demo ""
  /begin MODULE ECU ""
    /begin MEASUREMENT rpm "Engine speed"
      ULONG NO_COMPU_METHOD 0 0 0 8000
      ECU_ADDRESS 0x1000
    /end MEASUREMENT
    /begin CHARACTERISTIC idle "Idle setpoint"
      VALUE NO_COMPU_METHOD 0 NO_RECORD_LAYOUT 0 0 0
      ECU_ADDRESS 0x2000
    /end CHARACTERISTIC
    /begin CHARACTERISTIC boost_map "Boost MAP"
      MAP NO_COMPU_METHOD 0 NO_RECORD_LAYOUT 0 0 0
      ECU_ADDRESS 0x3000
    /end CHARACTERISTIC
  /end MODULE
/end PROJECT
"""

SAMPLE_B = """
ASAP2_VERSION 1 71
/begin PROJECT Demo ""
  /begin MODULE ECU ""
    /begin MEASUREMENT rpm "Engine speed"
      ULONG NO_COMPU_METHOD 0 0 0 8000
      ECU_ADDRESS 0x1004
    /end MEASUREMENT
    /begin CHARACTERISTIC idle "Idle setpoint"
      VALUE NO_COMPU_METHOD 0 NO_RECORD_LAYOUT 0 0 0
      ECU_ADDRESS 0x2000
    /end CHARACTERISTIC
  /end MODULE
/end PROJECT
"""


def test_parse_symbols():
    doc = parse_a2l_text(SAMPLE)
    assert doc.find("MEASUREMENT", "rpm")
    assert doc.find("CHARACTERISTIC", "idle").address == 0x2000
    assert doc.find("CHARACTERISTIC", "boost_map").char_type.upper() == "MAP"


def test_validate_ok():
    doc = parse_a2l_text(SAMPLE)
    findings = validate_document(doc)
    errors = [f for f in findings if f["severity"] == "error"]
    assert errors == []


def test_compare_address():
    a = parse_a2l_text(SAMPLE)
    b = parse_a2l_text(SAMPLE_B)
    rows = compare_documents(a, b)
    kinds = {(r["change"], r["name"]) for r in rows}
    assert ("address", "rpm") in kinds
    assert ("removed", "boost_map") in kinds


def test_export_roundtrip():
    doc = parse_a2l_text(SAMPLE)
    text = export_slim_a2l(doc)
    again = parse_a2l_text(text)
    assert again.find("MEASUREMENT", "rpm")
    csv_rows = export_symbol_csv(doc)
    assert csv_rows[0][0] == "Kind"
    assert any(r[1] == "idle" for r in csv_rows[1:])


def test_handoff_and_save():
    sess = A2lDocumentSession()
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "demo.a2l")
        with open(path, "w", encoding="utf-8") as f:
            f.write(SAMPLE)
        assert sess.load_path(path)
        payload = sess.handoff_payload()
        assert payload["a2l_path"] == path
        assert payload["from_a2l_studio"] is True
        assert sess.save()


def test_shell_routes():
    src = open(os.path.join(_PLUGIN, "app_shell.py"), encoding="utf-8").read()
    assert '"compare": ("analyze", 1)' in src
    assert '"validate": ("analyze", 0)' in src
    assert "a2l.apply_xcp" in src
    assert "xcp-studio" in src


if __name__ == "__main__":
    test_parse_symbols()
    test_validate_ok()
    test_compare_address()
    test_export_roundtrip()
    test_handoff_and_save()
    test_shell_routes()
    print("PASS a2l-studio core")
