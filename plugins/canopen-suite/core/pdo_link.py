# -*- coding: utf-8 -*-
"""PDO link matrix MVP — connect producer OD object → consumer RPDO map slot.

Pure functions for multi-node project planning (port CCM / PDL lite).
Does not talk to the bus; callers write mapping dwords into draft EDS entries.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Sequence


@dataclass(frozen=True)
class PdoLink:
    """One signal link from producer to consumer."""

    producer_node: int
    producer_index: int
    producer_sub: int
    consumer_node: int
    rpdo_number: int  # 1..4
    map_slot: int     # 1..8 (subindex of 0x1600+n)
    bit_length: int

    def mapping_dword(self) -> int:
        """CiA 301 PDO mapping entry format."""
        return (
            ((self.producer_index & 0xFFFF) << 16)
            | ((self.producer_sub & 0xFF) << 8)
            | (self.bit_length & 0xFF)
        )


def make_link(
    producer_node: int,
    producer_index: int,
    producer_sub: int,
    consumer_node: int,
    rpdo_number: int,
    map_slot: int,
    bit_length: int,
) -> PdoLink:
    if not (1 <= producer_node <= 127 and 1 <= consumer_node <= 127):
        raise ValueError("node id out of range")
    if not (1 <= rpdo_number <= 4):
        raise ValueError("rpdo_number must be 1..4")
    if not (1 <= map_slot <= 8):
        raise ValueError("map_slot must be 1..8")
    if bit_length <= 0 or bit_length > 64:
        raise ValueError("bit_length invalid")
    return PdoLink(
        producer_node=producer_node,
        producer_index=producer_index,
        producer_sub=producer_sub,
        consumer_node=consumer_node,
        rpdo_number=rpdo_number,
        map_slot=map_slot,
        bit_length=bit_length,
    )


def rpdo_map_index(rpdo_number: int) -> int:
    return 0x1600 + (int(rpdo_number) - 1)


def validate_links(links: Sequence[PdoLink]) -> List[str]:
    """Return human-readable issues (empty = OK)."""
    issues: List[str] = []
    seen = set()
    bits_per: dict = {}
    for lk in links:
        key = (lk.consumer_node, lk.rpdo_number, lk.map_slot)
        if key in seen:
            issues.append(
                "Duplicate map slot node %d RPDO%d slot %d"
                % (lk.consumer_node, lk.rpdo_number, lk.map_slot))
        seen.add(key)
        bk = (lk.consumer_node, lk.rpdo_number)
        bits_per[bk] = bits_per.get(bk, 0) + lk.bit_length
    for (node, rpdo), bits in bits_per.items():
        if bits > 64:
            issues.append(
                "Node %d RPDO%d mapped bits %d > 64" % (node, rpdo, bits))
    return issues


def apply_links_to_entries(entries: list, links: Sequence[PdoLink]) -> int:
    """Write mapping dwords into OdEntry-like objects (draft). Returns updates.

    Expects entries with .index / .subindex / .default_value attributes.
    Creates missing map slots only if a matching index:0 count entry exists.
    """
    issues = validate_links(links)
    if issues:
        raise ValueError("; ".join(issues))
    by_key = {(e.index, e.subindex): e for e in entries}
    updated = 0
    # Group by consumer RPDO
    by_pdo: dict = {}
    for lk in links:
        by_pdo.setdefault(
            (lk.consumer_node, lk.rpdo_number), []).append(lk)
    for (_node, rpdo), group in by_pdo.items():
        map_idx = rpdo_map_index(rpdo)
        for lk in group:
            key = (map_idx, lk.map_slot)
            e = by_key.get(key)
            if e is None:
                continue
            dword = "0x%08X" % lk.mapping_dword()
            if hasattr(e, "parameter_value"):
                e.parameter_value = dword
            e.default_value = dword
            updated += 1
        count_key = (map_idx, 0)
        ce = by_key.get(count_key)
        if ce is not None:
            n = max(lk.map_slot for lk in group)
            ce.default_value = str(n)
            if hasattr(ce, "parameter_value"):
                ce.parameter_value = str(n)
            updated += 1
    return updated


def links_from_dicts(rows: Sequence[dict]) -> List[PdoLink]:
    out = []
    for r in rows or ():
        out.append(make_link(
            int(r["producer_node"]),
            int(r["producer_index"], 0) if isinstance(r["producer_index"], str)
            else int(r["producer_index"]),
            int(r.get("producer_sub", 0)),
            int(r["consumer_node"]),
            int(r["rpdo_number"]),
            int(r["map_slot"]),
            int(r["bit_length"]),
        ))
    return out
