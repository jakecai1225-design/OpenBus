# -*- coding: utf-8 -*-
"""Timing lite — cyclic bus-load estimate (CANdb++ Admin style)."""

from __future__ import annotations


def frame_bits(dlc: int, extended: bool = False, stuffing: bool = True) -> int:
    """Approximate on-wire bits for one CAN frame.

    Base (no stuffing): standard ~47 + 8*DLC, extended ~67 + 8*DLC.
    With stuffing=True apply a 20% lite worst-case factor.
    """
    dlc = max(0, min(64, int(dlc or 0)))
    base = (67 if extended else 47) + 8 * dlc
    if stuffing:
        return int(round(base * 1.2))
    return base


def estimate_bus_load(db, baud: int = 500000, stuffing: bool = True) -> dict:
    """Return summary + per-message rows for cyclic traffic.

    Load% = sum(bits_per_second) / baud * 100
    Messages with cycle_time <= 0 are listed as event (not in load).
    """
    baud = max(1000, int(baud or 500000))
    rows = []
    bits_per_s = 0.0
    cyclic = 0
    event = 0
    for cid in sorted(db.messages.keys()):
        m = db.messages[cid]
        bits = frame_bits(m.dlc, bool(m.extended), stuffing)
        cycle = int(m.cycle_time or 0)
        if cycle <= 0:
            # Fall back to GenMsgCycleTime attribute if present
            raw = (m.attributes or {}).get("GenMsgCycleTime")
            if raw not in (None, ""):
                try:
                    cycle = int(float(raw))
                except (TypeError, ValueError):
                    cycle = 0
        if cycle > 0:
            rate = 1000.0 / float(cycle)  # frames / second
            bps = bits * rate
            bits_per_s += bps
            cyclic += 1
            share = (bps / baud) * 100.0
            rows.append({
                "can_id": cid,
                "name": m.name,
                "dlc": m.dlc,
                "extended": bool(m.extended),
                "cycle_ms": cycle,
                "bits": bits,
                "fps": rate,
                "bps": bps,
                "share_pct": share,
                "kind": "cyclic",
            })
        else:
            event += 1
            rows.append({
                "can_id": cid,
                "name": m.name,
                "dlc": m.dlc,
                "extended": bool(m.extended),
                "cycle_ms": 0,
                "bits": bits,
                "fps": 0.0,
                "bps": 0.0,
                "share_pct": 0.0,
                "kind": "event",
            })

    load_pct = (bits_per_s / float(baud)) * 100.0
    return {
        "baud": baud,
        "stuffing": stuffing,
        "load_pct": load_pct,
        "bits_per_s": bits_per_s,
        "cyclic": cyclic,
        "event": event,
        "messages": len(db.messages),
        "rows": rows,
    }


def load_band(load_pct: float) -> str:
    """Traffic light band for UI."""
    if load_pct < 30:
        return "ok"
    if load_pct < 50:
        return "warn"
    return "high"
