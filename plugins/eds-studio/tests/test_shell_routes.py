# -*- coding: utf-8 -*-
"""EDS Studio shell routes (no QApplication)."""

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


def test_three_pillars():
    ns = _exec_assigns(
        os.path.join(_SUITE, "app_shell.py"),
        {"NAV_PAGES", "FEATURE_ROUTE", "_WORKSPACE_DEFAULT"},
    )
    keys = [k for k, _ in ns["NAV_PAGES"]]
    assert keys == ["edit", "analyze", "deliver"]
    assert len(ns["NAV_PAGES"]) == 3
    for leaf in (
            "editor", "pdo",
            "validate", "timing", "compare",
            "export", "library"):
        assert leaf in ns["FEATURE_ROUTE"], leaf
    print("PASS three pillars")


def test_sidebar_sections():
    path = os.path.join(_SUITE, "pages", "workspace_sidebar.py")
    ns = _exec_assigns(
        path,
        {"EDIT_SECTIONS", "ANALYZE_SECTIONS", "DELIVER_SECTIONS"},
    )
    assert [s[0] for s in ns["EDIT_SECTIONS"]] == ["editor", "pdo"]
    assert [s[0] for s in ns["ANALYZE_SECTIONS"]] == [
        "validate", "timing", "compare"]
    assert [s[0] for s in ns["DELIVER_SECTIONS"]] == ["export", "library"]
    for group in (
            ns["EDIT_SECTIONS"], ns["ANALYZE_SECTIONS"],
            ns["DELIVER_SECTIONS"]):
        for row in group:
            assert len(row) == 4, row
            assert row[3], "missing icon for %s" % row[0]
    print("PASS sidebar sections")


def test_file_menu_symbols():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert 'addMenu("&File")' in src
    assert "Recent &EDS" in src
    assert "def run_action(" in src
    assert "def _sync_next_hint(" in src
    assert "set_editor_tabs" in src
    assert "def _open_feature_tab(" in src
    assert "def _close_feature_tab(" in src
    assert "def normalize_open_tabs(" in src
    assert "def goto_editor_target(" in src
    assert "def _on_workbench_page(" in src
    assert "chrome_tabs" not in src
    print("PASS File menu symbols")


def test_normalize_open_tabs():
    import sys
    plugins = os.path.dirname(_SUITE)
    if plugins not in sys.path:
        sys.path.insert(0, plugins)
    from app_shell import normalize_open_tabs, _is_leaf_feature, FEATURE_TITLES
    assert normalize_open_tabs(None) == []
    assert normalize_open_tabs(["editor", "validate", "bogus"]) == [
        "editor", "validate"]
    assert normalize_open_tabs(["edit", "pdo_map", "editor"]) == [
        "pdo", "editor"]
    assert normalize_open_tabs({"edit": ["pdo"], "x": ["validate"]}) == [
        "pdo", "validate"]
    assert _is_leaf_feature("editor")
    assert not _is_leaf_feature("edit")
    assert "edit" not in FEATURE_TITLES
    print("PASS normalize_open_tabs")


def test_ui_density():
    path = os.path.join(_SUITE, "pages", "_ui.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "from _shared.suite_ui import" in src
    assert "apply_eds_chrome" in src
    shared = os.path.join(os.path.dirname(_SUITE), "_shared", "suite_ui.py")
    with open(shared, "r", encoding="utf-8") as f:
        ssrc = f.read()
    assert "TOOL_H = CTRL_H + 2 * STRIP_PAD_V + STRIP_EDGE" in ssrc
    print("PASS ui density")


def test_document_focus_next():
    sys.path.insert(0, os.path.join(_SUITE, ".."))
    from document import EdsDocument
    doc = EdsDocument()
    label, action, _kw = doc.next_hint()
    assert action == "eds.open"
    doc.set_focus(0x2000, 1)
    assert doc.focus_index == 0x2000
    assert doc.focus_subindex == 1
    seen = []
    doc.on_focus(lambda: seen.append(1))
    doc.set_focus(0x2001, 0)
    assert seen == [1]
    print("PASS document focus next")


def test_no_cjk_in_shell():
    import re
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert not re.search(r"[\u4e00-\u9fff]", src), "CJK in app_shell"
    print("PASS no CJK in shell")


def test_pages_no_chrome_tabs_attr():
    pages = os.path.join(_SUITE, "pages")
    bad = []
    for name in ("editor.py", "library.py"):
        path = os.path.join(pages, name)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        if "chrome_tabs" in src:
            bad.append(name)
    assert not bad, "chrome_tabs still set in %s" % bad
    print("PASS no chrome_tabs attr")


if __name__ == "__main__":
    test_three_pillars()
    test_sidebar_sections()
    test_file_menu_symbols()
    test_normalize_open_tabs()
    test_ui_density()
    test_document_focus_next()
    test_no_cjk_in_shell()
    test_pages_no_chrome_tabs_attr()
    print("PASS shell routes")
