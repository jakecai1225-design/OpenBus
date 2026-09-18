# -*- coding: utf-8 -*-
"""Import / AST smoke test for Bus Security (no GUI, no host APIs)."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)


def _has_cjk(text: str) -> bool:
    for ch in text:
        o = ord(ch)
        if (
            0x4E00 <= o <= 0x9FFF
            or 0x3400 <= o <= 0x4DBF
            or 0xF900 <= o <= 0xFAFF
            or 0x3000 <= o <= 0x303F
            or 0xFF00 <= o <= 0xFFEF
        ):
            return True
    return False


def test_session_defaults():
    from session import SharedSession

    s = SharedSession()
    assert s.default_interval_ms == 50
    assert s.default_send_limit == 500
    stopped = []
    s.register_stop_handler(lambda: stopped.append(1))
    s.stop_all()
    assert stopped == [1]
    s.apply_defaults(interval_ms=100, send_limit=10, load_pct=25.0, gap_ms=5)
    assert s.default_interval_ms == 100
    assert s.default_send_limit == 10
    assert s.default_load_pct == 25.0
    assert s.default_gap_ms == 5
    print("PASS SharedSession defaults + stop_all")


def test_ast_all_py_zero_cjk():
    py_files = []
    for dirpath, _dirs, files in os.walk(_SUITE):
        if "__pycache__" in dirpath:
            continue
        for name in files:
            if name.endswith(".py"):
                py_files.append(os.path.join(dirpath, name))
    assert len(py_files) >= 8, "expected suite python files, got %d" % len(py_files)
    for path in py_files:
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        try:
            tree = ast.parse(src, filename=path)
        except SyntaxError as e:
            raise AssertionError("AST fail %s: %s" % (path, e)) from e
        assert isinstance(tree, ast.AST)
        assert not _has_cjk(src), "CJK in %s" % os.path.relpath(path, _SUITE)
    print("PASS AST-import all modules + zero CJK (%d files)" % len(py_files))


if __name__ == "__main__":
    test_session_defaults()
    test_ast_all_py_zero_cjk()
    print("All bus-security import smoke tests passed")
