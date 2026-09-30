# -*- coding: utf-8 -*-
"""Shared suite_ui density + re-export contract (no QApplication)."""

from __future__ import annotations

import ast
import os
import re
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SHARED = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SHARED)
_ROOT = os.path.dirname(_PLUGINS)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)


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


def test_shared_density():
    path = os.path.join(_SHARED, "suite_ui.py")
    ns = _exec_assigns(path, {"CTRL_H", "STRIP_PAD_V", "STRIP_EDGE", "TOOL_H", "FILTER_H"})
    assert ns["TOOL_H"] == ns["CTRL_H"] + 2 * ns["STRIP_PAD_V"] + ns["STRIP_EDGE"]
    assert ns["FILTER_H"] == ns["TOOL_H"]
    assert ns["TOOL_H"] == 38
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "def polish_work_surface" in src
    assert "def style_page_tabs" in src
    assert "def muted_label" in src
    print("PASS shared density")


def test_suite_wrappers():
    for suite, alias in (
            ("uds-suite", "apply_uds_chrome"),
            ("obd-suite", "apply_obd_chrome"),
            ("j1939-suite", "apply_j1939_chrome"),
            ("eds-studio", "apply_eds_chrome"),
            ("autosar-suite", "apply_autosar_chrome"),
            ("ethercat-suite", "apply_ethercat_chrome")):
        path = os.path.join(_ROOT, "plugins", suite, "pages", "_ui.py")
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        assert "from _shared.suite_ui import" in src, suite
        assert alias in src, suite
    print("PASS wrappers")


def test_no_hardcoded_caption_hex():
    cjk = re.compile(r"[\u4e00-\u9fff]")
    bad_hex = ("color:#90A4AE", "color:#78909c", "color:#2e7d32")
    suites = ("obd-suite", "j1939-suite", "uds-suite")
    for suite in suites:
        pages = os.path.join(_ROOT, "plugins", suite, "pages")
        for name in os.listdir(pages):
            if not name.endswith(".py"):
                continue
            path = os.path.join(pages, name)
            with open(path, "r", encoding="utf-8") as f:
                src = f.read()
            for hx in bad_hex:
                assert hx not in src, "%s/%s has %s" % (suite, name, hx)
            if name == "_ui.py":
                assert not cjk.search(src), "CJK in %s" % suite
    print("PASS no hardcoded caption hex")


def test_chrome_toolbar_density():
    chrome = os.path.join(_SHARED, "suite_chrome.py")
    with open(chrome, "r", encoding="utf-8") as f:
        src = f.read()
    assert "CHROME_H" in src and "setFixedHeight(CHROME_H)" in src
    assert "bar.setObjectName(\"SuiteToolStrip\")" in src
    assert "STRIP_PAD_V" in src
    ui = os.path.join(_SHARED, "suite_ui.py")
    with open(ui, "r", encoding="utf-8") as f:
        usrc = f.read()
    assert "QWidget#SuiteToolbar" in usrc
    print("PASS chrome toolbar density")


if __name__ == "__main__":
    test_shared_density()
    test_suite_wrappers()
    test_no_hardcoded_caption_hex()
    test_chrome_toolbar_density()
    print("PASS suite_ui contract")
