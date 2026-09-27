# -*- coding: utf-8 -*-
"""Permission levels and TX rate limiting for agent tools."""

from __future__ import annotations

import time
from typing import List, Tuple

LEVELS = ("readonly", "tx_allowed", "diag_write", "flash")

# Tools that require HITL approval at their minimum level
APPROVAL_TOOLS = frozenset({
    "frames_send",
    "uds_read_did",
    "obd_read_pid",
})

# Minimum policy level to expose a tool in the schema
TOOL_MIN_LEVEL = {
    "frames_stats": "readonly",
    "frames_get_recent": "readonly",
    "frames_get_selected": "readonly",
    "dbc_decode_frame": "readonly",
    "workspace_get_paths": "readonly",
    "bus_get_status": "readonly",
    "tx_build_cyclic": "readonly",
    "uds_build_sequence": "readonly",
    "frames_send": "tx_allowed",
    "uds_read_did": "tx_allowed",
    "obd_read_pid": "tx_allowed",
}


class Policy:
    def __init__(self, level: str = "readonly", max_tx_per_sec: int = 10):
        self._level = "readonly"
        self.max_tx_per_sec = int(max_tx_per_sec)
        self._tx_times: List[float] = []
        self.set_level(level)

    @property
    def level(self) -> str:
        return self._level

    def set_level(self, level: str) -> None:
        # Map settings UI aliases
        aliases = {
            "guided": "tx_allowed",
            "full": "diag_write",
            "read-only": "readonly",
            "allow-tx": "tx_allowed",
            "diag-write": "diag_write",
        }
        level = aliases.get((level or "").strip().lower(), (level or "").strip().lower())
        if level not in LEVELS:
            raise ValueError("unknown policy level: %s" % level)
        self._level = level

    def _rank(self, level: str) -> int:
        return LEVELS.index(level)

    def allows(self, tool_name: str) -> bool:
        need = TOOL_MIN_LEVEL.get(tool_name)
        if need is None:
            # Capability tools: read always; write/diag gated by permission string elsewhere
            return True
        return self._rank(self._level) >= self._rank(need)

    def requires_approval(self, tool_name: str) -> bool:
        return tool_name in APPROVAL_TOOLS and self.allows(tool_name)

    def allowed_cap_permissions(self) -> set:
        if self._level == "readonly":
            return {"read"}
        if self._level == "tx_allowed":
            return {"read", "write"}
        if self._level == "diag_write":
            return {"read", "write", "diag_write"}
        return {"read", "write", "diag_write", "flash"}

    def check_tx_rate(self) -> Tuple[bool, str]:
        now = time.time()
        self._tx_times = [t for t in self._tx_times if now - t < 1.0]
        if len(self._tx_times) >= self.max_tx_per_sec:
            return False, "rate limit: max %d TX/s" % self.max_tx_per_sec
        self._tx_times.append(now)
        return True, ""
