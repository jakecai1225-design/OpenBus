# -*- coding: utf-8 -*-
"""ECU profile and sequence helpers for uds-diagnostic."""

from __future__ import annotations

import csv
import json
import os
from typing import Any, Optional


def default_profile() -> dict:
    return {
        "name": "Default ECU",
        "tx_id": 0x7E0,
        "func_id": 0x7DF,
        "rx_id": 0x7E8,
        "default_session": 0x01,
        "did_catalog": "",
        "notes": "",
    }


def load_profile_file(path: str) -> Optional[dict]:
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            return None
        base = default_profile()
        base.update(data)
        for k in ("tx_id", "func_id", "rx_id", "default_session"):
            if isinstance(base.get(k), str):
                base[k] = int(base[k], 0)
        return base
    except (OSError, json.JSONDecodeError, ValueError):
        return None


def save_profile_file(path: str, profile: dict) -> None:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(profile, f, indent=2, ensure_ascii=False)


def load_sequence(path: str) -> list[dict]:
    """Load sequence steps from JSON or CSV.

    JSON: [{"tag": "...", "pdu_hex": "10 03", "expect_response": true}, ...]
    CSV columns: tag,pdu_hex,expect_response
    """
    ext = os.path.splitext(path)[1].lower()
    if ext == ".csv":
        steps = []
        with open(path, "r", encoding="utf-8-sig", newline="") as f:
            for row in csv.DictReader(f):
                if not row.get("pdu_hex"):
                    continue
                steps.append({
                    "tag": row.get("tag") or "step",
                    "pdu_hex": row["pdu_hex"].strip(),
                    "expect_response": str(row.get("expect_response", "1")).lower()
                    not in ("0", "false", "no"),
                })
        return steps
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if isinstance(data, dict):
        data = data.get("steps") or data.get("sequence") or []
    return list(data)


def parse_pdu_hex(text: str) -> bytes:
    text = text.replace(" ", "").replace("0x", "").replace("0X", "")
    if len(text) % 2:
        text = "0" + text
    return bytes.fromhex(text)
