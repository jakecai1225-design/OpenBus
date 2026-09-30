# -*- coding: utf-8 -*-
"""Strip height must keep full CTRL_H field borders visible."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)


def test_strip_fits_ctrl():
    from pages import _ui
    assert _ui.CTRL_H == 28
    assert _ui.STRIP_PAD_V == 4
    assert _ui.TOOL_H == _ui.CTRL_H + 2 * _ui.STRIP_PAD_V + _ui.STRIP_EDGE
    assert _ui.FILTER_H == _ui.TOOL_H
    # Content box + vertical margins must fit inside strip height.
    assert _ui.CTRL_H + 2 * _ui.STRIP_PAD_V + _ui.STRIP_EDGE <= _ui.TOOL_H
    assert _ui.CTRL_H + 2 * _ui.STRIP_PAD_V + _ui.STRIP_EDGE <= _ui.FILTER_H
    print("PASS strip fits CTRL_H (+pad)")


def test_no_max_height_clamp_on_strips():
    path = os.path.join(_SUITE, "pages", "_ui.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    # Regression: max-height on SuiteToolStrip clipped field borders.
    assert "QWidget#SuiteToolStrip" in src
    marker = "QWidget#SuiteToolStrip"
    block_start = src.index(marker)
    block = src[block_start:block_start + 280]
    # Host rule must not clamp max-height (children keep CTRL_H max).
    host_rule = block.split("QLineEdit")[0]
    assert "max-height" not in host_rule
    print("PASS no SuiteToolStrip max-height clamp")


if __name__ == "__main__":
    test_strip_fits_ctrl()
    test_no_max_height_clamp_on_strips()
    print("All strip border tests passed")
