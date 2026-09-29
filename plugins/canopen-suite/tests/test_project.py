# -*- coding: utf-8 -*-
"""CANopen project manifest unit tests (no Qt / sin)."""

from __future__ import annotations

import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)

from core import project as can_project  # noqa: E402


def test_create_load_roundtrip():
    with tempfile.TemporaryDirectory() as tmp:
        m = can_project.create_project(tmp, name="DemoNode", node_id=5)
        assert can_project.is_project_dir(tmp)
        assert m.name == "DemoNode"
        assert m.node_id == 5
        assert os.path.isfile(can_project.project_json_path(tmp))
        again = can_project.load_manifest(tmp)
        assert again.name == "DemoNode"
        assert again.node_id == 5
        assert again.abs_eds().endswith("device.eds")
        again.notes = "hello"
        again.node_id = 7
        can_project.save_manifest(again)
        third = can_project.load_manifest(tmp)
        assert third.notes == "hello"
        assert third.node_id == 7
    print("PASS project create/load")


def test_not_project_dir():
    with tempfile.TemporaryDirectory() as tmp:
        assert not can_project.is_project_dir(tmp)
        assert can_project.resolve_eds_path(
            can_project.ProjectManifest(root=tmp)) is None
    print("PASS not project dir")


def test_list_project_eds_multi():
    with tempfile.TemporaryDirectory() as tmp:
        m = can_project.create_project(tmp, name="Multi", node_id=1)
        # Write two EDS stubs
        a = os.path.join(tmp, "device.eds")
        b = os.path.join(tmp, "slave2.eds")
        for p in (a, b):
            with open(p, "w", encoding="utf-8") as f:
                f.write("[FileInfo]\n")
        paths = can_project.list_project_eds(tmp, m)
        names = {os.path.basename(p) for p in paths}
        assert "device.eds" in names
        assert "slave2.eds" in names
        assert paths[0].endswith("device.eds")
        can_project.set_primary_eds(m, b)
        can_project.save_manifest(m)
        again = can_project.load_manifest(tmp)
        assert again.eds.endswith("slave2.eds")
        assert "slave2.eds" in again.eds_files
    print("PASS multi EDS list")


if __name__ == "__main__":
    test_create_load_roundtrip()
    test_not_project_dir()
    test_list_project_eds_multi()
    print("All project tests passed")
