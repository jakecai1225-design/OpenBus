# -*- coding: utf-8 -*-
"""CANopen Suite activity / tab routing (no QApplication / sin)."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
for p in (_SUITE, _PLUGINS):
    if p not in sys.path:
        sys.path.insert(0, p)


def _exec_assigns(path: str, wanted: set[str], extra: set[str] | None = None) -> dict:
    extra = extra or set()
    with open(path, "r", encoding="utf-8") as f:
        tree = ast.parse(f.read(), filename=path)
    ns: dict = {"Optional": object}
    for node in tree.body:
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = []
            if isinstance(node, ast.Assign):
                targets = [t.id for t in node.targets if isinstance(t, ast.Name)]
            elif isinstance(node.target, ast.Name):
                targets = [node.target.id]
            if any(n in wanted or n in extra for n in targets):
                exec(compile(ast.Module([node], type_ignores=[]), path, "exec"), ns)
        elif isinstance(node, ast.FunctionDef) and node.name in extra:
            exec(compile(ast.Module([node], type_ignores=[]), path, "exec"), ns)
    missing = wanted - ns.keys()
    if missing:
        raise AssertionError("missing from %s: %s" % (path, missing))
    return ns


_NS = _exec_assigns(
    os.path.join(_SUITE, "app_shell.py"),
    {
        "NAV_PAGES", "FEATURE_ROUTE", "_LIB_FEATURES", "_WORKSPACE_DEFAULT",
        "_TAB_CANONICAL",
    },
    extra={"normalize_open_tabs"},
)
if "normalize_open_tabs" not in _NS:
    raise AssertionError("normalize_open_tabs not loaded")
_SB = _exec_assigns(
    os.path.join(_SUITE, "pages", "workspace_sidebar.py"),
    {
        "EDS_SECTIONS", "EDS_NESTED",
        "LIVE_SECTIONS", "LIVE_NESTED",
        "TRACE_SECTIONS", "CODE_SECTIONS",
        "DEVICE_SECTIONS", "NETWORK_SECTIONS",
        "PROJECT_SECTIONS",
    },
    extra={"build_eds_sidebar", "build_project_sidebar"},
)
NAV_PAGES = _NS["NAV_PAGES"]
FEATURE_ROUTE = _NS["FEATURE_ROUTE"]
_LIB_FEATURES = _NS["_LIB_FEATURES"]
_WORKSPACE_DEFAULT = _NS["_WORKSPACE_DEFAULT"]
_TAB_CANONICAL = _NS["_TAB_CANONICAL"]
normalize_open_tabs = _NS["normalize_open_tabs"]
EDS_SECTIONS = _SB["EDS_SECTIONS"]
LIVE_SECTIONS = _SB["LIVE_SECTIONS"]
TRACE_SECTIONS = _SB["TRACE_SECTIONS"]
CODE_SECTIONS = _SB["CODE_SECTIONS"]
DEVICE_SECTIONS = _SB["DEVICE_SECTIONS"]
NETWORK_SECTIONS = _SB["NETWORK_SECTIONS"]


def test_activity_layout():
    keys = [k for k, _ in NAV_PAGES]
    assert keys == ["eds", "device", "trace", "code"]
    assert "project" not in keys
    assert "library" not in keys
    assert "setup" not in keys


def test_profiles_under_eds():
    for key in _LIB_FEATURES:
        ws, _idx = FEATURE_ROUTE[key]
        assert ws == "eds", key
    labels = [s[0] for s in EDS_SECTIONS]
    assert labels == ["eds_dict", "profiles", "eds_pdo", "eds_check"]
    assert "eds_codegen" not in labels
    assert FEATURE_ROUTE["eds_codegen"][0] == "code"
    assert FEATURE_ROUTE["eds_pdo"][0] == "eds"


def test_live_and_trace_pillars():
    assert [s[0] for s in LIVE_SECTIONS] == [
        "od", "network_scan", "network_nmt"]
    assert [s[0] for s in DEVICE_SECTIONS] == [
        "od", "network_scan", "network_nmt"]
    assert [s[0] for s in TRACE_SECTIONS] == ["monitor"]
    assert [s[0] for s in CODE_SECTIONS] == ["eds_codegen"]
    assert FEATURE_ROUTE["network_scan"][0] == "device"
    assert FEATURE_ROUTE["monitor"][0] == "trace"
    assert FEATURE_ROUTE["od"][0] == "device"
    assert "build_eds_sidebar" in _SB
    assert dict((k, lab) for k, lab, _t in EDS_SECTIONS)["eds_pdo"] == "PDO map"
    assert dict((k, lab) for k, lab, _t in LIVE_SECTIONS)["od"] == "Live OD"


def test_workspace_defaults():
    for ws, _title in NAV_PAGES:
        leaf = _WORKSPACE_DEFAULT[ws]
        assert FEATURE_ROUTE[leaf][0] == ws


def test_tab_canonical_collapses_views():
    assert _TAB_CANONICAL["eds_check"] == "eds_dict"
    assert _TAB_CANONICAL["project"] == "eds_dict"
    assert _TAB_CANONICAL["lib_301"] == "profiles"
    flat = normalize_open_tabs(["eds_dict", "eds_check", "lib_301", "monitor"])
    assert flat == ["eds_dict", "profiles", "monitor"]
    legacy = normalize_open_tabs({
        "eds": ["eds_dict", "eds_pdo"],
        "library": ["lib_301"],
        "network": ["monitor"],
        "setup": ["setup"],
    })
    assert "eds_dict" in legacy
    assert "eds_pdo" in legacy
    assert "profiles" in legacy
    assert "monitor" in legacy
    assert normalize_open_tabs(None) == []


def test_eds_stack_no_project_page():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert '("project", self._pages["project"])' not in src
    assert "project.build" not in src
    assert '("eds_dict", self._pages["eds"])' in src
    assert '("profiles", self._pages["library"])' in src
    print("PASS eds stack no project")


def test_no_cjk_in_suite_chrome():
    roots = [
        os.path.join(_SUITE, "app_shell.py"),
        os.path.join(_SUITE, "pages", "workspace_sidebar.py"),
        os.path.join(_SUITE, "pages", "project.py"),
        os.path.join(_SUITE, "pages", "eds_editor.py"),
    ]
    for path in roots:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        for ch in text:
            if "\u4e00" <= ch <= "\u9fff":
                raise AssertionError("CJK in %s: U+%04X" % (path, ord(ch)))


if __name__ == "__main__":
    test_activity_layout()
    test_profiles_under_eds()
    test_live_and_trace_pillars()
    test_workspace_defaults()
    test_tab_canonical_collapses_views()
    test_eds_stack_no_project_page()
    test_no_cjk_in_suite_chrome()
    print("PASS shell routes")
