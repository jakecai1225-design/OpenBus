# -*- coding: utf-8 -*-
"""J1939 Suite shell routes (no QApplication)."""

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
    assert keys == ["analyzer", "transport", "diagnostics", "network"]
    assert "log" not in keys
    print("PASS nav pages")


def test_shell_symbols():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "suite_chrome.build_workbench" in src
    assert "side_bar_enabled=True" in src
    assert "def run_action(" in src
    assert "def _sync_next_hint(" in src
    assert "def _open_feature_tab(" in src
    assert "def _on_workbench_page(" in src
    assert "suite_tabs.mount_editor_tabs" in src
    assert 'addMenu("&File")' in src
    assert "Load DBC" in src
    assert "j1939.load_dbc" in src
    print("PASS shell symbols")


def test_ui_density():
    shared = os.path.join(os.path.dirname(_SUITE), "_shared", "suite_ui.py")
    ns = _exec_assigns(
        shared, {"CTRL_H", "STRIP_PAD_V", "STRIP_EDGE", "TOOL_H", "FILTER_H"})
    assert ns["TOOL_H"] == ns["CTRL_H"] + 2 * ns["STRIP_PAD_V"] + ns["STRIP_EDGE"]
    assert ns["FILTER_H"] == ns["TOOL_H"]
    path = os.path.join(_SUITE, "pages", "_ui.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "from _shared.suite_ui import" in src
    print("PASS ui density")


def test_no_cjk():
    cjk = re.compile(r"[\u4e00-\u9fff]")
    for rel in ("app_shell.py", "session.py",
                os.path.join("pages", "_ui.py"),
                os.path.join("pages", "views.py")):
        path = os.path.join(_SUITE, rel)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        assert not cjk.search(src), "CJK in %s" % rel
    print("PASS no CJK")


def test_session_hint():
    from session import SharedSession
    s = SharedSession()
    lab, act, _kw = s.next_hint()
    assert act == "j1939.load_dbc"
    s.advance_next_hint()
    s.note_dbc("")
    lab, act, kw = s.next_hint()
    assert kw.get("page") == "analyzer"
    print("PASS session hint")


def test_analyzer_load_dbc_api():
    path = os.path.join(_SUITE, "pages", "analyzer.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "def load_dbc(" in src
    assert "tool_strip" in src
    assert "inline_filter" in src
    assert "CustomContextMenu" in src
    assert "def select_pgn(" in src
    assert "_dec_menu" in src or "dec_tree.setContextMenuPolicy" in src
    print("PASS analyzer API")


def test_interop_symbols():
    shell = os.path.join(_SUITE, "app_shell.py")
    with open(shell, "r", encoding="utf-8") as f:
        src = f.read()
    assert "def goto_pgn_target(" in src
    assert "j1939.goto_pgn" in src
    views = os.path.join(_SUITE, "pages", "views.py")
    with open(views, "r", encoding="utf-8") as f:
        vsrc = f.read()
    assert "CustomContextMenu" in vsrc
    assert "Open Live (PGN)" in vsrc or "Open Live" in vsrc
    print("PASS interop symbols")


def test_leaf_activity_collision():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "_LEAF_KEYS" in src
    assert "def _is_leaf_feature(" in src
    ns = _exec_assigns(path, {"FEATURE_TITLES", "FEATURE_ROUTE"})
    for key in ("analyzer", "transport", "diagnostics", "network"):
        assert key in ns["FEATURE_TITLES"]
        assert key in ns["FEATURE_ROUTE"]
    print("PASS leaf/activity collision")


if __name__ == "__main__":
    test_nav_pages()
    test_shell_symbols()
    test_ui_density()
    test_no_cjk()
    test_session_hint()
    test_analyzer_load_dbc_api()
    test_interop_symbols()
    test_leaf_activity_collision()
    print("PASS shell routes")
