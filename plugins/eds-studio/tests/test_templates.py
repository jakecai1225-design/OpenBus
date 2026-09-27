# -*- coding: utf-8 -*-
"""Starter templates and profile catalog smoke test."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from _shared import edsparse  # noqa: E402
from core import profiles, templates  # noqa: E402
from document import EdsDocument  # noqa: E402


def test_catalog_coverage():
    ids = {row[0] for row in profiles.PROFILE_CATALOG}
    assert "401" in ids and "406" in ids and "418" in ids
    assert "302" in ids and "437" in ids
    assert len(profiles.PROFILE_CATALOG) >= 30
    assert len(profiles.objects_for("402")) >= 20
    print("PASS profile catalog (%d entries)" % len(profiles.PROFILE_CATALOG))


def test_starters_valid():
    for tid, title, _blurb, fn in templates.STARTER_TEMPLATES:
        doc = fn()
        assert doc.entries, title
        idxs = {e.index for e in doc.entries}
        assert 0x1000 in idxs, title
        assert 0x1018 in idxs, title
        findings = edsparse.validate_document(doc, deep=True)
        errors = [f for f in findings if f.get("level") == "error"
                  or f.get("severity") == "error"]
        # PDO map refs in starters should resolve
        pdo_errs = [f for f in errors if f.get("rule") == "pdo_map_ref"]
        assert not pdo_errs, "%s: %s" % (title, pdo_errs)
        print("  OK starter %-12s %3d objects" % (tid, len(doc.entries)))
    print("PASS %d starters" % len(templates.STARTER_TEMPLATES))


def test_apply_starter_to_document():
    shell_doc = EdsDocument()
    eds = templates.build_template("dio")
    shell_doc.set_eds(eds, path="", dirty=True)
    assert any(e.index == 0x6000 for e in shell_doc.eds.entries)
    assert any(e.index == 0x1600 and e.subindex == 1
               for e in shell_doc.eds.entries)
    print("PASS apply digital-IO starter to EdsDocument")


if __name__ == "__main__":
    test_catalog_coverage()
    test_starters_valid()
    test_apply_starter_to_document()
    print("All template tests passed")
