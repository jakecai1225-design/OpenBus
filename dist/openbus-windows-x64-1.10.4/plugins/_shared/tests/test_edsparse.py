# -*- coding: utf-8 -*-
"""edsparse smoke + DCF / deep validate tests."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from _shared.edsparse import (  # noqa: E402
    diff_documents,
    empty_document,
    export_document,
    export_eds_text,
    parse_eds,
    parse_eds_document,
    validate_document,
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

[1001]
ParameterName=Error register
ObjectType=0x7
DataType=0x0005
AccessType=ro
DefaultValue=0x00

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

DCF_SAMPLE = """
[FileInfo]
FileName=Node.dcf

[DeviceInfo]
VendorName=Demo
ProductName=Node

[DeviceCommissioning]
NodeID=5
BaudRate=500
NodeName=Axis1

[1000]
ParameterName=Device type
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00020192
ParameterValue=0x00020192

[1001]
ParameterName=Error register
ObjectType=0x7
DataType=0x0005
AccessType=ro
DefaultValue=0x00

[1018]
ParameterName=Identity object
ObjectType=0x8

[1600]
ParameterName=Receive PDO mapping
ObjectType=0x8
SubNumber=2

[1600sub0]
ParameterName=Number of entries
ObjectType=0x7
DataType=0x0005
AccessType=rw
DefaultValue=1

[1600sub1]
ParameterName=Mapping entry 1
ObjectType=0x7
DataType=0x0007
AccessType=rw
DefaultValue=0x60410010
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


def test_dcf_commissioning_and_param():
    doc = parse_eds_document(DCF_SAMPLE, path="Node.dcf")
    assert doc.is_dcf
    assert doc.device_commissioning.get("NodeID") == "5"
    e1000 = doc.entry(0x1000)
    assert e1000 and e1000.parameter_value == "0x00020192"
    text = export_document(doc, as_dcf=True)
    again = parse_eds_document(text)
    assert again.device_commissioning.get("BaudRate") == "500"
    assert again.entry(0x1000).parameter_value == "0x00020192"
    print("PASS DCF ParameterValue + DeviceCommissioning")


def test_deep_pdo_map_missing():
    doc = parse_eds_document(DCF_SAMPLE)
    findings = validate_document(doc, deep=True)
    # 0x6041:00 not in OD → pdo_map_ref error
    rules = {f.get("rule") for f in findings}
    assert "pdo_map_ref" in rules
    print("PASS deep PDO map reference check")


def test_empty_and_diff():
    a = empty_document()
    b = parse_eds_document(SAMPLE)
    rows = diff_documents(a, b)
    assert any(r["kind"] in ("added", "changed", "removed") for r in rows)
    print("PASS empty + diff (%d rows)" % len(rows))


if __name__ == "__main__":
    test_parse_identity()
    test_roundtrip_meta()
    test_dcf_commissioning_and_param()
    test_deep_pdo_map_missing()
    test_empty_and_diff()
    print("All edsparse tests passed")
