# -*- coding: utf-8 -*-
"""EDS sidebar + five-pillar symbols (no Qt app)."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)


def test_build_eds_sidebar_symbol():
    path = os.path.join(_SUITE, "pages", "workspace_sidebar.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "def build_eds_sidebar(" in src
    assert "LIVE_SECTIONS" in src
    assert "TRACE_SECTIONS" in src
    assert "CODE_SECTIONS" in src
    assert "eds_codegen" not in src.split("EDS_SECTIONS")[1].split("EDS_NESTED")[0]
    tree = ast.parse(src)
    names = {n.name for n in tree.body if isinstance(n, ast.FunctionDef)}
    assert "build_eds_sidebar" in names
    print("PASS build_eds_sidebar")


def test_app_shell_four_pillars():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert '("eds", "EDS")' in src or "(\"eds\", \"EDS\")" in src
    assert '("device", "Live")' in src or "(\"device\", \"Live\")" in src
    assert '("trace", "Trace")' in src or "(\"trace\", \"Trace\")" in src
    assert '("code", "Code")' in src or "(\"code\", \"Code\")" in src
    assert "def _wire_live_inner(" in src
    print("PASS four pillars nav")


if __name__ == "__main__":
    test_build_eds_sidebar_symbol()
    test_app_shell_four_pillars()
    print("All project sidebar tests passed")
