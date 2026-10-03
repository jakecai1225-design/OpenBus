# -*- coding: utf-8 -*-
"""EtherCAT Suite shell routes (no QApplication)."""

from __future__ import annotations

import ast
import os
import re
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


def test_nav_pages():
    ns = _exec_assigns(os.path.join(_SUITE, "app_shell.py"), {"NAV_PAGES"})
    keys = [k for k, _ in ns["NAV_PAGES"]]
    assert keys == ["network", "objects", "timing", "setup"]
    assert len(keys) <= 5
    assert "log" not in keys
    print("PASS nav pages")


def test_shell_symbols():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "suite_chrome.build_workbench" in src
    assert "side_bar_enabled=True" in src
    assert "def run_action(" in src
    assert "def _open_feature_tab(" in src
    assert "def _on_workbench_page(" in src
    assert "suite_tabs.mount_editor_tabs" in src
    assert "attach_layout_toggles_to_menubar" in src
    assert 'addMenu("&File")' in src
    assert "set_editor_title" not in src
    print("PASS shell symbols")


def test_feature_routes():
    ns = _exec_assigns(
        os.path.join(_SUITE, "app_shell.py"),
        {"FEATURE_ROUTE", "FEATURE_TITLES", "_WORKSPACE_DEFAULT"})
    for key in ("topology", "frames", "pdo", "coe", "esi", "dc", "setup"):
        assert key in ns["FEATURE_TITLES"]
        assert key in ns["FEATURE_ROUTE"]
    assert ns["FEATURE_ROUTE"]["topology"][0] == "network"
    assert ns["FEATURE_ROUTE"]["pdo"][0] == "objects"
    assert ns["FEATURE_ROUTE"]["dc"][0] == "timing"
    assert ns["_WORKSPACE_DEFAULT"]["network"] == "topology"
    print("PASS feature routes")


def test_no_cjk():
    cjk = re.compile(r"[\u4e00-\u9fff]")
    for rel in ("app_shell.py", "session.py",
                os.path.join("pages", "_ui.py"),
                os.path.join("pages", "workspace_sidebar.py"),
                os.path.join("pages", "network.py"),
                os.path.join("pages", "frames.py"),
                os.path.join("pages", "esi.py")):
        path = os.path.join(_SUITE, rel)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        assert not cjk.search(src), "CJK in %s" % rel
    print("PASS no CJK")


def test_pages_no_chrome_tabs():
    for name in ("network.py", "frames.py", "esi.py"):
        path = os.path.join(_SUITE, "pages", name)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        assert "parent._net_tabs" not in src
        assert "parent._frame_tabs" not in src
        assert "parent._esi_tabs" not in src
        assert "SuiteEditorTabs" not in src
    print("PASS pages no chrome tabs")


if __name__ == "__main__":
    test_nav_pages()
    test_shell_symbols()
    test_feature_routes()
    test_no_cjk()
    test_pages_no_chrome_tabs()
    print("PASS shell routes")
