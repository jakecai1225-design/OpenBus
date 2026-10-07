# -*- coding: utf-8 -*-
"""Shared type-scale tokens must stay unified (§13.14)."""
from __future__ import annotations

import os
import re
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SHARED = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SHARED)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)


def test_font_tokens():
    from _shared import vscode_theme as T
    from _shared import suite_ui as sui

    assert T.FS_BODY == 12
    assert T.FS_CTRL == 12
    assert T.FS_META == 11
    assert T.FS_MENU == 12
    assert T.FS_MONO == 12
    assert sui.FS_BODY == T.FS_BODY
    ss = T.stylesheet()
    assert "font-size: 13px" not in ss
    assert "font-size: 12px" in ss
    assert "font-size: 11px" in ss
    # Bare QLabel must share body scale (stops giant form labels)
    assert "QLabel {" in ss.replace(" ", "") or "QLabel {" in ss
    assert re.search(r"QLabel\s*\{[^}]*font-size:\s*12px", ss, re.S)
    assert "12px" in sui.TREE_STYLE
    assert "font-size: 13px" not in sui.SUITE_OVERLAY
    print("PASS font tokens")


if __name__ == "__main__":
    test_font_tokens()
