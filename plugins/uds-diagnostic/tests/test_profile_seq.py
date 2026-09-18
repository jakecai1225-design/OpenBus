# -*- coding: utf-8 -*-
"""Unit tests for uds-diagnostic profile_seq helpers."""

import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from profile_seq import (
    default_profile, load_profile_file, save_profile_file,
    load_sequence, parse_pdu_hex,
)


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)
    print("  PASS", msg)


print("[profile_seq] defaults")
p = default_profile()
check(p["tx_id"] == 0x7E0 and p["rx_id"] == 0x7E8, "default IDs")

print("[profile_seq] save/load")
with tempfile.TemporaryDirectory() as td:
    path = os.path.join(td, "p.json")
    p["name"] = "TestECU"
    p["tx_id"] = 0x7E1
    save_profile_file(path, p)
    loaded = load_profile_file(path)
    check(loaded["name"] == "TestECU" and loaded["tx_id"] == 0x7E1, "round-trip")

print("[profile_seq] sequence JSON/CSV")
with tempfile.TemporaryDirectory() as td:
    jpath = os.path.join(td, "s.json")
    with open(jpath, "w", encoding="utf-8") as f:
        json.dump([{"tag": "ext", "pdu_hex": "10 03", "expect_response": True}], f)
    steps = load_sequence(jpath)
    check(len(steps) == 1 and steps[0]["tag"] == "ext", "json steps")
    cpath = os.path.join(td, "s.csv")
    with open(cpath, "w", encoding="utf-8", newline="") as f:
        f.write("tag,pdu_hex,expect_response\n")
        f.write("vin,22 F1 90,1\n")
    steps = load_sequence(cpath)
    check(len(steps) == 1 and parse_pdu_hex(steps[0]["pdu_hex"]) == bytes([0x22, 0xF1, 0x90]),
          "csv pdu")

print("All profile_seq tests passed")
