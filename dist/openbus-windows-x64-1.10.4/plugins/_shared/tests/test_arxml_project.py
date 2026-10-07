# -*- coding: utf-8 -*-
"""arxmlparse Phase 3 — ECUC-lite + project validate smoke tests."""

from __future__ import annotations

import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_SHARED = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SHARED)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)

from _shared import arxml_project, arxmlparse  # noqa: E402


def test_ecuc_roundtrip():
    com = arxmlparse.empty_model()
    ecuc = arxmlparse.derive_ecuc_from_com(com, ecu_name="DemoEcu")
    assert any(m.name == "Com" for m in ecuc.modules)
    assert any(m.name == "CanIf" for m in ecuc.modules)
    text = arxmlparse.serialize_ecuc_lite(ecuc)
    assert "ECUC-MODULE-CONFIGURATION-VALUES" in text
    assert "DemoSignal" in text
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "ecuc.arxml")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        again = arxmlparse.parse_ecuc_lite(path)
        assert len(again.modules) == len(ecuc.modules)
        names = {m.name for m in again.modules}
        assert "Com" in names and "CanNm" in names
    print("PASS ECUC-lite derive/serialize/parse")


def test_validate_project():
    com = arxmlparse.empty_model()
    findings = arxmlparse.validate_project(com, None)
    assert any(f["rule"] == "ecuc_empty" for f in findings)
    ecuc = arxmlparse.derive_ecuc_from_com(com)
    findings2 = arxmlparse.validate_project(com, ecuc)
    assert not any(f["rule"] == "ecuc_empty" for f in findings2)
    assert not any(f.get("severity") == "error" for f in findings2)
    tip = arxmlparse.tip_for_field("ecuc.com.ComIPduSize")
    assert tip and "I-PDU" in tip["summary"]
    print("PASS validate_project + BSWMD-lite tip")


def test_project_layout():
    with tempfile.TemporaryDirectory() as td:
        root = os.path.join(td, "MyEcu")
        m = arxml_project.create_project(root, name="MyEcu", ecu_name="MyEcu")
        assert arxml_project.is_project_dir(root)
        assert os.path.isdir(os.path.join(root, "work"))
        m2 = arxml_project.load_manifest(root)
        assert m2.name == "MyEcu"
        assert m2.abs_role("com").endswith(os.path.join("work", "com.arxml"))
    print("PASS project.json layout")


def test_phase4_recipes_swc_bswmd():
    arxmlparse.clear_bswmd_extra()
    n = arxmlparse.load_default_bswmd_pack()
    assert n >= 1
    assert arxmlparse.tip_for_field("ecuc.com.ComIPduSize")
    com = arxmlparse.empty_model()
    com.ipdus[0].can_id = 0
    com.ipdus[0].signals[0].name = ""
    model, ecuc, log = arxmlparse.apply_fix_recipes(
        com, None,
        ["bump_dlc", "unique_can_ids", "name_empty_signals", "derive_ecuc"])
    assert model.ipdus[0].can_id != 0
    assert model.ipdus[0].signals[0].name
    assert ecuc.modules
    swc = arxmlparse.derive_swc_from_com(model)
    assert swc.components and swc.components[0].ports
    text = arxmlparse.serialize_swc_lite(swc)
    assert "APPLICATION-SW-COMPONENT-TYPE" in text
    findings = arxmlparse.validate_swc(model, swc)
    assert not any(f.get("severity") == "error" for f in findings)
    print("PASS Phase4 recipes + SWC + BSWMD (%d tips)" % n)


def test_phase5_dbc_and_bsw_validate():
    from _shared import arxml_bsw, arxml_dbc
    with tempfile.TemporaryDirectory() as td:
        dbc = os.path.join(td, "net.dbc")
        with open(dbc, "w", encoding="utf-8") as f:
            f.write(
                'VERSION ""\n\nNS_ :\n\nBS_:\n\nBU_: ECU\n\n'
                "BO_ 100 TestMsg: 8 ECU\n"
                ' SG_ Val : 0|8@1+ (1,0) [0|255] "" Vector__XXX\n'
            )
        model = arxml_dbc.import_dbc_to_model(dbc)
        assert len(model.ipdus) == 1
        assert model.ipdus[0].signals
        bsw = arxml_bsw.stub_all_modules("Ecu")
        bsw = arxml_bsw.sync_com_into_bsw(bsw, model, ecu_name="Ecu")
        assert "EcuC" in bsw
        findings = arxml_bsw.validate_bsw_set(bsw, model)
        assert not any(f.get("severity") == "error" for f in findings)
        # Comm containers present
        assert "ComIPdu" in str(arxml_bsw._MODULE_CONTAINERS["Com"])
        assert "DcmDspSession" in str(arxml_bsw._MODULE_CONTAINERS["Dcm"])
    print("PASS Phase5 DBC import + validate_bsw_set")


def test_schema_driven_bsw_config():
    from _shared import arxml_bsw, arxml_ecuc_schema, arxmlparse
    arxml_ecuc_schema.clear_schema_cache()
    assert len(arxml_ecuc_schema.list_schema_modules()) >= 40
    assert arxml_ecuc_schema.load_schema("Os")
    assert arxml_ecuc_schema.load_schema("Com")
    assert arxml_ecuc_schema.load_schema("Dcm")
    os_mod = arxml_bsw.stub_module("Os")
    names = [c.name for c in os_mod.modules[0].containers]
    assert "OsTask" in names or any(n.startswith("OsTask") for n in names)
    sch = arxml_ecuc_schema.load_schema("Os")
    ecuc = os_mod.clone()
    added = arxml_ecuc_schema.add_container(ecuc, "Os", sch, (), "OsTask")
    assert added is not None
    text = arxmlparse.serialize_ecuc_lite(ecuc)
    assert "ECUC-ENUMERATION-PARAM-VALUE" in text or "ECUC-NUMERICAL" in text
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "Os.arxml")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        again = arxmlparse.parse_ecuc_lite(path)
        assert again.modules
    n = arxml_ecuc_schema.register_schema_tips("Com")
    assert n >= 1
    tip = arxmlparse.tip_for_field("ecuc.com.ComIPduSize")
    assert tip
    print("PASS schema-driven BSW config + serialize kinds")


if __name__ == "__main__":
    test_ecuc_roundtrip()
    test_validate_project()
    test_project_layout()
    test_phase4_recipes_swc_bswmd()
    test_phase5_dbc_and_bsw_validate()
    test_schema_driven_bsw_config()
    print("All Phase 3/4/5 arxmlparse tests passed")
