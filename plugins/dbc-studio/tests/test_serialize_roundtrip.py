# -*- coding: utf-8 -*-
"""Round-trip serialize test for DBC Studio document model."""

from __future__ import annotations

import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from _shared import dbcparse  # noqa: E402
from document import DbcDocument  # noqa: E402
from core.lint_engine import lint_dbc  # noqa: E402
from core.merge_engine import merge_dbc, POLICY_SKIP  # noqa: E402


SAMPLE = """VERSION \"\"

NS_ :
BS_:
BU_: ECU1 ECU2

BO_ 256 EngineData: 8 ECU1
 SG_ RPM : 0|16@1+ (0.25,0) [0|8000] \"rpm\" ECU2
 SG_ Temp : 16|8@1- (1,-40) [-40|215] \"C\" ECU2

CM_ BO_ 256 \"Engine periodic\";
CM_ SG_ 256 RPM \"Engine speed\";
VAL_ 256 Temp 0 \"Cold\" 1 \"Warm\" ;
BA_DEF_ BO_ \"GenMsgCycleTime\" INT 0 65535;
BA_ \"GenMsgCycleTime\" BO_ 256 10;
"""


def test_roundtrip():
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "sample.dbc")
        with open(path, "w", encoding="utf-8") as f:
            f.write(SAMPLE)
        doc = DbcDocument()
        doc.load(path)
        assert 256 in doc.db.messages
        msg = doc.db.messages[256]
        assert msg.name == "EngineData"
        assert msg.signal("RPM") is not None
        out = os.path.join(td, "out.dbc")
        doc.save(out)
        db2 = dbcparse.parse_file(out)
        assert 256 in db2.messages
        assert db2.messages[256].signal("RPM").factor == 0.25
        print("PASS serialize round-trip")


def test_lint_and_merge():
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "sample.dbc")
        with open(path, "w", encoding="utf-8") as f:
            f.write(SAMPLE)
        db = dbcparse.parse_file(path)
        findings = lint_dbc(db)
        assert isinstance(findings, list)
        print("PASS lint (%d findings)" % len(findings))
        other = dbcparse.DbcFile()
        other.nodes = ["ECU3"]
        m = dbcparse.Message()
        m.can_id = 0x200
        m.name = "Other"
        m.dlc = 8
        m.sender = "ECU3"
        other.messages[0x200] = m
        conflicts = []
        merge_dbc(db, other, "other.dbc", POLICY_SKIP, conflicts)
        assert 0x200 in db.messages
        print("PASS merge")


if __name__ == "__main__":
    test_roundtrip()
    test_lint_and_merge()
    print("All dbc-studio tests passed")
