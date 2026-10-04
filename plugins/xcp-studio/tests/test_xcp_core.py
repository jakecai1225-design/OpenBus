# -*- coding: utf-8 -*-
"""XCP CTO encode/decode + A2L model + shell routes."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_PLUGIN)
for p in (_PLUGIN, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from core.a2l_model import decode_raw, encode_value, datatype_size  # noqa: E402
from core.xcp_client import (  # noqa: E402
    decode_cto,
    encode_connect,
    encode_download,
    encode_set_cal_page,
    encode_short_upload,
    encode_start_stop_synch,
    PID_ERR,
    PID_RES,
    CMD_CONNECT,
    CMD_SHORT_UPLOAD,
)


def test_encode_connect():
    pdu = encode_connect(0)
    assert pdu[0] == CMD_CONNECT
    assert len(pdu) == 2


def test_encode_short_upload():
    pdu = encode_short_upload(4, 0, 0x1000)
    assert pdu[0] == CMD_SHORT_UPLOAD
    assert pdu[1] == 4
    assert pdu[4:8] == bytes([0x00, 0x10, 0x00, 0x00])


def test_encode_download_and_page():
    pdu = encode_download(b"\x01\x02")
    assert pdu[0] == 0xF0
    assert pdu[1] == 2
    assert len(pdu) == 8
    page = encode_set_cal_page(0x03, 0, 1)
    assert page[-1] == 1
    syn = encode_start_stop_synch(1)
    assert syn == bytes([0xDD, 1])


def test_decode_cto():
    kind, fields = decode_cto(bytes([PID_RES, 0x11, 0x22]))
    assert kind == "res"
    assert fields["payload"] == b"\x11\x22"
    kind, fields = decode_cto(bytes([PID_ERR, 0x10]))
    assert kind == "err"
    assert fields["code"] == 0x10
    kind, fields = decode_cto(bytes([0x01, 0xAA, 0xBB]))
    assert kind == "daq"


def test_value_codec():
    raw = encode_value(42, "ULONG")
    assert decode_raw(raw, "ULONG") == 42.0
    assert datatype_size("UWORD") == 2
    fraw = encode_value(1.5, "FLOAT32_IEEE")
    assert abs(decode_raw(fraw, "FLOAT32_IEEE") - 1.5) < 1e-6


def test_shell_routes():
    src = open(os.path.join(_PLUGIN, "app_shell.py"), encoding="utf-8").read()
    assert '"record": ("measure", 1)' in src
    assert '"setup": ("live", 0)' in src
    assert "xcp.open_a2l" in src
    assert "a2l_handoff.json" in src
    assert "export_mdf" in src


if __name__ == "__main__":
    test_encode_connect()
    test_encode_short_upload()
    test_encode_download_and_page()
    test_decode_cto()
    test_value_codec()
    test_shell_routes()
    print("PASS xcp-studio core")
