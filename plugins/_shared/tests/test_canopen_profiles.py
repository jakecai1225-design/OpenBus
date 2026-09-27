# -*- coding: utf-8 -*-
"""Smoke test for shared CiA profile catalog."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SHARED = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SHARED)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)

from _shared.canopen_profiles import (  # noqa: E402
    PROFILE_CATALOG,
    catalog_by_category,
    missing_entries,
    objects_for,
    present_keys,
    search_profile,
)


def test_catalog_breadth():
    ids = {row[0] for row in PROFILE_CATALOG}
    assert "301" in ids and "402" in ids and "418" in ids
    assert "RPDO1" in ids and "Identity" in ids
    # Most common device profiles present
    for pid in ("401", "403", "404", "406", "410", "417", "419", "437"):
        assert pid in ids, pid
    assert len(PROFILE_CATALOG) >= 30
    cats = catalog_by_category()
    assert "Communication" in cats and "Device" in cats and "Pack" in cats
    print("PASS catalog breadth (%d entries)" % len(PROFILE_CATALOG))


def test_objects_and_search():
    objs = objects_for("402")
    assert any(e.index == 0x6040 for e in objs)
    assert any(e.index == 0x6041 for e in objs)
    hit = search_profile("controlword", "402")
    assert hit and hit[0].index == 0x6040
    print("PASS 402 objects/search (%d)" % len(objs))


def test_missing_entries():
    base = objects_for("301")[:5]
    miss = missing_entries("301", base)
    have = present_keys(base)
    assert all((e.index, e.subindex) not in have for e in miss)
    assert len(miss) == len(objects_for("301")) - len(base)
    print("PASS missing_entries")


def test_profile_coverage():
    from _shared.canopen_profiles import (
        detect_device_profile, validate_profile_coverage)
    from _shared.edsparse import OdEntry, validate_document, empty_document

    # Explicit 402 device type in 0x1000
    entries = [
        OdEntry(0x1000, 0, "Device type", default_value="0x00020192"),
        OdEntry(0x1001, 0, "Error register"),
        OdEntry(0x1018, 0, "Identity"),
    ]
    assert detect_device_profile(entries) == "402"
    findings = validate_profile_coverage(entries, profile_id="402")
    rules = {f["rule"] for f in findings}
    assert "profile" in rules
    assert "profile_object" in rules
    assert any("Controlword" in f["message"] for f in findings)

    doc = empty_document()
    doc.entries = list(entries)
    all_f = validate_document(doc, deep=True, profile_id="402")
    assert any(f.get("rule") == "profile_object" for f in all_f)
    print("PASS profile coverage validate")


if __name__ == "__main__":
    test_catalog_breadth()
    test_objects_and_search()
    test_missing_entries()
    test_profile_coverage()
    print("All canopen_profiles tests passed")
