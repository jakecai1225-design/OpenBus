# -*- coding: utf-8 -*-
"""AST smoke — Drive page + LSS helpers import without QApplication crash path."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)


def test_drive_page_ast():
    path = os.path.join(_SUITE, "pages", "drive.py")
    with open(path, "r", encoding="utf-8") as f:
        tree = ast.parse(f.read(), filename=path)
    names = {n.name for n in tree.body if isinstance(n, ast.FunctionDef)}
    assert "build" in names
    src = open(path, encoding="utf-8").read()
    assert "0x6040" in src and "0x6041" in src
    assert "statusword_state" in src


def test_lss_master_ast():
    path = os.path.join(_SUITE, "core", "lss_master.py")
    with open(path, "r", encoding="utf-8") as f:
        tree = ast.parse(f.read(), filename=path)
    names = {n.name for n in tree.body if isinstance(n, ast.FunctionDef)}
    assert "encode_configure_node_id" in names
    assert "encode_switch_global" in names


def test_network_has_lss_view():
    path = os.path.join(_SUITE, "pages", "network.py")
    src = open(path, encoding="utf-8").read()
    assert "network_lss" in src
    assert "encode_configure_node_id" in src or "lss_master" in src


def test_session_has_health():
    path = os.path.join(_SUITE, "session.py")
    src = open(path, encoding="utf-8").read()
    assert "NetworkHealth" in src
    assert "health_summary" in src
    assert "sdo_download_bytes" in src


if __name__ == "__main__":
    test_drive_page_ast()
    test_lss_master_ast()
    test_network_has_lss_view()
    test_session_has_health()
    print("PASS top3_pages_ast")
