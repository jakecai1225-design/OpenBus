# -*- coding: utf-8 -*-
"""EDS parse smoke test for CANopen Suite."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)

from core.eds_parse import parse_eds  # noqa: E402

SAMPLE = """
[FileInfo]
FileName=Sample.eds

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


if __name__ == "__main__":
    test_parse_identity()
    print("All canopen-suite eds tests passed")
