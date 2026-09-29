# -*- coding: utf-8 -*-
"""Unit tests for EDS-driven CANopen bus decode (no Qt / sin)."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)

from core.cia301_codes import (  # noqa: E402
    emcy_error_text, sdo_abort_text, statusword_state,
)
from core.cob_classify import classify_cob  # noqa: E402
from core.eds_decode import (  # noqa: E402
    EdsBusDecoder, MappedObject, PdoLayout, build_pdo_layouts,
    decode_pdo_payload, extract_bits,
)
from core.eds_parse import OdEntry  # noqa: E402


def test_extract_bits_intel():
    data = bytes([0x34, 0x12, 0x00, 0x00])
    assert extract_bits(data, 0, 16) == 0x1234
    assert extract_bits(data, 0, 8) == 0x34


def test_pdo_layout_from_eds_defaults():
    entries = [
        OdEntry(0x1800, 1, name="COB-ID", default_value="0x185"),
        OdEntry(0x1A00, 0, name="Number of entries", default_value="2"),
        OdEntry(0x1A00, 1, name="Mapping 1",
                default_value="0x60410010"),  # Statusword 16-bit
        OdEntry(0x1A00, 2, name="Mapping 2",
                default_value="0x606C0020"),  # Velocity 32-bit
        OdEntry(0x6041, 0, name="Statusword", data_type="0x0006"),
        OdEntry(0x606C, 0, name="Velocity actual value", data_type="0x0004"),
    ]
    layouts = build_pdo_layouts(entries, node_id=5)
    assert 0x185 in layouts
    lay = layouts[0x185]
    assert lay.kind == "TPDO1"
    assert len(lay.objects) == 2
    assert lay.objects[0].index == 0x6041
    assert lay.objects[0].bit_length == 16
    assert lay.objects[1].index == 0x606C
    assert lay.objects[1].bit_offset == 16


def test_decode_pdo_payload_values():
    lay = PdoLayout(
        cob_id=0x185, kind="TPDO1", node_id=5,
        objects=[
            MappedObject(0x6041, 0, "Statusword", 0, 16, signed=False),
            MappedObject(0x606C, 0, "Velocity", 16, 32, signed=True),
        ],
    )
    # Statusword=0x1637 (Operation enabled), Velocity=100
    data = (0x1637).to_bytes(2, "little") + (100).to_bytes(4, "little", signed=True)
    vals = decode_pdo_payload(lay, data)
    assert vals[0].raw == 0x1637
    assert "Operation enabled" in vals[0].text
    assert vals[1].value == 100


def test_decoder_sdo_and_nmt():
    entries = [
        OdEntry(0x1018, 1, name="Vendor ID", data_type="0x0007"),
    ]
    dec = EdsBusDecoder()
    dec.rebuild(entries, 1)
    ix = dec.decode(0x000, bytes([0x01, 0x00]))
    assert "Start" in ix.summary
    ix = dec.decode(0x581, bytes([0x43, 0x18, 0x10, 0x01, 0x34, 0x12, 0x00, 0x00]))
    assert "Vendor ID" in ix.summary
    assert ix.layer == "application"


def test_sdo_abort_text():
    entries = [OdEntry(0x6040, 0, name="Controlword", data_type="0x0006")]
    dec = EdsBusDecoder()
    dec.rebuild(entries, 1)
    # Abort: object does not exist
    ix = dec.decode(0x581, bytes([
        0x80, 0x40, 0x60, 0x00,
        0x00, 0x00, 0x02, 0x06,
    ]))
    assert "does not exist" in ix.summary.lower() or "Object does not exist" in ix.summary
    assert sdo_abort_text(0x06020000).startswith("Object does not exist")


def test_emcy_and_sync_time_lss():
    dec = EdsBusDecoder()
    dec.rebuild([], 1)
    ix = dec.decode(0x081, bytes([0x30, 0x81, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00]))
    assert "Life guard" in ix.summary or "heartbeat" in ix.summary.lower()
    assert emcy_error_text(0x8130).startswith("Life guard")
    ix = dec.decode(0x080, bytes([0x03]))
    assert "counter=3" in ix.summary
    # TIME: 1h + 2 days → ms=3600000
    ms = 3600000
    payload = bytes([
        ms & 0xFF, (ms >> 8) & 0xFF, (ms >> 16) & 0xFF, (ms >> 24) & 0x0F,
        0x02, 0x00,
    ])
    ix = dec.decode(0x100, payload)
    assert "01:00:00" in ix.summary
    assert classify_cob(0x7E5)[0] == "LSS_M"
    ix = dec.decode(0x7E5, bytes([0x04, 0x01]))
    assert "configuration" in ix.summary


def test_segmented_sdo_reassembly():
    entries = [
        OdEntry(0x1008, 0, name="Manufacturer device name", data_type="0x0009"),
    ]
    dec = EdsBusDecoder()
    dec.rebuild(entries, 1)
    # Initiate upload segmented, size=10, e=0 s=1 → cmd=0x41
    ix = dec.decode(0x581, bytes([0x41, 0x08, 0x10, 0x00, 10, 0, 0, 0]))
    assert "initiate" in ix.summary.lower()
    # Segment 1: t=0, 7 bytes
    data1 = b"ABCDEFG"
    ix = dec.decode(0x581, bytes([0x00]) + data1)
    assert "segment" in ix.summary.lower()
    # Last segment: t=1, c=1, n=4 unused → 3 bytes
    # cmd = c | (n<<1) | (t<<4) = 1 | (4<<1) | (1<<4) = 1|8|16 = 0x19
    ix = dec.decode(0x581, bytes([0x19, ord("H"), ord("I"), ord("J"), 0, 0, 0, 0]))
    assert "segmented" in ix.summary.lower()
    assert ix.values
    assert b"ABCDEFGHIJ" in ix.values[0].value


def test_multi_node_layouts():
    e1 = [
        OdEntry(0x1800, 1, name="COB-ID", default_value="0x181"),
        OdEntry(0x1A00, 0, name="n", default_value="1"),
        OdEntry(0x1A00, 1, name="m", default_value="0x60410010"),
        OdEntry(0x6041, 0, name="Statusword", data_type="0x0006"),
    ]
    e2 = [
        OdEntry(0x1800, 1, name="COB-ID", default_value="0x182"),
        OdEntry(0x1A00, 0, name="n", default_value="1"),
        OdEntry(0x1A00, 1, name="m", default_value="0x60410010"),
        OdEntry(0x6041, 0, name="Statusword", data_type="0x0006"),
    ]
    dec = EdsBusDecoder()
    dec.rebuild(e1, 1)
    dec.add_node(e2, 2)
    assert dec.layout_count() == 2
    ix = dec.decode(0x182, (0x0021).to_bytes(2, "little"))
    assert ix.node_id == 2
    assert ix.layer == "application"


def test_statusword_state():
    assert statusword_state(0x1637) == "Operation enabled"
    assert statusword_state(0x0040) == "Switch on disabled"


def test_block_sdo_download():
    entries = [
        OdEntry(0x1008, 0, name="Device name", data_type="0x0009"),
    ]
    dec = EdsBusDecoder()
    dec.rebuild(entries, 1)
    # Client initiate block download size=10
    ix = dec.decode(0x601, bytes([0xC2, 0x08, 0x10, 0x00, 10, 0, 0, 0]))
    assert "block write initiate" in ix.summary.lower()
    # Server ACK blksize=2
    ix = dec.decode(0x581, bytes([0xA0, 0x08, 0x10, 0x00, 2, 0, 0, 0]))
    assert "block write ACK" in ix.summary
    # Two segments: seq1 + last seq2
    ix = dec.decode(0x601, bytes([0x01]) + b"ABCDEFG")
    assert "seq=1" in ix.summary
    ix = dec.decode(0x601, bytes([0x82]) + b"HIJ\x00\x00\x00\x00")
    assert "end-of-block" in ix.summary.lower()
    # Client end: ccs=6 ss=1 n=4 (7-3=4 unused in last conceptual — we set n=4)
    # Actually last segment already had 7 bytes of which 3 useful; n on end = unused in last seq
    ix = dec.decode(0x601, bytes([0xC1 | (4 << 2), 0, 0, 0, 0, 0, 0, 0]))
    assert "block end" in ix.summary.lower()
    # Server finalize scs=5 ss=1
    ix = dec.decode(0x581, bytes([0xA1, 0x08, 0x10, 0x00, 0, 0, 0, 0]))
    assert "[block]" in ix.summary
    assert ix.values


def test_guarding_vs_heartbeat():
    dec = EdsBusDecoder()
    dec.rebuild([], 1)
    # Heartbeat: bit7 always 0
    ix = dec.decode(0x701, bytes([0x05]))
    assert "HB" in ix.kind or "Guard" in ix.summary
    ix = dec.decode(0x701, bytes([0x05]))
    assert ix.kind == "HB"
    assert "Heartbeat" in ix.summary
    # Guarding: toggle alternates
    dec2 = EdsBusDecoder()
    dec2.rebuild([], 2)
    ix = dec2.decode(0x702, bytes([0x05]))       # t=0
    ix = dec2.decode(0x702, bytes([0x85]))       # t=1
    assert ix.kind == "GUARD"
    assert "Guarding" in ix.summary


def test_protocol_monitor_sync_gap_and_hb_late():
    dec = EdsBusDecoder()
    entries = [OdEntry(0x1017, 0, name="Producer heartbeat", default_value="100")]
    dec.rebuild(entries, 1)
    ix = dec.decode(0x080, bytes([0x01]), ts=1.0)
    assert not ix.anomalies or "SYNC" not in "".join(ix.anomalies)
    ix = dec.decode(0x080, bytes([0x05]), ts=1.1)  # gap
    assert any("SYNC gap" in a for a in ix.anomalies)
    ix = dec.decode(0x701, bytes([0x05]), ts=2.0)
    ix = dec.decode(0x701, bytes([0x05]), ts=2.3)  # 300ms > 1.5*100
    assert any("HB late" in a for a in ix.anomalies)


if __name__ == "__main__":
    test_extract_bits_intel()
    test_pdo_layout_from_eds_defaults()
    test_decode_pdo_payload_values()
    test_decoder_sdo_and_nmt()
    test_sdo_abort_text()
    test_emcy_and_sync_time_lss()
    test_segmented_sdo_reassembly()
    test_multi_node_layouts()
    test_statusword_state()
    test_block_sdo_download()
    test_guarding_vs_heartbeat()
    test_protocol_monitor_sync_gap_and_hb_late()
    print("PASS eds_decode")
