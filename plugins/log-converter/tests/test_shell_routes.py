# -*- coding: utf-8 -*-
"""Route table sanity for Log Converter AppShell."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)


def test_routes():
    # Import only constants — avoid constructing QMainWindow without Qt app.
    import importlib.util

    path = os.path.join(_SUITE, "app_shell.py")
    # Parse FEATURE maps via exec of module without Qt show
    ns = {}
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()
    # Extract by importing after stubbing heavy deps is fragile; use AST constants
    import ast

    tree = ast.parse(src)
    got = {}
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for t in node.targets:
                if isinstance(t, ast.Name) and t.id in (
                        "NAV_PAGES", "FEATURE_ROUTE", "FEATURE_TITLES"):
                    got[t.id] = ast.literal_eval(node.value)
    assert "NAV_PAGES" in got
    assert [k for k, _ in got["NAV_PAGES"]] == [
        "convert", "batch", "inspect", "jobs"]
    for key in ("convert", "batch", "inspect", "jobs"):
        assert key in got["FEATURE_ROUTE"]
        assert key in got["FEATURE_TITLES"]
    print("PASS shell routes")


if __name__ == "__main__":
    test_routes()
    print("All log-converter route tests passed")
