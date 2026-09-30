# -*- coding: utf-8 -*-
"""UDS Suite density tokens via shared suite_ui (no QApplication)."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_SHARED = os.path.join(os.path.dirname(_SUITE), "_shared", "suite_ui.py")


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


def test_density_tokens():
    ns = _exec_assigns(
        _SHARED, {"CTRL_H", "STRIP_PAD_V", "STRIP_EDGE", "TOOL_H", "FILTER_H"})
    assert ns["TOOL_H"] == ns["CTRL_H"] + 2 * ns["STRIP_PAD_V"] + ns["STRIP_EDGE"]
    assert ns["FILTER_H"] == ns["TOOL_H"]
    assert ns["TOOL_H"] == 38
    path = os.path.join(_SUITE, "pages", "_ui.py")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    assert "from _shared.suite_ui import" in src
    assert "apply_uds_chrome" in src
    print("PASS uds density")


if __name__ == "__main__":
    test_density_tokens()
