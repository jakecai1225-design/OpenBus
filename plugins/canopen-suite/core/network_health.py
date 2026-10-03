# -*- coding: utf-8 -*-
"""Network health — Heartbeat age, NMT state, EMCY ring (DeviceExplorer slice)."""

from __future__ import annotations

import time
from collections import deque
from dataclasses import dataclass, field
from typing import Deque, Dict, List, Optional, Tuple

from core.cia301_codes import emcy_error_text
from core.cob_classify import classify_cob

_HB_STATE = {
    0x00: "Boot-up",
    0x04: "Stopped",
    0x05: "Operational",
    0x7F: "Pre-operational",
}


@dataclass
class NodeHealth:
    node_id: int
    nmt_state: int = -1
    nmt_label: str = "—"
    last_hb_ts: float = 0.0
    hb_timeout_s: float = 2.0
    missed: bool = False

    def touch_hb(self, state_byte: int, now: Optional[float] = None) -> None:
        self.nmt_state = int(state_byte) & 0x7F
        self.nmt_label = _HB_STATE.get(self.nmt_state, "0x%02X" % self.nmt_state)
        self.last_hb_ts = now if now is not None else time.time()
        self.missed = False

    def age_s(self, now: Optional[float] = None) -> float:
        if self.last_hb_ts <= 0:
            return 1e9
        return (now if now is not None else time.time()) - self.last_hb_ts

    def refresh_missed(self, now: Optional[float] = None) -> bool:
        if self.last_hb_ts <= 0:
            return False
        self.missed = self.age_s(now) > self.hb_timeout_s
        return self.missed


@dataclass
class EmcyEvent:
    ts: float
    node_id: int
    error_code: int
    error_reg: int
    message: str
    raw: bytes = b""


@dataclass
class NetworkHealth:
    """Per-node HB/NMT + rolling EMCY list."""

    hb_timeout_s: float = 2.0
    emcy_limit: int = 64
    nodes: Dict[int, NodeHealth] = field(default_factory=dict)
    emcy: Deque[EmcyEvent] = field(default_factory=lambda: deque(maxlen=64))

    def __post_init__(self) -> None:
        self.emcy = deque(maxlen=max(8, int(self.emcy_limit)))

    def ensure_node(self, node_id: int) -> NodeHealth:
        nid = max(1, min(127, int(node_id)))
        n = self.nodes.get(nid)
        if n is None:
            n = NodeHealth(node_id=nid, hb_timeout_s=self.hb_timeout_s)
            self.nodes[nid] = n
        else:
            n.hb_timeout_s = self.hb_timeout_s
        return n

    def on_frame(self, can_id: int, data: bytes, now: Optional[float] = None) -> Optional[str]:
        """Ingest a frame. Returns a short event note or None."""
        kind, node, _lab = classify_cob(can_id)
        ts = now if now is not None else time.time()
        raw = bytes(data or b"")

        if kind == "HB" and node:
            if not raw:
                return None
            nh = self.ensure_node(node)
            nh.touch_hb(raw[0], ts)
            return "HB node %d → %s" % (node, nh.nmt_label)

        if kind == "EMCY" and node:
            code = 0
            ereg = 0
            if len(raw) >= 2:
                code = raw[0] | (raw[1] << 8)
            if len(raw) >= 3:
                ereg = raw[2]
            msg = emcy_error_text(code)
            ev = EmcyEvent(
                ts=ts, node_id=node, error_code=code, error_reg=ereg,
                message=msg, raw=raw)
            self.emcy.appendleft(ev)
            self.ensure_node(node)
            return "EMCY node %d: %s" % (node, msg)

        return None

    def poll_timeouts(self, now: Optional[float] = None) -> List[int]:
        """Mark missed heartbeats; return node ids that newly timed out."""
        newly = []
        for nid, nh in self.nodes.items():
            was = nh.missed
            if nh.refresh_missed(now) and not was:
                newly.append(nid)
        return newly

    def snapshot(self) -> List[Tuple[int, str, float, bool]]:
        """[(node, nmt_label, age_s, missed), ...] sorted by node."""
        now = time.time()
        rows = []
        for nid in sorted(self.nodes):
            nh = self.nodes[nid]
            nh.refresh_missed(now)
            rows.append((nid, nh.nmt_label, nh.age_s(now), nh.missed))
        return rows

    def recent_emcy(self, limit: int = 20) -> List[EmcyEvent]:
        return list(self.emcy)[:max(1, int(limit))]
