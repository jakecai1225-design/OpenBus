# -*- coding: utf-8 -*-
"""arxmlparse smoke tests."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SHARED = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SHARED)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)

from _shared import arxmlparse  # noqa: E402


def test_roundtrip():
    import xml.etree.ElementTree as ET
    model = arxmlparse.empty_model()
    text = arxmlparse.serialize_model(model)
    root = ET.fromstring(
        text.replace('<?xml version="1.0" encoding="UTF-8"?>\n', ""))
    again = arxmlparse.parse_root(root)
    assert len(again) == 1
    assert again[0].name == "DemoPdu"
    assert again[0].signals[0].name == "DemoSignal"
    print("PASS serialize/parse round-trip")


def test_validate_overlap():
    model = arxmlparse.empty_model()
    model.ipdus[0].signals.append(
        arxmlparse.Signal("Clash", 0, 8))  # overlaps DemoSignal bits
    findings = arxmlparse.validate_model(model, deep=True)
    rules = {f["rule"] for f in findings}
    assert "bit_overlap" in rules
    assert any(f.get("fix") for f in findings if f["rule"] == "bit_overlap")
    print("PASS deep validate + fix hint")


def test_spec_glossary():
    rows = arxmlparse.search_spec("START-POSITION")
    assert rows
    tip = arxmlparse.tip_for_field("i-signal.length")
    assert tip and "bits" in tip["summary"].lower()
    print("PASS spec glossary (%d entries)" % len(arxmlparse.SPEC_GLOSSARY))


def test_diff():
    a = arxmlparse.empty_model()
    b = a.clone()
    b.ipdus[0].can_id = 0x200
    b.ipdus.append(arxmlparse.Ipdu("Extra", 0x300, 8, []))
    rows = arxmlparse.diff_models(a, b)
    assert any(r["kind"] == "added" for r in rows)
    assert any(r["kind"] == "changed" for r in rows)
    print("PASS diff (%d rows)" % len(rows))


if __name__ == "__main__":
    test_roundtrip()
    test_validate_overlap()
    test_spec_glossary()
    test_diff()
    print("All arxmlparse tests passed")
