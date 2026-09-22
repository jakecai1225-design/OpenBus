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
VAL_TABLE_ TempState 0 \"Cold\" 1 \"Warm\" 2 \"Hot\" ;
VAL_ 256 Temp 0 \"Cold\" 1 \"Warm\" ;
BA_DEF_ BO_ \"GenMsgCycleTime\" INT 0 65535;
BA_DEF_ BO_ \"GenMsgSendType\" ENUM \"Cyclic\",\"Event\",\"NotUsed\";
BA_DEF_DEF_ \"GenMsgCycleTime\" 10;
BA_ \"GenMsgCycleTime\" BO_ 256 10;
BA_ \"GenMsgSendType\" BO_ 256 0;
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


def test_communications_matrix_model():
    from pages.matrix import build_matrix_model
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "sample.dbc")
        with open(path, "w", encoding="utf-8") as f:
            f.write(SAMPLE)
        db = dbcparse.parse_file(path)
        nodes, rows = build_matrix_model(db)
        assert "ECU1" in nodes and "ECU2" in nodes
        assert any(r["signal"] == "RPM" for r in rows)
        rpm = next(r for r in rows if r["signal"] == "RPM")
        assert rpm["cells"].get("ECU1", (None,))[0] == "tx"
        assert rpm["cells"].get("ECU2", (None,))[0] == "rx"
        print("PASS communications matrix model")


def test_value_tables_and_attributes_roundtrip():
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "sample.dbc")
        with open(path, "w", encoding="utf-8") as f:
            f.write(SAMPLE)
        db = dbcparse.parse_file(path)
        assert "TempState" in db.value_tables
        assert db.value_tables["TempState"][2] == "Hot"
        assert db.attr_def("GenMsgCycleTime") is not None
        assert db.attr_def("GenMsgSendType") is not None
        msg = db.messages[256]
        assert msg.cycle_time == 10
        assert msg.attributes.get("GenMsgSendType") == "Cyclic"
        out = os.path.join(td, "out.dbc")
        text = dbcparse.serialize(db)
        with open(out, "w", encoding="utf-8") as f:
            f.write(text)
        db2 = dbcparse.parse_file(out)
        assert "TempState" in db2.value_tables
        assert db2.messages[256].cycle_time == 10
        assert db2.attr_def("GenMsgSendType").enum_values[:2] == ["Cyclic", "Event"]
        # Assign named table onto Temp
        db2.messages[256].signal("Temp").value_table = dict(db2.value_tables["TempState"])
        db2.messages[256].signal("Temp").value_table_name = "TempState"
        text2 = dbcparse.serialize(db2)
        assert "VAL_TABLE_ TempState" in text2
        assert 'VAL_ 256 Temp' in text2
        print("PASS value tables + attributes round-trip")


def test_vector_layout():
    """CANdb++ bit numbering: bit 0 is the LSB of byte 0."""
    data = bytearray(8)
    dbcparse.insert_intel(data, 0, 8, 0xAB)
    assert data[0] == 0xAB
    assert dbcparse.extract_intel(data, 0, 8) == 0xAB
    data = bytearray(8)
    dbcparse.insert_motorola(data, 7, 16, 0x1234)
    assert data[0] == 0x12 and data[1] == 0x34
    assert dbcparse.extract_motorola(bytes(data), 7, 16) == 0x1234
    print("PASS vector bit layout")


def test_undo_redo():
    doc = DbcDocument()
    doc.db.nodes.append("ECU1")
    doc.mark_dirty(True)
    assert doc.dirty and doc.can_undo()
    assert "ECU1" in doc.db.nodes
    doc.undo()
    assert "ECU1" not in doc.db.nodes
    assert doc.can_redo()
    doc.redo()
    assert "ECU1" in doc.db.nodes
    print("PASS undo / redo")


def test_csv_import():
    from core import csv_import
    db = dbcparse.DbcFile()
    db.nodes = ["Vector__XXX"]
    stats = csv_import.import_csv_text(db, csv_import.template_csv())
    assert stats["messages"] >= 1
    assert stats["signals"] >= 2
    assert 0x100 in db.messages
    assert db.messages[0x100].signal("RPM") is not None
    assert db.messages[0x100].signal("RPM").factor == 0.25
    print("PASS csv import")


def test_timing_lite():
    from core import timing_lite
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "sample.dbc")
        with open(path, "w", encoding="utf-8") as f:
            f.write(SAMPLE)
        db = dbcparse.parse_file(path)
        result = timing_lite.estimate_bus_load(db, 500000, True)
        assert result["cyclic"] >= 1
        assert result["load_pct"] > 0
        assert timing_lite.load_band(10) == "ok"
        assert timing_lite.load_band(40) == "warn"
        assert timing_lite.load_band(60) == "high"
        print("PASS timing lite (load=%.2f%%)" % result["load_pct"])


if __name__ == "__main__":
    test_roundtrip()
    test_lint_and_merge()
    test_communications_matrix_model()
    test_value_tables_and_attributes_roundtrip()
    test_vector_layout()
    test_undo_redo()
    test_csv_import()
    test_timing_lite()
    print("All dbc-studio tests passed")
