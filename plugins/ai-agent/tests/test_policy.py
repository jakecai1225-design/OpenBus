# -*- coding: utf-8 -*-
"""Policy levels, approval, and write-path smoke tests."""

from __future__ import annotations

import ast
import json
import os
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN = os.path.dirname(_HERE)
if _PLUGIN not in sys.path:
    sys.path.insert(0, _PLUGIN)

os.environ["OPENBUS_AI_HOME"] = os.path.join(_HERE, "_ai_home")

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
    assert "tx_build_cyclic" in names
    assert "frames_send" not in names
    assert "uds_read_did" not in names
    denied = reg.invoke("frames_send", {"can_id": "0x123", "data": "00"})
    assert denied["ok"] is False
    assert "denied" in denied["error"]
    dotted = reg.invoke("frames.stats", {"count": 20, "top_n": 3})
    assert dotted["ok"] is True
    assert dotted["data"]["top"][0]["id"] == "0x123"
    print("PASS readonly hides write tools")


def test_tx_allowed_requires_approval_then_sends():
    host = FakeHost()
    decisions = []

    def approve(info):
        decisions.append(info["tool"])
        return True

    reg = ToolRegistry(host, Policy("tx_allowed"), approval_fn=approve)
    names = _names(reg.schema())
    assert "frames_send" in names
    assert "uds_read_did" in names
    assert "obd_read_pid" in names
    result = reg.invoke("frames_send", {"can_id": "0x123", "data": "010203"})
    assert result["ok"] is True
    assert result["data"]["sent"] is True
    assert decisions == ["frames_send"]
    assert len(host.sent) == 1
    assert host.sent[0]["id"] == 0x123
    assert host.sent[0]["data"] == bytes.fromhex("010203")

    denied = ToolRegistry(host, Policy("tx_allowed"), approval_fn=lambda _i: False)
    no = denied.invoke("frames_send", {"can_id": "0x1", "data": "00"})
    assert no["ok"] is False
    assert "denied by user" in no["error"]
    print("PASS tx_allowed + approval send")


def test_artifacts_without_tx():
    host = FakeHost()
    with tempfile.TemporaryDirectory() as td:
        host.project_dir = lambda: td  # type: ignore
        reg = ToolRegistry(host, Policy("readonly"))
        tx = reg.invoke("tx_build_cyclic", {
            "can_id": "0x123", "data": "00", "period_ms": 100, "name": "demo",
        })
        assert tx["ok"] is True
        path = tx["data"]["path"]
        assert os.path.isfile(path)
        with open(path, encoding="utf-8") as f:
            art = json.load(f)
        assert art["period_ms"] == 100
        assert art["can_id"] == 0x123
        seq = reg.invoke("uds_build_sequence", {"did": "0xF190"})
        assert seq["ok"] is True
        assert os.path.isfile(seq["data"]["path"])
        assert host.sent == []
    print("PASS artifact builders (no bus TX)")


def test_rate_limit():
    p = Policy("tx_allowed", max_tx_per_sec=3)
    for _ in range(3):
        ok, _msg = p.check_tx_rate()
        assert ok
    ok, msg = p.check_tx_rate()
    assert not ok
    assert "rate limit" in msg
    print("PASS TX rate limit")


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
    test_tx_allowed_requires_approval_then_sends()
    test_artifacts_without_tx()
    test_rate_limit()
    test_no_cjk_in_plugin()
    print("All policy tests passed")
