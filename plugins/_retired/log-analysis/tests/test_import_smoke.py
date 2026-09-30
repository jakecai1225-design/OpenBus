# -*- coding: utf-8 -*-
"""Import / AST smoke test for Log Analysis (no GUI, no host APIs)."""

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


def test_core_log_io_import():
    from core import load_any, read_asc, write_asc  # noqa: F401
    assert callable(load_any)
    assert callable(read_asc)
    assert callable(write_asc)
    print("PASS core.log_io import")


def test_ast_all_py_zero_cjk():
    py_files = []
    for dirpath, _dirs, files in os.walk(_SUITE):
        if "__pycache__" in dirpath:
            continue
        for name in files:
            if name.endswith(".py"):
                py_files.append(os.path.join(dirpath, name))
    assert len(py_files) >= 10, "expected suite python files, got %d" % len(py_files)
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
    test_core_log_io_import()
    test_ast_all_py_zero_cjk()
    print("All log-analysis import smoke tests passed")
