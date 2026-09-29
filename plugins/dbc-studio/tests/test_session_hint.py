# -*- coding: utf-8 -*-
"""DbcDocument next_hint / focus (no full Qt app required beyond QCore)."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from document import DbcDocument


def test_next_hint_golden():
    d = DbcDocument()
    label, action, _ = d.next_hint()
    assert action == "dbc.open"

    d.db.messages = {}  # still empty content for _has_content without path
    # Force content via path-less dirty with a fake message
    from _shared import dbcparse
    msg = dbcparse.Message()
    msg.name = "Demo"
    msg.can_id = 0x100
    d.db.messages[0x100] = msg
    d.dirty = True
    label, action, _ = d.next_hint()
    assert action == "view.validate"

    d.mark_validated(True, error_count=0)
    label, action, _ = d.next_hint()
    assert action == "dbc.save"

    d.path = r"C:\tmp\demo.dbc"
    d.dirty = False
    d.mark_validated(True, error_count=0)
    label, action, _ = d.next_hint()
    assert action == "view.export"

    d.advance_next_hint()
    label, action, _ = d.next_hint()
    assert action == "view.library"
    print("PASS next_hint")


def test_set_focus():
    d = DbcDocument()
    d.set_focus(0x200, "Speed")
    assert d.focus_can_id == 0x200
    assert d.focus_signal == "Speed"
    print("PASS set_focus")


if __name__ == "__main__":
    test_next_hint_golden()
    test_set_focus()
    print("All session hint tests passed")
