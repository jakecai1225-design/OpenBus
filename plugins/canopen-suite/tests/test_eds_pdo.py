# -*- coding: utf-8 -*-
"""Unit tests for EDS PDO map helpers (no Qt UI)."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)
_ROOT = os.path.dirname(_SUITE)
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from core.eds_parse import OdEntry  # noqa: E402
from pages.eds_pdo import (  # noqa: E402
    _bits_for_entry,
    _classify,
    _mappable_entries,
    _pack_map,
    _parse_map,
    map_budget,
    would_exceed_pdo_bits,
)


def test_parse_pack_roundtrip():
    assert _parse_map("0x60410010") == (0x6041, 0x00, 0x10)
    assert _pack_map(0x6041, 0, 16) == "0x60410010"
    assert _parse_map("") == (0, 0, 0)
    assert _parse_map("junk") == (0, 0, 0)


def test_classify_pdo_ranges():
    assert _classify(0x1400) == ("RPDO", 1, "comm")
    assert _classify(0x1600) == ("RPDO", 1, "map")
    assert _classify(0x1800) == ("TPDO", 1, "comm")
    assert _classify(0x1A00) == ("TPDO", 1, "map")
    assert _classify(0x2000) is None


def test_bits_for_entry_dtype():
    e = OdEntry(0x6041, 0, name="Statusword", data_type="0x0006")
    assert _bits_for_entry(e) == 16
    e32 = OdEntry(0x606C, 0, name="Vel", data_type="0x0004")
    assert _bits_for_entry(e32) == 32


def test_mappable_skips_pdo_area_and_shells():
    entries = [
        OdEntry(0x1600, 1, name="Map1", default_value="0x60410010"),
        OdEntry(0x6040, 0, name="Controlword", data_type="0x0006",
                object_type="0x7", pdo_mapping="yes"),
        OdEntry(0x6041, 0, name="Statusword", data_type="0x0006",
                object_type="0x7", pdo_mapping="yes"),
        OdEntry(0x2000, 0, name="Record", object_type="0x9"),
    ]
    mapped = _mappable_entries(entries)
    keys = [(e.index, e.subindex) for e in mapped]
    assert (0x1600, 1) not in keys
    assert (0x2000, 0) not in keys
    assert (0x6040, 0) in keys
    # PDO-flagged first
    assert keys[0][0] in (0x6040, 0x6041)


def test_map_budget_and_exceed():
    entries = [
        OdEntry(0x1A00, 0, name="N", default_value="2"),
        OdEntry(0x1A00, 1, name="M1", default_value="0x60410010"),  # 16
        OdEntry(0x1A00, 2, name="M2", default_value="0x606C0020"),  # 32
    ]
    slots, used = map_budget(entries, 0x1A00)
    assert slots == 2
    assert used == 48
    # Replace slot 2 with 32 bits still OK (48-32+32=48)
    assert not would_exceed_pdo_bits(entries, 0x1A00, 2, 32)
    # Add 32 into a new conceptual slot while keeping both → exclude none
    # Writing slot 3 (new) with 32 → 48+32=80
    assert would_exceed_pdo_bits(entries, 0x1A00, 3, 32)
    # Replace slot 1 (16) with 64 → 48-16+64=96
    assert would_exceed_pdo_bits(entries, 0x1A00, 1, 64)
    # Replace slot 2 with 16 → 48-32+16=32 OK
    assert not would_exceed_pdo_bits(entries, 0x1A00, 2, 16)
    # Exact 64 when replacing both effectively: exclude #2, add 16 → 16+16=32
    assert not would_exceed_pdo_bits(entries, 0x1A00, 2, 16)
    # Fill to exactly 64: used without #2 is 16; +48 = 64 OK
    assert not would_exceed_pdo_bits(entries, 0x1A00, 2, 48)
    # +49 exceeds
    assert would_exceed_pdo_bits(entries, 0x1A00, 2, 49)


def test_format_budget_and_first_slot():
    from pages.eds_pdo import (
        format_budget_text, format_status_line, first_ready_map_slot,
        _entry_matches_filter)
    assert "12 free" in format_budget_text(2, 52)
    assert format_status_line("TPDO1", 2, 48) == "TPDO1 #2 · 16 free"
    entries = [
        OdEntry(0x1600, 0, name="N", default_value="2"),
        OdEntry(0x1600, 1, name="M1", default_value="0x60410010"),
        OdEntry(0x1600, 2, name="M2", default_value="0x00000010"),  # empty idx
        OdEntry(0x1A00, 1, name="T1", default_value="0x00000010"),
    ]
    assert first_ready_map_slot(entries) == (0x1600, 2)
    filled = [
        OdEntry(0x1A00, 1, name="T1", default_value="0x60410010"),
    ]
    assert first_ready_map_slot(filled) == (0x1A00, 1)
    assert first_ready_map_slot([]) is None
    cw = OdEntry(0x6040, 0, name="Controlword", data_type="0x0006")
    assert _entry_matches_filter(cw, "6040")
    assert _entry_matches_filter(cw, "0x6040")
    assert _entry_matches_filter(cw, "control")
    assert _entry_matches_filter(cw, "Control Word")
    assert not _entry_matches_filter(cw, "6064")


if __name__ == "__main__":
    test_parse_pack_roundtrip()
    test_classify_pdo_ranges()
    test_bits_for_entry_dtype()
    test_mappable_skips_pdo_area_and_shells()
    test_map_budget_and_exceed()
    test_format_budget_and_first_slot()
    print("PASS eds_pdo helpers")
