# -*- coding: utf-8 -*-
"""Segmented SDO + NetworkHealth + LSS + PDO link unit tests (no Qt bus)."""

from __future__ import annotations

import os
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

# Minimal QCoreApplication for QTimer in SdoClient
from PyQt6.QtCore import QCoreApplication  # noqa: E402

_app = QCoreApplication.instance() or QCoreApplication([])

from core.lss_master import (  # noqa: E402
    encode_configure_node_id,
    encode_switch_global,
    decode_lss_slave,
)
from core.network_health import NetworkHealth  # noqa: E402
from core.pdo_link import make_link, validate_links, apply_links_to_entries  # noqa: E402
from core.sdo_client import (  # noqa: E402
    SdoClient,
    decode_sdo_response,
    encode_download_segment,
    encode_expedited_upload,
    encode_segmented_download_init,
    encode_upload_segment,
)


def test_decode_abort():
    # Abort 0x06020000
    data = bytes([0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x06])
    kind, fields = decode_sdo_response(data)
    assert kind == "abort"
    assert fields["abort"] == 0x06020000
    assert "Object does not exist" in fields["message"]


def test_segmented_upload_roundtrip():
    sent = []

    def send_fn(cid, data):
        sent.append((cid, bytes(data)))

    client = SdoClient(send_fn, timeout_ms=5000)
    client.set_node(5)
    result = {"ok": None, "value": None, "note": ""}

    def done(ok, value, note):
        result["ok"] = ok
        result["value"] = value
        result["note"] = note

    assert client.upload(0x1008, 0, on_done=done)
    assert sent and sent[0][0] == 0x605
    assert sent[0][1][0] == 0x40

    # Server initiate segmented upload, size=10
    init = bytes([0x41, 0x08, 0x10, 0x00, 10, 0, 0, 0])
    client.on_frame(0x585, init)
    assert any(b[1][0] & 0xE0 == 0x60 for b in sent[1:])  # upload segment req

    # Segment 0: 7 bytes, not last, toggle 0
    seg0 = bytes([0x00]) + b"ABCDEFG"
    client.on_frame(0x585, seg0)
    # Segment 1: 3 bytes, last, toggle 1 → n=4 → cmd = 0x01|0x10|(4<<1)=0x19
    seg1 = bytes([0x19]) + b"HIJ\x00\x00\x00\x00"
    client.on_frame(0x585, seg1)

    assert result["ok"] is True
    assert result["value"] == b"ABCDEFGHIJ"


def test_segmented_download_init_encode():
    pdu = encode_segmented_download_init(0x2000, 1, 20)
    assert pdu[0] == 0x21
    assert pdu[1] == 0x00 and pdu[2] == 0x20 and pdu[3] == 1
    assert pdu[4] == 20


def test_download_segment_toggle():
    pdu = encode_download_segment(1, b"abc", last=True)
    assert (pdu[0] >> 4) & 1 == 1
    assert pdu[0] & 1 == 1
    assert pdu[1:4] == b"abc"


def test_network_health_hb_emcy():
    h = NetworkHealth(hb_timeout_s=0.05)
    note = h.on_frame(0x705, bytes([0x05]), now=100.0)
    assert note and "Operational" in note
    assert h.nodes[5].nmt_label == "Operational"
    # EMCY
    emcy = bytes([0x30, 0x81, 0x01, 0, 0, 0, 0, 0])
    note2 = h.on_frame(0x085, emcy, now=100.1)
    assert note2 and "EMCY" in note2
    assert h.recent_emcy(1)[0].error_code == 0x8130
    # Timeout
    newly = h.poll_timeouts(now=100.2)
    assert newly == [5]
    assert h.nodes[5].missed is True


def test_lss_encode_decode():
    assert encode_switch_global(1)[0] == 0x04
    assert encode_configure_node_id(42)[1] == 42
    kind, fields = decode_lss_slave(bytes([0x5E, 7, 0, 0, 0, 0, 0, 0]))
    assert kind == "inquire_node_id"
    assert fields["node_id"] == 7


def test_pdo_link_mvp():
    lk = make_link(1, 0x2000, 0, 2, 1, 1, 16)
    assert lk.mapping_dword() == ((0x2000 << 16) | (0 << 8) | 16)
    bad = make_link(1, 0x2000, 0, 2, 1, 1, 40)
    bad2 = make_link(1, 0x2001, 0, 2, 1, 2, 40)
    issues = validate_links([bad, bad2])
    assert any("64" in x for x in issues)
    dup = make_link(1, 0x2000, 0, 2, 1, 1, 8)
    dup2 = make_link(3, 0x2001, 0, 2, 1, 1, 8)
    assert validate_links([dup, dup2])


def test_pdo_link_apply():
    class E:
        def __init__(self, index, sub, default="0"):
            self.index = index
            self.subindex = sub
            self.default_value = default
            self.parameter_value = default

    entries = [
        E(0x1600, 0, "0"),
        E(0x1600, 1, "0"),
    ]
    lk = make_link(1, 0x6041, 0, 2, 1, 1, 16)
    n = apply_links_to_entries(entries, [lk])
    assert n >= 1
    assert entries[1].default_value == "0x%08X" % lk.mapping_dword()
    assert entries[0].default_value == "1"


def test_expedited_still_works():
    sent = []
    client = SdoClient(lambda c, d: sent.append((c, bytes(d))), timeout_ms=2000)
    client.set_node(1)
    got = {}

    def done(ok, value, note):
        got["ok"] = ok
        got["value"] = value

    assert client.upload(0x1000, 0, on_done=done)
    # Expedited upload response: 4 bytes value 0x12345678
    resp = bytes([0x43, 0x00, 0x10, 0x00, 0x78, 0x56, 0x34, 0x12])
    client.on_frame(0x581, resp)
    assert got["ok"] is True
    assert got["value"] == 0x12345678


if __name__ == "__main__":
    test_decode_abort()
    test_segmented_upload_roundtrip()
    test_segmented_download_init_encode()
    test_download_segment_toggle()
    test_network_health_hb_emcy()
    test_lss_encode_decode()
    test_pdo_link_mvp()
    test_pdo_link_apply()
    test_expedited_still_works()
    print("PASS top3_core")
