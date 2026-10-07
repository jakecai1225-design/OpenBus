# -*- coding: utf-8 -*-
"""Import / AST smoke test for Log Converter."""

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


def test_formats_catalog():
    from formats import ENGINE_FORMATS, FORMAT_CATALOG, detect_format, suggest_target

    assert set(ENGINE_FORMATS) == {"blf", "asc", "csv", "pcap", "trc"}
    assert len(FORMAT_CATALOG) >= 5
    assert detect_format(r"C:\tmp\demo.blf") == "blf"
    assert detect_format(r"C:\tmp\demo.pcapng") == "pcap"
    assert suggest_target(r"C:\tmp\a.blf", "asc").endswith(".asc")
    print("PASS formats catalog")


if __name__ == "__main__":
    test_ast_zero_cjk()
    test_formats_catalog()
    print("All log-converter smoke tests passed")
