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
    for group in (
            ns["EDIT_SECTIONS"], ns["ANALYZE_SECTIONS"],
            ns["INTEGRATE_SECTIONS"], ns["DELIVER_SECTIONS"]):
        for row in group:
            assert len(row) == 4, row
            assert row[3], "missing icon for %s" % row[0]
    print("PASS sidebar sections")


def test_activity_icon_assets():
    """Activity keys must resolve to real SVGs — never the letter-A placeholder."""
    icons = os.path.normpath(os.path.join(_SUITE, "..", "_shared", "icons"))
    edit_svg = os.path.join(icons, "edit.svg")
    with open(edit_svg, "r", encoding="utf-8") as f:
        body = f.read()
    assert "Capital A" not in body
    assert "L6.5 3.5" not in body  # old letter-A path
    assert "11.2 2.3" in body or "pencil" in body.lower() or "M11" in body
    aliases = {
        "edit": "edit.svg",
        "analyze": "analyze.svg",
        "integrate": "integrate.svg",
        "deliver": "deliver.svg",
    }
    for key, fname in aliases.items():
        path = os.path.join(icons, fname)
        assert os.path.isfile(path), "%s → missing %s" % (key, path)
    print("PASS activity icon assets")


def test_file_menu_symbols():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert 'addMenu("&File")' in src
    assert "Recent &DBCs" in src
    assert "def run_action(" in src
    assert "def _sync_next_hint(" in src
    assert "_doc_btns" in src
    assert "set_editor_tabs" in src
    assert "def _open_feature_tab(" in src
    assert "def _close_feature_tab(" in src
    assert "def normalize_open_tabs(" in src
    assert "def goto_value_tables(" in src
    assert "def goto_attributes(" in src
    assert "def _on_workbench_page(" in src
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
    assert normalize_open_tabs(["edit", "value_tables", "editor"]) == [
        "valuetables", "editor"]
    assert normalize_open_tabs({"edit": ["attributes"], "x": ["matrix"]}) == [
        "attributes", "matrix"]
    assert _is_leaf_feature("editor")
    assert not _is_leaf_feature("edit")
    assert "edit" not in FEATURE_TITLES
    print("PASS normalize_open_tabs")


def test_ui_density():
    path = os.path.join(_SUITE, "pages", "_ui.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "STRIP_PAD_V = 4" in src
    assert "TOOL_H = CTRL_H + 2 * STRIP_PAD_V + STRIP_EDGE" in src
    assert "FILTER_H = TOOL_H" in src
    block = src.split("QWidget#SuiteToolStrip")[1].split(
        "QWidget#SuiteInlineFilter")[0]
    assert "max-height" not in block
    print("PASS ui density")


def test_no_caption_hints():
    pages = os.path.join(_SUITE, "pages")
    bad = []
    for name in os.listdir(pages):
        if not name.endswith(".py"):
            continue
        path = os.path.join(pages, name)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        if "color:#78909c;font-size:12px" in src:
            bad.append(name)
    assert not bad, "caption hints remain in %s" % bad
    print("PASS no caption hints")


if __name__ == "__main__":
    test_four_pillars()
    test_sidebar_sections()
    test_activity_icon_assets()
    test_file_menu_symbols()
    test_normalize_open_tabs()
    test_ui_density()
    test_no_caption_hints()
    print("PASS shell routes")
