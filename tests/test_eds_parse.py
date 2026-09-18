# -*- coding: utf-8 -*-
"""Unit tests for CANopen Suite EDS parser."""

from __future__ import annotations

import ast
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUITE = os.path.join(ROOT, "plugins", "canopen-suite")
sys.path.insert(0, SUITE)

from core.eds_parse import parse_eds, find_entry  # noqa: E402


SAMPLE_EDS = """
[FileInfo]
FileName=sample.eds
EDSVersion=4.0

[DeviceInfo]
VendorName=Test

[1000]
ParameterName=Device type
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00000000

[1018]
ParameterName=Identity object
ObjectType=0x9
SubNumber=4

[1018sub1]
ParameterName=Vendor-ID
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00000000

[1018sub2]
ParameterName=Product code
ObjectType=0x7
DataType=0x0007
AccessType=ro
"""


def check(cond, msg):
    if not cond:
        raise AssertionError(msg)
    print("  PASS", msg)


def test_parse_inline_eds():
    print("[eds_parse] inline sample")
    entries = parse_eds(SAMPLE_EDS)
    check(len(entries) >= 3, "parsed multiple OD sections")
    e1018 = find_entry(entries, 0x1018, 0)
    check(e1018 is not None, "0x1018 present")
    check(e1018.name == "Identity object", "0x1018 name")
    vendor = find_entry(entries, 0x1018, 1)
    check(vendor is not None, "0x1018:01 present")
    check(vendor.access_type == "ro", "vendor access ro")
    e1000 = find_entry(entries, 0x1000, 0)
    check(e1000 is not None and e1000.default_value == "0x00000000", "0x1000 default")


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


def test_ast_and_no_cjk():
    print("[canopen-suite] AST-parse all .py; zero CJK")
    py_files = []
    for dirpath, _dirs, files in os.walk(SUITE):
        for name in files:
            if name.endswith(".py"):
                py_files.append(os.path.join(dirpath, name))
    check(len(py_files) >= 10, "found suite python files (%d)" % len(py_files))
    for path in py_files:
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        try:
            ast.parse(src, filename=path)
        except SyntaxError as e:
            raise AssertionError("AST fail %s: %s" % (path, e)) from e
        check(not _has_cjk(src), "no CJK in %s" % os.path.relpath(path, SUITE))


if __name__ == "__main__":
    test_parse_inline_eds()
    test_ast_and_no_cjk()
    print("All tests passed.")
