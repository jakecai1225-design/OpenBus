# -*- coding: utf-8 -*-
"""Read a Vector-style ASC slice for TX Lab replay."""

from __future__ import annotations

import re

_ASC_RE = re.compile(
    r"^\s*([\d.]+)\s+(\d+)\s+([0-9A-Fa-fxX]+)\s+(Rx|Tx)\s+d\s+(\d+)\s*(.*)$",
    re.IGNORECASE,
)


def read_asc(path: str) -> tuple[list, int]:
    """Return (frames, skipped). Frame = (ts, channel, can_id, data, extended)."""
    frames = []
    skipped = 0
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = _ASC_RE.match(line)
            if not m:
                continue
            try:
                ts = float(m.group(1))
                cid = int(m.group(3).rstrip("xX"), 16)
                dlc = int(m.group(5))
            except ValueError:
                skipped += 1
                continue
            data_hex = (m.group(6) or "").strip()
            try:
                data = bytes.fromhex(data_hex.replace(" ", "")[: max(0, dlc) * 2]) if data_hex else b""
            except ValueError:
                skipped += 1
                continue
            frames.append((ts, int(m.group(2)), cid, data, cid > 0x7FF))
    frames.sort(key=lambda row: row[0])
    return frames, skipped
