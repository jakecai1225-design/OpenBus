# -*- coding: utf-8 -*-
"""Golden ARXML fixture corpus — parse / serialize / validate (Phase 12)."""

from __future__ import annotations

import glob
import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)

from _shared import arxml_bsw, arxmlparse  # noqa: E402
from document import ArxmlDocument  # noqa: E402

_FX = os.path.join(_HERE, "fixtures")


def _com_fixtures():
    return sorted(glob.glob(os.path.join(_FX, "com_*.arxml")))


def _bsw_fixtures():
    return sorted(glob.glob(os.path.join(_FX, "bsw_*.arxml")))


def test_fixture_count():
    assert len(_com_fixtures()) >= 6, "need COM golden set"
    assert len(_bsw_fixtures()) >= 4, "need BSW golden set"
    assert len(_com_fixtures()) + len(_bsw_fixtures()) >= 10
    print("PASS fixture count %d" % (
        len(_com_fixtures()) + len(_bsw_fixtures())))


def test_com_roundtrip_preserve_foreign():
    for path in _com_fixtures():
        model = arxmlparse.parse_arxml_model(path)
        text = arxmlparse.serialize_model(model)
        again = arxmlparse.parse_arxml_model_text(text) if hasattr(
            arxmlparse, "parse_arxml_model_text") else None
        if again is None:
            # Write temp and re-parse
            with tempfile.NamedTemporaryFile(
                    suffix=".arxml", delete=False, mode="w",
                    encoding="utf-8") as tf:
                tf.write(text)
                tmp = tf.name
            try:
                again = arxmlparse.parse_arxml_model(tmp)
            finally:
                os.unlink(tmp)
        assert len(again.ipdus) == len(model.ipdus), path
        foreign = getattr(model, "foreign_packages", None) or []
        if foreign:
            out_foreign = getattr(again, "foreign_packages", None) or []
            # Names from SHORT-NAME should survive round-trip
            for frag in foreign:
                assert "VendorExtra" in frag or "AR-PACKAGE" in frag
            assert out_foreign or "VendorExtra" in text, path
        findings = arxmlparse.validate_ipdus(model.ipdus)
        assert isinstance(findings, list)
    print("PASS COM round-trip %d files" % len(_com_fixtures()))


def test_bsw_ecuc_roundtrip():
    for path in _bsw_fixtures():
        ecuc = arxmlparse.parse_ecuc_lite(path)
        text = arxmlparse.serialize_ecuc_lite(ecuc)
        assert "ECUC" in text.upper() or "MODULE" in text.upper() or len(text) > 40
        with tempfile.NamedTemporaryFile(
                suffix=".arxml", delete=False, mode="w",
                encoding="utf-8") as tf:
            tf.write(text)
            tmp = tf.name
        try:
            again = arxmlparse.parse_ecuc_lite(tmp)
            assert len(again.modules) == len(ecuc.modules), path
        finally:
            os.unlink(tmp)
    print("PASS BSW ECUC round-trip %d files" % len(_bsw_fixtures()))


def test_document_bsw_undo_and_ack():
    doc = ArxmlDocument()
    doc.new()
    doc.init_all_bsw()
    name = "Com"
    before = doc.bsw[name]
    model = before.clone()
    if model.modules and model.modules[0].containers:
        model.modules[0].containers[0].name = "RenamedCont"
    doc.apply_bsw_module(name, model)
    assert doc.bsw[name].modules[0].containers[0].name == "RenamedCont"
    assert doc.can_undo()
    doc.undo()
    assert doc.bsw[name].modules[0].containers[0].name != "RenamedCont"
    findings = doc.validate_all()
    if findings and findings[0].get("rule") != "ok":
        key = doc.finding_ack_key(findings[0])
        doc.ack_finding(key)
        assert key in doc.finding_acks
        again = doc.validate_all()
        matched = [f for f in again if f.get("ack_key") == key]
        assert matched and matched[0].get("acked")
    print("PASS BSW undo + finding ack")


def test_cross_module_validate_and_wizards():
    doc = ArxmlDocument()
    with tempfile.TemporaryDirectory() as td:
        root = os.path.join(td, "FxEcu")
        doc.new_project(root, name="FxEcu", ecu_name="FxEcu")
        log = doc.wizard_init_comm_stack()
        assert any("Com" in x or "sync" in x.lower() for x in log)
        findings = arxml_bsw.validate_bsw_set(doc.bsw, doc.model)
        assert isinstance(findings, list)
        sarif = arxmlparse.findings_to_sarif(doc.validate_all())
        assert sarif["version"] == "2.1.0"
        props = sarif["runs"][0]["results"]
        if props:
            assert "acked" in props[0]["properties"]
        html = doc.handoff_html_report()
        assert "DaVinci" in html and "handoff" in html.lower()
    print("PASS wizards + cross-module + SARIF ack")


def test_schema_depth_acceptance():
    from _shared import arxml_ecuc_schema
    com = arxml_ecuc_schema.load_schema("Com") or {}
    canif = arxml_ecuc_schema.load_schema("CanIf") or {}

    def count(sch):
        n = 0
        for c in sch.get("containers") or []:
            n += len(c.get("params") or [])
            for ch in c.get("containers") or []:
                n += len(ch.get("params") or [])
        return n

    assert count(com) >= 200, "Com params %d" % count(com)
    assert count(canif) >= 120, "CanIf params %d" % count(canif)
    print("PASS schema depth Com=%d CanIf=%d" % (count(com), count(canif)))


if __name__ == "__main__":
    test_fixture_count()
    test_com_roundtrip_preserve_foreign()
    test_bsw_ecuc_roundtrip()
    test_document_bsw_undo_and_ack()
    test_cross_module_validate_and_wizards()
    test_schema_depth_acceptance()
    print("ALL fixture tests OK")
