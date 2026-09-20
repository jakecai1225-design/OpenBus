# -*- coding: utf-8 -*-
"""Phase A: attach resolve, capability bus, registry merge, snapshot budget."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_PLUGIN = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_PLUGIN)
if _PLUGIN not in sys.path:
    sys.path.insert(0, _PLUGIN)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)

os.environ["OPENBUS_AI_HOME"] = os.path.join(_HERE, "_ai_home_phase_a")

from _shared import activity_snapshot, ai_attach, capability_bus
from _shared.capability_bus import Capability
from tools.policy import Policy
from tools.registry import ToolRegistry


class _Host:
    def get_recent_frames(self, count=100):
        return []

    def get_selected_frames(self):
        return []

    def get_frame_stats(self, count=500):
        return {"total": 0, "by_id": []}

    def decode_frame(self, *a, **k):
        return {}

    def list_dbc_messages(self):
        return []

    def workspace_paths(self):
        return {}

    def bus_status(self):
        return {}


def test_attach_resolve_budget():
    ai_attach.get_inbox().clear()
    frames = [{"id": i, "data": "00"} for i in range(80)]
    att = ai_attach.frames_attachment(frames, title="Busy Trace")
    text = ai_attach.resolve([att], budget=800)
    assert "Attachment" in text
    assert len(text) <= 900


def test_inbox_chips_remove():
    inbox = ai_attach.get_inbox()
    inbox.clear()
    a = ai_attach.attach({"kind": "note", "title": "N1", "payload": {"x": 1}})
    b = ai_attach.attach({"kind": "note", "title": "N2", "payload": {"x": 2}})
    assert len(inbox.items()) == 2
    assert inbox.remove(a.id)
    titles = [x.title for x in inbox.items()]
    assert titles == ["N2"]
    inbox.clear()
    assert inbox.items() == []
    assert b.id  # still valid object


def test_capability_bus_register_invoke():
    capability_bus.unregister("test-suite")

    def handler(args):
        return {"ok": True, "echo": args.get("v")}

    capability_bus.register("test-suite", [
        Capability(
            id="test.echo",
            provider="test-suite",
            title="Echo",
            description="Echo args",
            parameters={"type": "object", "properties": {"v": {"type": "string"}}},
            permission="read",
            handler=handler,
        ),
    ])
    listed = capability_bus.list_capabilities(provider="test-suite")
    assert any(c["id"] == "test.echo" for c in listed)
    result = capability_bus.invoke("test.echo", {"v": "hi"})
    assert result.get("ok") is True
    assert result.get("echo") == "hi"
    capability_bus.unregister("test-suite")


def test_registry_merges_caps():
    capability_bus.unregister("test-suite")
    capability_bus.register("test-suite", [
        Capability(
            id="test.ping",
            provider="test-suite",
            title="Ping",
            description="Ping",
            parameters={"type": "object", "properties": {}},
            permission="read",
            handler=lambda _a: {"ok": True, "pong": True},
        ),
    ])
    reg = ToolRegistry(_Host(), Policy("readonly"))
    names = [s["function"]["name"] for s in reg.schema()]
    assert "cap.test.ping" in names
    out = reg.invoke("cap.test.ping", {})
    assert out.get("ok") is True
    capability_bus.unregister("test-suite")


def test_snapshot_prompt_budget():
    activity_snapshot.update(
        project_path="/tmp/demo",
        active_plugin="canopen-suite",
        active_page="eds",
        selection_summary="chip: EDS",
        measure_on=False,
    )
    # Force flush without waiting debounce
    activity_snapshot._flush()
    text = activity_snapshot.format_for_prompt(max_chars=120)
    assert "Activity Snapshot" in text
    assert len(text) <= 120
    long_path = "P" * 500
    activity_snapshot.update(project_path=long_path)
    activity_snapshot._flush()
    text2 = activity_snapshot.format_for_prompt(max_chars=100)
    assert len(text2) <= 100


def test_eds_attachment_resolve_summary():
    att = ai_attach.eds_attachment(
        "", summary={"object_count": 12, "node_id": 1}, title="Stub EDS")
    text = ai_attach.resolve([att], budget=2000)
    assert "object_count" in text or "12" in text
