# -*- coding: utf-8 -*-
"""EDS parse smoke test for CANopen Suite."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from core.eds_parse import (  # noqa: E402
    export_eds_text,
    parse_eds,
    parse_eds_document,
    validate_eds,
)

SAMPLE = """
[FileInfo]
FileName=Sample.eds
CreatedBy=test

[DeviceInfo]
VendorName=Demo
ProductName=Node

[1000]
ParameterName=Device type
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00020192

[1018]
ParameterName=Identity object
ObjectType=0x8
SubNumber=4

[1018sub1]
ParameterName=Vendor-ID
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00000000
"""


def test_parse_identity():
    entries = parse_eds(SAMPLE)
    idxs = {e.index for e in entries}
    assert 0x1000 in idxs
    assert 0x1018 in idxs
    vendor = [e for e in entries if e.index == 0x1018 and e.subindex == 1]
    assert vendor and vendor[0].name == "Vendor-ID"
    print("PASS eds parse identity/device type (%d entries)" % len(entries))


def test_roundtrip_meta():
    doc = parse_eds_document(SAMPLE)
    text = export_eds_text(
        doc.entries,
        file_info=doc.file_info,
        device_info=doc.device_info,
    )
    again = parse_eds_document(text)
    assert again.file_info.get("CreatedBy") == "test"
    assert again.device_info.get("VendorName") == "Demo"
    assert len(again.entries) == len(doc.entries)
    listed = again.other_meta.get("mandatoryobjects", {})
    vals = {v.lower() for v in listed.values()}
    assert "0x1000" in vals
    assert "0x1018" in vals
    findings = validate_eds(doc.entries)
    assert findings and findings[0]["level"] in ("info", "warn", "error")
    print("PASS eds FileInfo round-trip + validate")


if __name__ == "__main__":
    test_parse_identity()
    test_roundtrip_meta()
    print("All canopen-suite eds tests passed")
