# -*- coding: utf-8 -*-
"""DBC merge engine (ported from dbc-merge)."""

from __future__ import annotations

import copy

POLICY_SKIP = 0
POLICY_RENAME = 1
POLICY_PREFER_A = 2


def _clone_message(m):
    return copy.deepcopy(m)


def merge_dbc(base, incoming, source_name: str, policy: int, conflicts: list) -> None:
    """Merge incoming into base; append conflict tuples."""
    for cid, m in incoming.messages.items():
        msg = _clone_message(m)
        if cid not in base.messages:
            base.messages[cid] = msg
            continue
        existing = base.messages[cid]
        if policy == POLICY_SKIP or policy == POLICY_PREFER_A:
            action = "skip" if policy == POLICY_SKIP else "prefer-A"
            conflicts.append((cid, existing.name, msg.name, source_name, action))
            continue
        new_name = "%s_%X" % (msg.name[:40], cid)
        conflicts.append((cid, new_name, existing.name, source_name, "rename"))
        msg.name = new_name
        base.messages[cid] = msg

    for n in incoming.nodes:
        if n not in base.nodes:
            base.nodes.append(n)
