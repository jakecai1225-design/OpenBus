# -*- coding: utf-8 -*-
"""Protocol tests for EtherCAT Suite (no Qt)."""

from __future__ import annotations

import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from core.al_state import decode_status, request_state
from core.coe import parse_sdo, sdo_download_request, sdo_upload_request
from core.datagram import Datagram, encode_frame, parse_frame
from core.dc import cable_delay_ns, shift_ns, system_delay_ns
from core.esi import parse_esi, serialize_esi, validate_esi
from core.pdo import pack_entries, unpack_entries


def test_datagram_roundtrip():
    dgs = [
        Datagram(1, 1, 0, 0x130, b"\x11\x22", 0, 1),
        Datagram(11, 2, 0x1000, 0, b"\xAA", 0, 3),
    ]
    raw = encode_frame(dgs)
    frame = parse_frame(raw)
    assert frame.frame_type == 1
    assert len(frame.datagrams) == 2
    assert frame.datagrams[0].cmd_name == "APRD"
    assert frame.datagrams[0].ado == 0x130
    assert frame.datagrams[0].data == b"\x11\x22"
    assert frame.datagrams[1].cmd_name == "LWR"
    assert frame.datagrams[1].wkc == 3


def test_ethernet_prefix():
    body = encode_frame([Datagram(7, 0, 0, 0, b"", 0, 0)])
    eth = b"\x00" * 12 + b"\x88\xA4" + body
    frame = parse_frame(eth)
    assert frame.datagrams[0].cmd_name == "BRD"


def test_esi_and_pdo():
    path = os.path.join(_ROOT, "samples", "demo_slave.xml")
    dev = parse_esi(path)
    assert dev.vendor_id == 2
    assert dev.product_code == 0x10
    assert dev.rx_pdos[0].entries[0].name == "Controlword"
    assert len(dev.objects) >= 4
    entries = dev.rx_pdos[0].entries
    blob = pack_entries(entries, [0x1234, 0x00AB])
    got = unpack_entries(blob, entries)
    assert got[0][1] == 0x1234 and got[1][1] == 0x00AB


def test_esi_roundtrip():
    path = os.path.join(_ROOT, "samples", "demo_slave.xml")
    dev = parse_esi(path)
    xml = serialize_esi(dev)
    with tempfile.NamedTemporaryFile(
            "w", suffix=".xml", delete=False, encoding="utf-8") as f:
        f.write(xml)
        tmp = f.name
    try:
        again = parse_esi(tmp)
    finally:
        os.unlink(tmp)
    assert again.name == dev.name
    assert again.vendor_id == dev.vendor_id
    assert again.product_code == dev.product_code
    assert len(again.rx_pdos[0].entries) == len(dev.rx_pdos[0].entries)
    assert validate_esi(dev)


def test_coe():
    req = sdo_upload_request(0x1000, 0)
    info = parse_sdo(req)
    assert info["ok"] and info["index"] == 0x1000 and info["cs"] == 0x40
    down = sdo_download_request(0x7000, 1, 0x1234, size=2)
    info = parse_sdo(down)
    assert info["index"] == 0x7000 and info["subindex"] == 1
    assert info["data"][0] == 0x34 and info["data"][1] == 0x12


def test_al_and_dc():
    assert decode_status(0x08)["name"] == "OP"
    assert decode_status(0x14)["error"] and decode_status(0x14)["name"] == "SAFEOP"
    assert request_state("INIT", "OP")["ok"] is False
    assert request_state("INIT", "PREOP")["state"] == "PREOP"
    assert cable_delay_ns(2) == 10.0
    assert system_delay_ns([1, 1]) == [5.0, 10.0]
    assert shift_ns(1_000_000, 64, 100) > 0


if __name__ == "__main__":
    test_datagram_roundtrip()
    test_ethernet_prefix()
    test_esi_and_pdo()
    test_esi_roundtrip()
    test_coe()
    test_al_and_dc()
    print("ethercat-suite core tests passed")
