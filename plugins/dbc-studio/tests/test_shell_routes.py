# -*- coding: utf-8 -*-
"""DBC Studio shell routes (no QApplication)."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)


def _exec_assigns(path: str, wanted: set[str]) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        tree = ast.parse(f.read(), filename=path)
    ns: dict = {}
    for node in tree.body:
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = []
            if isinstance(node, ast.Assign):
                targets = [t.id for t in node.targets if isinstance(t, ast.Name)]
            elif isinstance(node.target, ast.Name):
                targets = [node.target.id]
            if any(n in wanted for n in targets):
                exec(compile(ast.Module([node], type_ignores=[]), path, "exec"), ns)
    missing = wanted - ns.keys()
    if missing:
        raise AssertionError("missing: %s" % missing)
    return ns


def test_four_pillars():
    ns = _exec_assigns(
        os.path.join(_SUITE, "app_shell.py"),
        {"NAV_PAGES", "FEATURE_ROUTE", "_WORKSPACE_DEFAULT"},
    )
    keys = [k for k, _ in ns["NAV_PAGES"]]
    assert keys == ["edit", "analyze", "integrate", "deliver"]
    assert len(ns["NAV_PAGES"]) == 4
    for leaf in (
            "editor", "valuetables", "attributes",
            "matrix", "timing", "validate",
            "compare", "merge", "export", "library"):
        assert leaf in ns["FEATURE_ROUTE"], leaf
    print("PASS four pillars")


def test_sidebar_sections():
    path = os.path.join(_SUITE, "pages", "workspace_sidebar.py")
    ns = _exec_assigns(
        path,
        {"EDIT_SECTIONS", "ANALYZE_SECTIONS",
         "INTEGRATE_SECTIONS", "DELIVER_SECTIONS"},
    )
    assert [s[0] for s in ns["EDIT_SECTIONS"]] == [
        "editor", "valuetables", "attributes"]
    assert [s[0] for s in ns["ANALYZE_SECTIONS"]] == [
        "matrix", "timing", "validate"]
    assert [s[0] for s in ns["INTEGRATE_SECTIONS"]] == [
        "compare", "merge"]
    assert [s[0] for s in ns["DELIVER_SECTIONS"]] == [
        "export", "library"]
    print("PASS sidebar sections")


def test_file_menu_symbols():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert 'addMenu("&File")' in src
    assert "Recent &DBCs" in src
    assert "def run_action(" in src
    assert "def _sync_next_hint(" in src
    # Document cluster not on chrome row
    assert 'Open DBC (Ctrl+O)"' not in src or "File" in src
    assert "_doc_btns" in src
    assert "set_editor_title" in src
    print("PASS File menu symbols")


if __name__ == "__main__":
    test_four_pillars()
    test_sidebar_sections()
    test_file_menu_symbols()
    print("PASS shell routes")
