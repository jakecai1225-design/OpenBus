# -*- coding: utf-8 -*-
"""Layout cell ↔ Vector bit mapping and drag-to-range (no QApplication)."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from pages.bit_layout import bit_to_cell, cell_to_bit, range_from_drag  # noqa: E402
from _shared.dbcparse import signal_bit_numbers  # noqa: E402


def test_cell_bit_roundtrip():
    for bit in range(64):
        row, col = bit_to_cell(bit)
        assert cell_to_bit(row, col) == bit, bit
        assert 0 <= col <= 7
    # Byte 0: left column is bit 7, right column is bit 0
    assert cell_to_bit(0, 0) == 7
    assert cell_to_bit(0, 7) == 0
    assert cell_to_bit(1, 7) == 8
    print("PASS cell_bit_roundtrip")


def test_intel_drag_byte0():
    # Drag full byte 0 left-to-right (bit 7 → bit 0) or opposite
    a = range_from_drag((0, 0), (0, 7), True, 8)
    b = range_from_drag((0, 7), (0, 0), True, 8)
    assert a == (0, 8), a
    assert b == (0, 8), b
    # Single cell bit 3 (column 4)
    one = range_from_drag((0, 4), (0, 4), True, 8)
    assert one == (3, 1), one
    print("PASS intel_drag_byte0")


def test_motorola_drag_byte0():
    # MSB bit 7 (col 0) to LSB bit 0 (col 7) → start 7, length 8
    rng = range_from_drag((0, 0), (0, 7), False, 8)
    assert rng == (7, 8), rng
    bits = signal_bit_numbers(7, 8, False)
    assert bits == [7, 6, 5, 4, 3, 2, 1, 0]
    print("PASS motorola_drag_byte0")


def test_motorola_16bit():
    # Classic 16-bit Motorola: start 7 covers byte0 then byte1 down to bit 8
    last = bit_to_cell(8)  # byte 1, col 7
    rng = range_from_drag((0, 0), last, False, 8)
    assert rng == (7, 16), rng
    assert signal_bit_numbers(7, 16, False)[-1] == 8
    print("PASS motorola_16bit")


def test_clamp_to_dlc():
    # 1-byte frame: drag cannot leave byte 0
    rng = range_from_drag((0, 7), (0, 0), True, 1)
    assert rng == (0, 8), rng
    print("PASS clamp_to_dlc")


if __name__ == "__main__":
    test_cell_bit_roundtrip()
    test_intel_drag_byte0()
    test_motorola_drag_byte0()
    test_motorola_16bit()
    test_clamp_to_dlc()
    print("PASS bit layout")
