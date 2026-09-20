# -*- coding: utf-8 -*-
"""Mock tool-loop: fixed tool_calls produce an ID ranking with a DBC name."""

from __future__ import annotations

import json
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN = os.path.dirname(_HERE)
if _PLUGIN not in sys.path:
    sys.path.insert(0, _PLUGIN)

os.environ["OPENBUS_AI_HOME"] = os.path.join(_HERE, "_ai_home")

from agent.llm_client import ScriptedLLM
from agent.orchestrator import Orchestrator
from agent.session_store import SessionStore
from tools.bus_tools import summarize_frames
from tools.policy import Policy
from tools.registry import ToolRegistry


class _Frame:
    def __init__(self, can_id, data=b"\x01\x02", ts=1.0):
        self.id = can_id
        self.data = data
        self.timestamp = ts
        self.channel = 1
        self.direction = "Rx"
        self.extended = False
        self.fd = False
        self.dlc = len(data)


class FakeHost:
    def __init__(self):
        self.sent = []
        self.frames = (
            [_Frame(0x123, b"\x11\x22", ts=1.0 + i * 0.01) for i in range(5)]
            + [_Frame(0x456, b"\x00", ts=2.0)]
        )

    def get_recent(self, count):
        return list(self.frames)[: int(count)]

    def get_selected(self):
        return [self.frames[0]]

    def send_frame(self, can_id, data, extended=False, fd=False):
        raw = data if isinstance(data, (bytes, bytearray)) else bytes.fromhex(str(data))
        self.sent.append({
            "id": int(can_id),
            "data": bytes(raw),
            "extended": bool(extended),
            "fd": bool(fd),
        })

    def decode(self, can_id, data):
        if int(can_id) == 0x123:
            return {"EngineTemp": 90}
        return {}

    def project_dir(self):
        return "/tmp/proj"

    def dbc_files(self):
        return ["/tmp/proj/engine.dbc"]

    def list_messages(self):
        return [
            {"id": 0x123, "name": "EngineData", "dlc": 8},
            {"id": 0x456, "name": "BodyStatus", "dlc": 8},
        ]

    def log(self, text):
        pass


def test_stats_ranks_top_id_with_dbc_name():
    host = FakeHost()
    summary = summarize_frames(host.frames, host, top_n=5)
    assert summary["top"][0]["id"] == "0x123"
    assert summary["top"][0]["count"] == 5
    assert summary["top"][0]["message"] == "EngineData"
    assert summary["top"][0]["signals"]["EngineTemp"] == 90
    assert summary["top"][1]["message"] == "BodyStatus"
    print("PASS stats ranking + DBC name")


def test_scripted_loop_calls_stats_and_answers():
    host = FakeHost()
    registry = ToolRegistry(host, Policy("readonly"))
    store = SessionStore("mockloop")
    script = [
        {"tool_calls": [{
            "id": "c1",
            "name": "frames_stats",
            "arguments": {"count": 50, "top_n": 5},
        }]},
        {"content": "Top talker is 0x123 EngineData (5 frames)."},
    ]
    orch = Orchestrator(ScriptedLLM(script), registry, store)
    answer = orch.run("Who is transmitting the most?")
    assert "0x123" in answer
    assert "EngineData" in answer
    rows = []
    with open(store.path, "r", encoding="utf-8") as f:
        for line in f:
            rows.append(json.loads(line))
    tools = [r for r in rows if r.get("kind") == "tool"]
    assert tools and tools[0]["name"] == "frames_stats"
    assert tools[0]["ok"] is True
    assert "0x123" in tools[0]["summary"]
    assert "EngineData" in tools[0]["summary"]
    print("PASS scripted tool-loop")


if __name__ == "__main__":
    test_stats_ranks_top_id_with_dbc_name()
    test_scripted_loop_calls_stats_and_answers()
    print("All orchestrator mock tests passed")
