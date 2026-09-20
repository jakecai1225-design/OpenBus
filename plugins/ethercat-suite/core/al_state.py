# -*- coding: utf-8 -*-
"""EtherCAT AL status and allowed transitions (application layer)."""

from __future__ import annotations


STATES = {
    0x1: "INIT",
    0x2: "PREOP",
    0x3: "BOOT",
    0x4: "SAFEOP",
    0x8: "OP",
}

NAME_TO_CODE = {v: k for k, v in STATES.items()}

ALLOWED = {
    "INIT": {"PREOP"},
    "PREOP": {"INIT", "SAFEOP", "BOOT"},
    "BOOT": {"INIT"},
    "SAFEOP": {"INIT", "PREOP", "OP"},
    "OP": {"INIT", "PREOP", "SAFEOP"},
}


def decode_status(word: int) -> dict:
    code = int(word) & 0x0F
    return {
        "code": code,
        "name": STATES.get(code, "UNKNOWN"),
        "error": bool(int(word) & 0x10),
        "raw": int(word) & 0xFFFF,
    }


def can_transition(current: str, target: str) -> bool:
    return target in ALLOWED.get(current, set())


def request_state(current: str, target: str) -> dict:
    if target not in NAME_TO_CODE:
        return {"ok": False, "reason": "unknown state", "state": current}
    if target == current:
        return {"ok": True, "state": current, "reason": "unchanged"}
    if not can_transition(current, target):
        return {"ok": False, "reason": "not allowed", "state": current}
    return {"ok": True, "state": target, "reason": "ok"}
