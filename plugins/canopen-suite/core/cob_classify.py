# -*- coding: utf-8 -*-
"""Classify standard CiA 301 pre-defined connection set CAN IDs."""

from __future__ import annotations

from typing import Optional, Tuple


# (kind, node_id or None, label)
def classify_cob(can_id: int) -> Tuple[str, Optional[int], str]:
    """Return (kind, node_id|None, short label) for a CAN identifier.

    kind is one of: NMT, SYNC, TIME, EMCY, TPDO1..4, RPDO1..4,
    TSDO, RSDO, HB, UNKNOWN.
    """
    cid = int(can_id) & 0x7FF

    if cid == 0x000:
        return ("NMT", None, "NMT")
    if cid == 0x080:
        return ("SYNC", None, "SYNC")
    if cid == 0x100:
        return ("TIME", None, "TIME")

    if 0x081 <= cid <= 0x0FF:
        node = cid - 0x080
        return ("EMCY", node, "EMCY node %d" % node)

    # PDO / SDO / heartbeat ranges (pre-defined connection set)
    ranges = (
        (0x180, 0x1FF, "TPDO1", "TPDO1"),
        (0x200, 0x27F, "RPDO1", "RPDO1"),
        (0x280, 0x2FF, "TPDO2", "TPDO2"),
        (0x300, 0x37F, "RPDO2", "RPDO2"),
        (0x380, 0x3FF, "TPDO3", "TPDO3"),
        (0x400, 0x47F, "RPDO3", "RPDO3"),
        (0x480, 0x4FF, "TPDO4", "TPDO4"),
        (0x500, 0x57F, "RPDO4", "RPDO4"),
        (0x580, 0x5FF, "TSDO", "TSDO"),
        (0x600, 0x67F, "RSDO", "RSDO"),
        (0x700, 0x77F, "HB", "Heartbeat"),
    )
    for lo, hi, kind, prefix in ranges:
        if lo <= cid <= hi:
            node = cid - lo
            if kind == "HB":
                node = cid - 0x700
            return (kind, node, "%s node %d" % (prefix, node))

    return ("UNKNOWN", None, "0x%03X" % cid)


def cob_label(can_id: int) -> str:
    kind, node, label = classify_cob(can_id)
    return label if kind != "UNKNOWN" else ("UNK 0x%03X" % (can_id & 0x7FF))
