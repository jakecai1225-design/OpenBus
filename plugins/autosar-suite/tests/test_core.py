# -*- coding: utf-8 -*-
"""Protocol tests for AUTOSAR Suite (no Qt)."""

from __future__ import annotations

import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from core.arxml_min import export_dbc, parse_arxml, serialize_arxml, validate_ipdus
from core.cannm import decode_nm, encode_nm
from core.e2e import p01_check, p01_protect, p02_check, p02_protect
from core.ipdu import pack_raw, unpack_raw
from core.secoc import build_secured, verify_secured


def test_intel_roundtrip():
    raw = pack_raw(b"\x00" * 8, 0, 8, "intel", 0xAB)
    assert raw[0] == 0xAB
    assert unpack_raw(raw, 0, 8, "intel") == 0xAB
    raw = pack_raw(b"\x00" * 8, 4, 12, "intel", 0xABC)
    assert unpack_raw(raw, 4, 12, "intel") == 0xABC


def test_motorola():
    raw = pack_raw(b"\x00" * 8, 7, 16, "motorola", 0x1234)
    assert raw[0] == 0x12 and raw[1] == 0x34
    assert unpack_raw(raw, 7, 16, "motorola") == 0x1234


def test_e2e_p01():
    protected = p01_protect(b"\x00\x00\x11\x22", 0x1234, 3)
    result = p01_check(protected, 0x1234)
    assert result["ok"] and result["counter"] == 3
    bad = bytearray(protected)
    bad[2] ^= 0xFF
    assert not p01_check(bytes(bad), 0x1234)["ok"]


def test_e2e_p02():
    ids = bytes(range(16))
    protected = p02_protect(b"\x00\x00\x10", ids, 5)
    assert p02_check(protected, ids)["ok"]


def test_nm():
    cid, payload = encode_nm(7, b"\x01\x02", repeat=True, active=True)
    pdu = decode_nm(cid, payload)
    assert pdu is not None
    assert pdu.node_id == 7 and pdu.repeat_request and pdu.user_data[:2] == b"\x01\x02"
    assert decode_nm(0x100, payload) is None


def test_secoc():
    frame = build_secured(b"\x11\x22", 0x5A, 8, 16, b"key")
    info = verify_secured(frame, 8, 16, b"key")
    assert info["mac_ok"] and info["freshness"] == 0x5A
    assert info["payload"] == b"\x11\x22"


def test_arxml():
    path = os.path.join(_ROOT, "samples", "body_can.xml")
    pdus = parse_arxml(path)
    assert len(pdus) == 1
    pdu = pdus[0]
    assert pdu.can_id == 256 and pdu.name == "BodyStatus"
    assert pdu.signals[0].name == "VehicleSpeed"
    assert pdu.signals[0].endian == "motorola"
    assert pdu.signals[1].endian == "intel"


def test_arxml_roundtrip():
    path = os.path.join(_ROOT, "samples", "body_can.xml")
    pdus = parse_arxml(path)
    xml = serialize_arxml(pdus)
    with tempfile.NamedTemporaryFile(
            "w", suffix=".arxml", delete=False, encoding="utf-8") as f:
        f.write(xml)
        tmp = f.name
    try:
        again = parse_arxml(tmp)
    finally:
        os.unlink(tmp)
    assert len(again) == 1
    assert again[0].name == pdus[0].name
    assert again[0].can_id == pdus[0].can_id
    assert again[0].signals[0].length == pdus[0].signals[0].length
    assert validate_ipdus(pdus)
    assert "BO_ 256 BodyStatus" in export_dbc(pdus)


if __name__ == "__main__":
    test_intel_roundtrip()
    test_motorola()
    test_e2e_p01()
    test_e2e_p02()
    test_nm()
    test_secoc()
    test_arxml()
    test_arxml_roundtrip()
    print("autosar-suite core tests passed")
