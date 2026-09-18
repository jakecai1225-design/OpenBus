# -*- coding: utf-8 -*-
"""Readonly policy hides write tools."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN = os.path.dirname(_HERE)
if _PLUGIN not in sys.path:
    sys.path.insert(0, _PLUGIN)

from tools.policy import Policy
from tools.registry import ToolRegistry
from test_orchestrator_mock import FakeHost


def _names(schema):
    return [item["function"]["name"] for item in schema]


def test_readonly_hides_send():
    reg = ToolRegistry(FakeHost(), Policy("readonly"))
    names = _names(reg.schema())
    assert "frames_stats" in names
    assert "frames_get_recent" in names
    assert "dbc_decode_frame" in names
    assert "workspace_get_paths" in names
    assert "frames_send" not in names
    denied = reg.invoke("frames_send", {"can_id": "0x123", "data": "00"})
    assert denied["ok"] is False
    assert "denied" in denied["error"]
    dotted = reg.invoke("frames.stats", {"count": 20, "top_n": 3})
    assert dotted["ok"] is True
    assert dotted["data"]["top"][0]["id"] == "0x123"
    print("PASS readonly hides frames_send")


def test_no_cjk_in_plugin():
    for dirpath, _dirs, files in os.walk(_PLUGIN):
        if "__pycache__" in dirpath or os.path.basename(dirpath) == "_ai_home":
            continue
        for name in files:
            if not name.endswith((".py", ".json")):
                continue
            path = os.path.join(dirpath, name)
            with open(path, "r", encoding="utf-8") as f:
                src = f.read()
            if name.endswith(".py"):
                ast.parse(src, filename=path)
            for ch in src:
                o = ord(ch)
                assert not (0x4E00 <= o <= 0x9FFF), "CJK in %s" % path
    print("PASS AST + zero CJK")


if __name__ == "__main__":
    test_readonly_hides_send()
    test_no_cjk_in_plugin()
    print("All policy tests passed")
