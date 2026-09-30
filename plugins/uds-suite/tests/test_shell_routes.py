# -*- coding: utf-8 -*-
"""UDS Suite shell routes (no QApplication)."""

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


def test_nav_pages():
    ns = _exec_assigns(
        os.path.join(_SUITE, "app_shell.py"),
        {"NAV_PAGES", "FEATURE_ROUTE", "_WORKSPACE_DEFAULT"},
    )
    keys = [k for k, _ in ns["NAV_PAGES"]]
    assert keys == ["diagnose", "scan", "batch", "security", "setup"]
    assert "profiles" not in keys
    for leaf in (
            "session", "services", "did", "dtc", "sec_access", "flash",
            "profiles", "scan", "batch", "security", "setup"):
        assert leaf in ns["FEATURE_ROUTE"], leaf
    assert ns["_WORKSPACE_DEFAULT"]["diagnose"] == "services"
    print("PASS nav pages")


def test_sidebar_sections():
    path = os.path.join(_SUITE, "pages", "workspace_sidebar.py")
    ns = _exec_assigns(path, {"DIAGNOSE_SECTIONS"})
    keys = [s[0] for s in ns["DIAGNOSE_SECTIONS"]]
    assert keys == [
        "session", "services", "did", "dtc", "sec_access", "flash", "profiles"]
    for row in ns["DIAGNOSE_SECTIONS"]:
        assert len(row) == 4 and row[3]
    print("PASS sidebar sections")


def test_no_diagnose_tab_bar():
    path = os.path.join(_SUITE, "pages", "diagnose.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "QTabBar" not in src
    assert "_diagnose_tabs" not in src
    assert "set_editor_tabs" not in src
    assert "leaf_pages" in src
    print("PASS no diagnose tab bar")


def test_file_menu_symbols():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert 'addMenu("&File")' in src
    assert "Recent &Profiles" in src
    assert "def run_action(" in src
    assert "def _sync_next_hint(" in src
    assert "side_bar_enabled=True" in src
    assert "def _open_feature_tab(" in src
    assert "def _on_workbench_page(" in src
    assert "suite_tabs.mount_editor_tabs" in src
    print("PASS File menu symbols")


def test_no_cjk_shell():
    import re
    cjk = re.compile(r"[\u4e00-\u9fff]")
    for rel in ("app_shell.py", "session.py",
                os.path.join("pages", "workspace_sidebar.py")):
        path = os.path.join(_SUITE, rel)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        assert not cjk.search(src), "CJK in %s" % rel
    print("PASS no CJK shell")


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
        if name in ("scan.py", "batch.py", "security.py", "setup.py"):
            if "SuiteHint" in src and 'setObjectName("SuiteHint")' in src:
                # status muted OK; SuiteHint as caption object is banned on these pages
                bad.append("%s:SuiteHint" % name)
    assert not bad, "caption hints remain in %s" % bad
    print("PASS no caption hints")


def test_tool_strip_pages():
    for name in ("scan.py", "batch.py", "security.py"):
        path = os.path.join(_SUITE, "pages", name)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        assert "tool_strip" in src, name
    print("PASS tool_strip pages")


def test_strip_field_labels():
    """Bare numeric/hex fields in chrome rows must carry a visible label."""
    for name, needles in (
            ("security.py", ('strip_field(\n            "Samples"',
                             'strip_field(\n            "Interval"')),
            ("scan.py", ('strip_field("From"', 'strip_field("To"',
                         'strip_field(\n            "Offset"',
                         'strip_field("Timeout"'))):
        path = os.path.join(_SUITE, "pages", name)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        for n in needles:
            assert n in src or n.replace("\n            ", " ") in src, (
                "%s missing %s" % (name, n))
        assert "strip_field" in src
    # Shared helper exists
    shared = os.path.join(
        os.path.dirname(_SUITE), "_shared", "suite_ui.py")
    with open(shared, "r", encoding="utf-8") as f:
        ssrc = f.read()
    assert "def strip_field(" in ssrc
    print("PASS strip field labels")


def test_interop_symbols():
    shell = os.path.join(_SUITE, "app_shell.py")
    with open(shell, "r", encoding="utf-8") as f:
        src = f.read()
    assert "def goto_service_target(" in src
    assert "def goto_did_target(" in src
    assert "def goto_dtc_target(" in src
    assert "uds.goto_did" in src
    for name in ("diagnose.py", "scan.py"):
        path = os.path.join(_SUITE, "pages", name)
        with open(path, "r", encoding="utf-8") as f:
            page = f.read()
        assert "CustomContextMenu" in page, name
    diag = os.path.join(_SUITE, "pages", "diagnose.py")
    with open(diag, "r", encoding="utf-8") as f:
        dsrc = f.read()
    assert 'QLabel("Related")' in dsrc or "Related" in dsrc
    assert "did_rel_svc" in dsrc
    print("PASS interop symbols")


def test_leaf_activity_collision():
    """Single-leaf workspaces share Activity id — must still open tabs."""
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "_LEAF_KEYS" in src
    assert "def _is_leaf_feature(" in src
    ns = _exec_assigns(path, {"FEATURE_TITLES", "FEATURE_ROUTE"})
    for key in ("scan", "batch", "security", "setup"):
        assert key in ns["FEATURE_TITLES"], key
        assert key in ns["FEATURE_ROUTE"], key
    assert "diagnose" not in ns["FEATURE_TITLES"]
    print("PASS leaf/activity collision")


if __name__ == "__main__":
    test_nav_pages()
    test_sidebar_sections()
    test_no_diagnose_tab_bar()
    test_file_menu_symbols()
    test_no_cjk_shell()
    test_no_caption_hints()
    test_tool_strip_pages()
    test_strip_field_labels()
    test_interop_symbols()
    test_leaf_activity_collision()
    print("PASS shell routes")
