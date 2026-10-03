# -*- coding: utf-8 -*-
"""AUTOSAR Studio shell routes (no QApplication)."""

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
    assert keys == ["project", "config", "com", "bus", "setup"]
    assert len(keys) <= 5
    assert "validate" not in keys
    assert "log" not in keys
    print("PASS nav pages")


def test_shell_symbols():
    path = os.path.join(_SUITE, "app_shell.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "suite_chrome.build_workbench" in src
    assert "side_bar_enabled=True" in src
    assert "suite_tabs.mount_editor_tabs" in src
    assert "def _open_feature_tab(" in src
    assert "def _on_workbench_page(" in src
    assert "def _on_activity_clicked(" in src
    assert "attach_layout_toggles_to_menubar" in src
    assert "_mount_chrome" not in src
    print("PASS shell symbols")


def test_frameless_menubar_chrome():
    """Shared helper must collapse OS title bar into one VS Code menu row."""
    shared = os.path.normpath(os.path.join(_SUITE, "..", "_shared", "suite_chrome.py"))
    with open(shared, "r", encoding="utf-8") as f:
        src = f.read()
    assert "FramelessWindowHint" in src
    assert "CHROME_BTN_H" in src
    assert "WinMinBtn" in src
    assert "WinMaxBtn" in src
    assert "WinCloseBtn" in src
    assert "startSystemMove" in src
    assert "_embed_menu_chrome" in src
    assert "SuiteMenuChromeSlot" in src
    assert "SuiteMenuChrome" in src
    assert "SuiteMenuButton" in src
    assert "SuiteMenuBarSentinel" in src
    assert "_harvest_top_menus" in src
    assert "begin_suite_menubar" in src
    assert "reload_live_modules" in src
    assert "_park_popup_menu" in src
    assert "WindowType.Popup" in src
    assert "MENU_ROW_H = 35" in src
    assert "class SuiteMenuButton(QPushButton)" not in src
    shell = open(os.path.join(_SUITE, "app_shell.py"), encoding="utf-8").read()
    assert "begin_suite_menubar" in shell
    # VS Code text menubar tops: File Edit View Run Help
    assert 'bar.addMenu("&File")' in shell
    assert 'bar.addMenu("&Edit")' in shell
    assert 'bar.addMenu("&View")' in shell
    assert 'bar.addMenu("&Run")' in shell
    assert 'bar.addMenu("&Help")' in shell
    assert 'bar.addMenu("&Import")' not in shell
    assert 'bar.addMenu("&Validate")' not in shell
    print("PASS frameless menubar chrome")


def test_validate_in_config():
    ns = _exec_assigns(
        os.path.join(_SUITE, "app_shell.py"),
        {"FEATURE_ROUTE", "FEATURE_TITLES", "_WORKSPACE_DEFAULT"})
    for key in ("validate", "timing", "compare", "merge", "export"):
        assert ns["FEATURE_ROUTE"][key][0] == "config", key
        assert key in ns["FEATURE_TITLES"]
    assert ns["_WORKSPACE_DEFAULT"]["config"] == "bsw"
    path = os.path.join(_SUITE, "pages", "config_sidebar.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert '("validate",' in src
    assert '("export",' in src
    assert '_QUALITY_LEAVES' in src
    assert 'codicons' in src
    assert 'folder' in src
    assert 'expand_matches' in src
    print("PASS validate in config")


def test_config_explorer_shape():
    """VS Code Explorer: folder containers + leaf icons; groups stay collapsed."""
    path = os.path.join(_SUITE, "pages", "config_sidebar.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert '_make_folder' in src
    assert '_make_leaf' in src
    assert 'Quality' in src
    assert 'setRootIsDecorated(True)' in src
    assert 'gitem.setExpanded(bool(q) and expand_matches)' in src
    print("PASS config explorer shape")


def test_no_cjk():
    cjk = re.compile(r"[\u4e00-\u9fff]")
    for rel in ("app_shell.py", "main.py",
                os.path.join("pages", "config_sidebar.py"),
                os.path.join("pages", "workspace_sidebar.py")):
        path = os.path.join(_SUITE, rel)
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        assert not cjk.search(src), "CJK in %s" % rel
    print("PASS no CJK")


if __name__ == "__main__":
    test_nav_pages()
    test_shell_symbols()
    test_frameless_menubar_chrome()
    test_validate_in_config()
    test_config_explorer_shape()
    test_no_cjk()
    print("PASS shell routes")
