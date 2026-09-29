# -*- coding: utf-8 -*-
"""Session focus carry + next_hint (Interop Unity IU-2 / IU-6)."""

from __future__ import annotations

import os
import sys
import types
from unittest.mock import MagicMock

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

# Stub host modules before importing SharedSession.
sys.modules.setdefault("sin", types.ModuleType("sin"))
sys.modules["sin"].frames = MagicMock()  # type: ignore[attr-defined]

from PyQt6.QtCore import QCoreApplication

from core.eds_parse import OdEntry
from session import SharedSession


def _app():
    app = QCoreApplication.instance()
    if app is None:
        app = QCoreApplication([])
    return app


def test_set_focus_idempotent():
    _app()
    s = SharedSession()
    hits = []
    s.on_focus_changed(lambda: hits.append(1))
    s.set_focus(0x2000, 1)
    assert s.focus_index == 0x2000
    assert s.focus_subindex == 1
    assert len(hits) == 1
    s.set_focus(0x2000, 1)
    assert len(hits) == 1
    s.set_focus(0x2001, 0)
    assert len(hits) == 2
    print("PASS set_focus")


def test_next_hint_golden_path():
    _app()
    s = SharedSession()
    label, action, _kw = s.next_hint()
    assert action == "eds.new"
    assert "New" in label

    s.draft_entries = [
        OdEntry(index=0x1000, subindex=0, name="Device type",
                object_type="0x7", data_type="0x0007", access_type="ro",
                default_value="0"),
    ]
    s.eds_dirty = True
    s._clean_fingerprint = "x"
    label, action, _kw = s.next_hint()
    assert action == "eds.save"

    # Saved path, no PDO maps → Map PDOs
    s.eds_path = r"C:\tmp\demo.eds"
    s.eds_dirty = False
    s._clean_fingerprint = s._fingerprint()
    label, action, _kw = s.next_hint()
    assert action == "view.pdo_map"
    assert "PDO" in label

    # Fake a map entry so hint advances
    s.draft_entries.append(
        OdEntry(index=0x1A00, subindex=1, name="Mapped object 1",
                object_type="0x7", data_type="0x0007", access_type="rw",
                default_value="0x20000108"))
    s._clean_fingerprint = s._fingerprint()
    s.eds_dirty = False
    label, action, _kw = s.next_hint()
    assert action == "view.check"

    s.mark_validated(True)
    label, action, _kw = s.next_hint()
    assert action == "eds.apply_od"

    s.od_entries = list(s.draft_entries)
    label, action, _kw = s.next_hint()
    assert action == "view.trace"
    assert "Trace" in label

    s.advance_next_hint()
    label, action, kw = s.next_hint()
    assert action == "network.nmt"
    assert kw.get("cmd") == 0x01

    s.advance_next_hint()
    label, action, _kw = s.next_hint()
    assert action == "eds.codegen"
    print("PASS next_hint")


def test_has_pdo_maps_helper():
    _app()
    s = SharedSession()
    assert s._has_pdo_maps() is False
    s.draft_entries = [
        OdEntry(index=0x1A00, subindex=1, name="m",
                object_type="0x7", data_type="0x0007", access_type="rw",
                default_value="0x0"),
    ]
    assert s._has_pdo_maps() is False
    s.draft_entries[0].default_value = "0x20000110"
    assert s._has_pdo_maps() is True
    print("PASS _has_pdo_maps")


def test_next_step_bar_exists():
    path = os.path.join(_SUITE, "pages", "_ui.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "def next_step_bar(" in src
    print("PASS next_step_bar symbol")


if __name__ == "__main__":
    test_set_focus_idempotent()
    test_next_hint_golden_path()
    test_has_pdo_maps_helper()
    test_next_step_bar_exists()
    print("All session focus tests passed")
