# -*- coding: utf-8 -*-
"""Shared starters + assemble_document smoke tests."""

from __future__ import annotations

import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from _shared.canopen_profiles import (  # noqa: E402
    PROFILE_CATALOG,
    STARTER_TEMPLATES,
    assemble_document,
    build_template,
    objects_for,
)


def test_catalog():
    ids = {row[0] for row in PROFILE_CATALOG}
    assert "301" in ids and "402" in ids
    assert len(PROFILE_CATALOG) >= 30
    assert len(objects_for("402")) >= 20
    print("PASS profile catalog (%d entries)" % len(PROFILE_CATALOG))


def test_starters():
    for tid, title, _blurb, fn in STARTER_TEMPLATES:
        doc = fn()
        assert doc.entries, title
        assert doc.entry(0x1000) is not None, title
    assert len(STARTER_TEMPLATES) >= 8
    eds = build_template("dio")
    assert any(e.index == 0x6000 for e in eds.entries)
    print("PASS %d starters" % len(STARTER_TEMPLATES))


def test_assemble():
    doc = assemble_document(
        base="301", device="401",
        packs=("RPDO1+TPDO1", "Heartbeat producer"),
        product="Test IO")
    assert doc.device_info.get("ProductName") == "Test IO"
    assert any(e.index == 0x1400 or e.index == 0x1800 for e in doc.entries)
    assert doc.entry(0x1000) is not None
    print("PASS assemble_document (%d objects)" % len(doc.entries))


def test_session_new():
    # Lightweight session API — need sin stub for import
    try:
        from PyQt6.QtWidgets import QApplication
    except ImportError:
        print("SKIP session (no PyQt6)")
        return
    import types
    if "sin" not in sys.modules:
        stub = types.ModuleType("sin")
        stub.frames = types.SimpleNamespace(send=lambda *a, **k: None)
        sys.modules["sin"] = stub
    app = QApplication.instance() or QApplication([])
    suite = os.path.join(ROOT, "canopen-suite")
    if suite not in sys.path:
        sys.path.insert(0, suite)
    from session import SharedSession  # noqa: E402
    s = SharedSession()
    s.new_empty()
    assert s.draft_entries
    assert s.eds_path == ""
    assert not s.eds_dirty
    s.new_from_template("servo")
    assert any(e.index == 0x6040 for e in s.draft_entries)
    s.new_from_profile(base="301", device="406", packs=("TPDO1",))
    assert any(e.index == 0x6004 or e.index >= 0x6000 for e in s.draft_entries)
    print("PASS session new_from_*")


if __name__ == "__main__":
    test_catalog()
    test_starters()
    test_assemble()
    test_session_new()
    print("All shared template / assemble tests passed")
