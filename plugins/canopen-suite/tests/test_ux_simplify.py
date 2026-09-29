# -*- coding: utf-8 -*-
"""EDS sidebar + File-menu EDS symbols (no Qt app)."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)


def test_build_eds_sidebar_flat():
    path = os.path.join(_SUITE, "pages", "workspace_sidebar.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "def build_eds_sidebar(" in src
    assert "LIVE_SECTIONS" in src
    # File folds removed from sidebar
    assert "Current / Project / Recent" not in src or "files live under File" in src
    assert "return build_section_sidebar(" in src
    tree = ast.parse(src)
    names = {n.name for n in tree.body if isinstance(n, ast.FunctionDef)}
    assert "build_eds_sidebar" in names
    print("PASS build_eds_sidebar flat")


def test_file_menu_eds():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "Recent &EDS" in src
    assert "Project E&DS" in src
    assert "def _fill_recent_eds_menu(" in src
    assert "def _fill_project_eds_menu(" in src
    print("PASS File menu EDS")


def test_no_finetune():
    path = os.path.join(_SUITE, "pages", "eds_pdo.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "Fine-tune" not in src
    assert "fine_toggle" not in src
    print("PASS no Fine-tune")


def test_trace_simplified():
    path = os.path.join(_SUITE, "pages", "analysis.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "Quick send" not in src
    assert "btn_raw" not in src
    assert "Watch" in src
    print("PASS Trace simplified")


def test_kit_density_tokens():
    path = os.path.join(_SUITE, "pages", "_ui.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "Chrome recipe" in src
    assert "FILTER_H = 34" in src
    assert "PANE_MIN = 180" in src
    assert "PANE_MIN_PROP = 200" in src
    print("PASS kit density tokens")


def test_codegen_no_body_tip():
    path = os.path.join(_SUITE, "pages", "codegen.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "Generates OD C/H for CANopenNode" not in src
    assert "issues.setVisible(False)" in src
    print("PASS codegen no body tip")


def test_live_od_apply_once():
    path = os.path.join(_SUITE, "pages", "object_dict.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert 'panel_header("Live objects", status)' in src
    assert "apply_live" not in src
    assert "draft_next" in src
    print("PASS Live OD Apply once")


if __name__ == "__main__":
    test_build_eds_sidebar_flat()
    test_file_menu_eds()
    test_no_finetune()
    test_trace_simplified()
    test_kit_density_tokens()
    test_codegen_no_body_tip()
    test_live_od_apply_once()
    print("All UX simplify tests passed")
