# -*- coding: utf-8 -*-
"""AUTOSAR Studio document + project smoke test."""

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

from _shared import arxmlparse  # noqa: E402
from document import ArxmlDocument  # noqa: E402


def test_document_undo_save():
    doc = ArxmlDocument()
    doc.new()
    assert len(doc.model.ipdus) >= 1
    n0 = len(doc.model.ipdus)
    model = doc.clone_model()
    model.ipdus.append(arxmlparse.Ipdu("ExtraPdu", 0x200, 8, []))
    doc.apply_model(model)
    assert doc.dirty
    assert len(doc.model.ipdus) == n0 + 1
    assert doc.undo()
    assert len(doc.model.ipdus) == n0
    assert doc.redo()
    assert len(doc.model.ipdus) == n0 + 1

    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "t.arxml")
        doc.save(path)
        assert not doc.dirty
        again = ArxmlDocument()
        again.load(path)
        assert len(again.model.ipdus) == len(doc.model.ipdus)
        assert any(p.name == "ExtraPdu" for p in again.model.ipdus)
    print("PASS ArxmlDocument undo/save/load")


def test_project_derive_write():
    doc = ArxmlDocument()
    with tempfile.TemporaryDirectory() as td:
        root = os.path.join(td, "BodyEcu")
        doc.new_project(root, name="BodyEcu", ecu_name="BodyEcu")
        assert doc.has_project()
        assert os.path.isfile(doc.manifest.abs_role("com"))
        assert os.path.isfile(doc.manifest.abs_role("ecuc"))
        assert len(doc.ecuc.modules) >= 3
        findings = doc.validate_all()
        assert not any(f.get("severity") == "error" for f in findings)
        out = doc.write_out_reports(findings)
        assert os.path.isfile(out["sarif"])
        assert os.path.isfile(out["html"])
        handoff = doc.handoff_com_path()
        assert handoff.endswith("com.arxml")
    print("PASS project create/derive/validate/out")


def test_page_imports():
    from pages import (  # noqa: F401
        analysis, bsw, compare, editor, export, library, merge, project, spec,
        swc, validate,
    )
    assert callable(editor.build)
    assert callable(project.build)
    assert callable(bsw.build)
    assert callable(swc.build)
    print("PASS page module imports")


def test_bsw_catalog_project():
    from _shared import arxml_bsw
    assert "Os" in arxml_bsw.module_names()
    assert "Dcm" in arxml_bsw.module_names()
    assert "NvM" in arxml_bsw.module_names()
    assert len(arxml_bsw.module_names()) >= 40
    with tempfile.TemporaryDirectory() as td:
        root = os.path.join(td, "FullEcu")
        doc = ArxmlDocument()
        doc.new_project(root, name="FullEcu", ecu_name="FullEcu")
        assert len(doc.bsw) >= 40
        assert os.path.isdir(os.path.join(root, "work", "bsw"))
        assert os.path.isfile(os.path.join(root, "work", "bsw", "Os.arxml"))
        assert os.path.isfile(os.path.join(root, "work", "bsw", "Com.arxml"))
        assert os.path.isfile(os.path.join(root, "work", "bsw", "Dcm.arxml"))
    print("PASS BSW catalog + per-module ARXML")


def test_dbc_import_sync():
    with tempfile.TemporaryDirectory() as td:
        dbc = os.path.join(td, "mini.dbc")
        with open(dbc, "w", encoding="utf-8") as f:
            f.write(
                'VERSION ""\n\n'
                "NS_ :\n\n"
                "BS_:\n\n"
                "BU_: ECU\n\n"
                "BO_ 256 EngineData: 8 ECU\n"
                ' SG_ RPM : 0|16@1+ (0.25,0) [0|8000] "rpm" Vector__XXX\n'
                "BO_ 512 BodyStatus: 4 ECU\n"
                ' SG_ DoorOpen : 0|1@1+ (1,0) [0|1] "" Vector__XXX\n'
            )
        _run_dbc_import(dbc)
    print("PASS DBC import → COM + BSW sync")


def _run_dbc_import(dbc_path: str):
    from _shared import arxml_bsw, arxml_dbc
    model = arxml_dbc.import_dbc_to_model(dbc_path)
    assert model.ipdus, "expected at least one I-PDU from DBC"
    doc = ArxmlDocument()
    with tempfile.TemporaryDirectory() as td:
        root = os.path.join(td, "DbcEcu")
        doc.new_project(root, name="DbcEcu", ecu_name="DbcEcu")
        n = doc.import_dbc(dbc_path)
        assert n == len(model.ipdus)
        assert "Com" in doc.bsw and "CanIf" in doc.bsw and "PduR" in doc.bsw
        assert os.path.isfile(os.path.join(root, "input", "network.dbc"))
        findings = arxml_bsw.validate_bsw_set(doc.bsw, doc.model)
        assert not any(f.get("severity") == "error" for f in findings)
        paths = doc.write_intermediates()
        assert paths.get("bsw")
        for name in ("Com", "CanIf", "PduR", "Os"):
            assert os.path.isfile(os.path.join(root, "work", "bsw", "%s.arxml" % name))


def test_menu_nav_keys():
    from app_shell import FEATURE_ROUTE, NAV_PAGES
    keys = [k for k, _ in NAV_PAGES]
    assert keys == [
        "project", "config", "com", "bus", "validate", "setup"]
    for need in ("project", "bsw", "editor", "com", "system",
                 "validate", "spec", "library", "swc", "nm", "e2e"):
        assert need in FEATURE_ROUTE, need
        ws, _tab = FEATURE_ROUTE[need]
        assert ws in keys, (need, ws)
    print("PASS nav page keys (6 workspaces + feature routes)")


if __name__ == "__main__":
    test_document_undo_save()
    test_project_derive_write()
    test_page_imports()
    test_bsw_catalog_project()
    test_dbc_import_sync()
    test_menu_nav_keys()
    print("All autosar-suite tests passed")
