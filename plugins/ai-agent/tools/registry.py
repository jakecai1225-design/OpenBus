# -*- coding: utf-8 -*-
"""ToolRegistry — OpenAI function-calling schemas + invoke with policy/HITL."""

from __future__ import annotations

import json
import os
from typing import Any, Callable, Dict, List, Optional

from tools.bus_tools import (
    parse_can_id,
    parse_data_bytes,
    summarize_frames,
    write_json_artifact,
    write_text_artifact,
)
from tools.policy import TOOL_MIN_LEVEL, Policy

ApprovalFn = Optional[Callable[[dict], bool]]


def _ok(data: Any = None, **extra) -> dict:
    out = {"ok": True, "data": data if data is not None else {}}
    out.update(extra)
    return out


def _err(msg: str, **extra) -> dict:
    out = {"ok": False, "error": msg}
    out.update(extra)
    return out


def _normalize_name(name: str) -> str:
    """frames.stats -> frames_stats; cap.x.y stays as-is for caps."""
    if name.startswith("cap."):
        return name
    return name.replace(".", "_")


class ToolRegistry:
    def __init__(self, host, policy: Policy, approval_fn: ApprovalFn = None):
        self.host = host
        self.policy = policy
        self.approval_fn = approval_fn

    def schema(self) -> List[dict]:
        tools = []
        for name, spec in self._builtin_specs().items():
            if not self.policy.allows(name):
                continue
            tools.append({
                "type": "function",
                "function": {
                    "name": name,
                    "description": spec["description"],
                    "parameters": spec["parameters"],
                },
            })
        # Capability bus tools
        try:
            from _shared import capability_bus
            allow = self.policy.allowed_cap_permissions()
            for cap in capability_bus.list():
                perm = cap.get("permission") or "read"
                if perm not in allow:
                    continue
                cid = cap["id"]
                fname = "cap." + cid
                tools.append({
                    "type": "function",
                    "function": {
                        "name": fname,
                        "description": (cap.get("description") or cap.get("title") or cid),
                        "parameters": cap.get("parameters") or {
                            "type": "object", "properties": {},
                        },
                    },
                })
        except Exception:
            pass
        return tools

    def list_tools(self) -> List[dict]:
        """Flat list for Node/Mastra sync (GET /api/tools)."""
        out = []
        for item in self.schema():
            fn = item.get("function") or {}
            out.append({
                "name": fn.get("name"),
                "description": fn.get("description"),
                "parameters": fn.get("parameters"),
            })
        return out

    def invoke(self, name: str, arguments: Optional[dict] = None) -> dict:
        args = dict(arguments or {})
        name = _normalize_name(name or "")

        if name.startswith("cap."):
            return self._invoke_cap(name[4:], args)

        if name not in TOOL_MIN_LEVEL and name not in self._builtin_specs():
            return _err("unknown tool: %s" % name)

        if not self.policy.allows(name):
            return _err("denied by policy (%s)" % self.policy.level)

        if self.policy.requires_approval(name):
            info = {"tool": name, "arguments": args, "policy": self.policy.level}
            if self.approval_fn is None:
                return _err("denied by user (no approval gate)")
            if not self.approval_fn(info):
                return _err("denied by user")

        if name in ("frames_send", "uds_read_did", "obd_read_pid"):
            ok, msg = self.policy.check_tx_rate()
            if not ok:
                return _err(msg)

        try:
            return self._dispatch(name, args)
        except Exception as e:
            return _err("%s: %s" % (type(e).__name__, e))

    def _invoke_cap(self, cap_id: str, args: dict) -> dict:
        try:
            from _shared import capability_bus
            return capability_bus.invoke(
                cap_id, args,
                allow_permissions=self.policy.allowed_cap_permissions(),
            )
        except Exception as e:
            return _err("%s: %s" % (type(e).__name__, e))

    def _dispatch(self, name: str, args: dict) -> dict:
        if name == "frames_stats":
            count = int(args.get("count") or 100)
            top_n = int(args.get("top_n") or 10)
            frames = self.host.get_recent(count)
            return _ok(summarize_frames(frames, self.host, top_n=top_n))

        if name == "frames_get_recent":
            count = int(args.get("count") or 50)
            frames = self.host.get_recent(count)
            return _ok({"frames": self._serialize_frames(frames)})

        if name == "frames_get_selected":
            frames = self.host.get_selected()
            return _ok({"frames": self._serialize_frames(frames)})

        if name == "dbc_decode_frame":
            cid = parse_can_id(args.get("can_id") or args.get("id") or 0)
            data = parse_data_bytes(args.get("data") or "")
            signals = self.host.decode(cid, data)
            return _ok({"can_id": "0x%X" % cid, "signals": signals})

        if name == "workspace_get_paths":
            if hasattr(self.host, "workspace_paths"):
                return _ok(self.host.workspace_paths())
            return _ok({
                "project_dir": self.host.project_dir(),
                "dbc_files": self.host.dbc_files(),
            })

        if name == "bus_get_status":
            status = self.host.bus_status() if hasattr(self.host, "bus_status") else {}
            return _ok(status)

        if name == "tx_build_cyclic":
            cid = parse_can_id(args.get("can_id") or 0)
            data = parse_data_bytes(args.get("data") or "")
            period = int(args.get("period_ms") or 100)
            art_name = args.get("name") or "cyclic_%X" % cid
            payload = {
                "kind": "tx_cyclic",
                "name": art_name,
                "can_id": cid,
                "data_hex": data.hex(),
                "period_ms": period,
                "extended": bool(args.get("extended")),
                "fd": bool(args.get("fd")),
            }
            path = write_json_artifact(self.host.project_dir(), art_name, payload)
            return _ok({"path": path, "artifact": payload})

        if name == "uds_build_sequence":
            did = args.get("did") or "0xF190"
            art_name = args.get("name") or "uds_seq"
            lines = ["step,service,did,note", "1,ReadDataByIdentifier,%s,generated" % did]
            path = write_text_artifact(
                self.host.project_dir(), art_name, "\n".join(lines) + "\n", ext=".csv")
            return _ok({"path": path, "did": did})

        if name == "frames_send":
            cid = parse_can_id(args.get("can_id") or args.get("id") or 0)
            data = parse_data_bytes(args.get("data") or "")
            self.host.send_frame(
                cid, data,
                extended=bool(args.get("extended")),
                fd=bool(args.get("fd")),
            )
            return _ok({
                "sent": True,
                "can_id": "0x%X" % cid,
                "data_hex": data.hex(),
            })

        if name == "uds_read_did":
            # Minimal ISO-TP SF: 03 22 DID_H DID_L (no full ISO-TP stack here)
            did = parse_can_id(args.get("did") or 0)
            req_id = parse_can_id(args.get("request_id") or args.get("can_id") or 0x7E0)
            payload = bytes([0x03, 0x22, (did >> 8) & 0xFF, did & 0xFF])
            self.host.send_frame(req_id, payload)
            return _ok({
                "sent": True,
                "request_id": "0x%X" % req_id,
                "did": "0x%X" % did,
                "note": "UDS request sent; parse response from Trace",
            })

        if name == "obd_read_pid":
            pid = parse_can_id(args.get("pid") or 0)
            req_id = parse_can_id(args.get("request_id") or 0x7DF)
            payload = bytes([0x02, 0x01, pid & 0xFF, 0, 0, 0, 0, 0])
            self.host.send_frame(req_id, payload[:8])
            return _ok({
                "sent": True,
                "request_id": "0x%X" % req_id,
                "pid": "0x%02X" % (pid & 0xFF),
                "note": "OBD request sent; parse response from Trace",
            })

        return _err("unhandled tool: %s" % name)

    def _serialize_frames(self, frames) -> List[dict]:
        out = []
        for fr in list(frames or [])[:200]:
            if isinstance(fr, dict):
                out.append(fr)
                continue
            data = getattr(fr, "data", b"") or b""
            if not isinstance(data, (bytes, bytearray)):
                try:
                    data = bytes.fromhex(str(data).replace(" ", ""))
                except Exception:
                    data = b""
            out.append({
                "id": int(getattr(fr, "id", 0) or 0),
                "data": bytes(data).hex(),
                "timestamp": getattr(fr, "timestamp", 0),
                "channel": getattr(fr, "channel", 1),
                "direction": getattr(fr, "direction", "Rx"),
                "extended": bool(getattr(fr, "extended", False)),
                "fd": bool(getattr(fr, "fd", False)),
            })
        return out

    def _builtin_specs(self) -> Dict[str, dict]:
        obj = {"type": "object", "properties": {}, "additionalProperties": True}
        return {
            "frames_stats": {
                "description": "Rank recent CAN IDs by traffic; include DBC names when available",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "count": {"type": "integer", "description": "Recent frame window"},
                        "top_n": {"type": "integer", "description": "Top N IDs"},
                    },
                },
            },
            "frames_get_recent": {
                "description": "Return recent Trace frames",
                "parameters": {
                    "type": "object",
                    "properties": {"count": {"type": "integer"}},
                },
            },
            "frames_get_selected": {
                "description": "Return frames selected in Trace",
                "parameters": obj,
            },
            "dbc_decode_frame": {
                "description": "Decode one frame with loaded DBC",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "can_id": {"type": "string"},
                        "data": {"type": "string", "description": "Hex bytes"},
                    },
                    "required": ["can_id", "data"],
                },
            },
            "workspace_get_paths": {
                "description": "Project directory and DBC file paths",
                "parameters": obj,
            },
            "bus_get_status": {
                "description": "Bus / device connection status (partial)",
                "parameters": obj,
            },
            "tx_build_cyclic": {
                "description": "Write a cyclic TX artifact JSON (does not send)",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "can_id": {"type": "string"},
                        "data": {"type": "string"},
                        "period_ms": {"type": "integer"},
                        "name": {"type": "string"},
                    },
                    "required": ["can_id", "data"],
                },
            },
            "uds_build_sequence": {
                "description": "Write a UDS sequence CSV artifact (does not send)",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "did": {"type": "string"},
                        "name": {"type": "string"},
                    },
                },
            },
            "frames_send": {
                "description": "Send one CAN frame (requires approval)",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "can_id": {"type": "string"},
                        "data": {"type": "string"},
                        "extended": {"type": "boolean"},
                        "fd": {"type": "boolean"},
                    },
                    "required": ["can_id", "data"],
                },
            },
            "uds_read_did": {
                "description": "Send UDS ReadDataByIdentifier request (requires approval)",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "did": {"type": "string"},
                        "request_id": {"type": "string"},
                        "can_id": {"type": "string"},
                    },
                    "required": ["did"],
                },
            },
            "obd_read_pid": {
                "description": "Send OBD Mode 01 PID request (requires approval)",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "pid": {"type": "string"},
                        "request_id": {"type": "string"},
                    },
                    "required": ["pid"],
                },
            },
        }
