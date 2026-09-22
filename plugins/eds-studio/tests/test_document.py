# -*- coding: utf-8 -*-
"""EDS Studio document round-trip smoke test."""

from __future__ import annotations

import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from _shared import edsparse  # noqa: E402
from document import EdsDocument  # noqa: E402


def test_document_undo_save():
    doc = EdsDocument()
    doc.new()
    assert len(doc.eds.entries) >= 3
    n0 = len(doc.eds.entries)
    doc.upsert_entries([
        edsparse.OdEntry(
            0x2000, 0, "Custom", "0x7", "0x0007", "rw", "0"),
    ], merge=True)
    assert doc.dirty
    assert len(doc.eds.entries) == n0 + 1
    assert doc.undo()
    assert len(doc.eds.entries) == n0
    assert doc.redo()
    assert len(doc.eds.entries) == n0 + 1

    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "t.eds")
        doc.save(path)
        assert not doc.dirty
        again = EdsDocument()
        again.load(path)
        assert again.entry_count() if hasattr(again, "entry_count") else True
        assert len(again.eds.entries) == len(doc.eds.entries)
        assert any(e.index == 0x2000 for e in again.eds.entries)
    print("PASS EdsDocument undo/save/load")


def test_dcf_roundtrip():
    doc = EdsDocument()
    doc.new()
    eds = doc.clone_eds()
    eds.is_dcf = True
    eds.device_commissioning = {
        "NodeID": "7", "BaudRate": "250", "NodeName": "Axis"}
    e = eds.entry(0x1000)
    if e:
        e.parameter_value = "0x00020192"
    doc.apply_eds(eds)
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "n.dcf")
        doc.save(path, as_dcf=True)
        text = open(path, encoding="utf-8").read()
        assert "[DeviceCommissioning]" in text
        assert "ParameterValue=" in text
        loaded = edsparse.parse_eds_file_document(path)
        assert loaded.device_commissioning.get("NodeID") == "7"
    print("PASS DCF save round-trip")


if __name__ == "__main__":
    test_document_undo_save()
    test_dcf_roundtrip()
    print("All eds-studio tests passed")
