# -*- coding: utf-8 -*-
"""Import / AST smoke test for Protocol Hub."""

from __future__ import annotations

import ast
import os

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)


def _has_cjk(text: str) -> bool:
    for ch in text:
        if 0x4E00 <= ord(ch) <= 0x9FFF:
            return True
    return False


def test_ast_zero_cjk():
    n = 0
    for dirpath, _dirs, files in os.walk(_SUITE):
        if "__pycache__" in dirpath:
            continue
        for name in files:
            if not name.endswith(".py"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, "r", encoding="utf-8") as f:
                src = f.read()
            ast.parse(src, filename=path)
            assert not _has_cjk(src), "CJK in %s" % path
            n += 1
    assert n >= 8
    print("PASS AST + zero CJK (%d files)" % n)


if __name__ == "__main__":
    test_ast_zero_cjk()
    print("All protocol-hub smoke tests passed")
