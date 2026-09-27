# -*- coding: utf-8 -*-
"""Pure helpers for frame stats and artifact builders."""

from __future__ import annotations

import json
import os
from collections import Counter
from typing import Any, List, Optional


def _frame_id(fr) -> int:
    if isinstance(fr, dict):
        return int(fr.get("id") or fr.get("can_id") or 0)
    return int(getattr(fr, "id", 0) or 0)


def _frame_data(fr) -> bytes:
    if isinstance(fr, dict):
        raw = fr.get("data") or b""
        if isinstance(raw, (bytes, bytearray)):
            return bytes(raw)
        return bytes.fromhex(str(raw).replace(" ", ""))
    data = getattr(fr, "data", b"") or b""
    if isinstance(data, (bytes, bytearray)):
        return bytes(data)
    return bytes.fromhex(str(data).replace(" ", ""))


def summarize_frames(frames: List[Any], host=None, top_n: int = 10) -> dict:
    """Rank CAN IDs by count; enrich with DBC message name + sample decode."""
    frames = list(frames or [])
    counts = Counter(_frame_id(f) for f in frames)
    # Keep first sample per id for decode
    samples = {}
    for fr in frames:
        cid = _frame_id(fr)
        if cid not in samples:
            samples[cid] = fr

    msg_names = {}
    if host is not None and hasattr(host, "list_messages"):
        try:
            for m in host.list_messages() or []:
                mid = int(m.get("id") if isinstance(m, dict) else getattr(m, "id", 0))
                name = m.get("name") if isinstance(m, dict) else getattr(m, "name", "")
                msg_names[mid] = name or ""
        except Exception:
            pass

    top = []
    for cid, count in counts.most_common(int(top_n)):
        item = {
            "id": "0x%X" % cid,
            "count": count,
            "message": msg_names.get(cid) or "",
            "signals": {},
        }
        sample = samples.get(cid)
        if sample is not None and host is not None and hasattr(host, "decode"):
            try:
                item["signals"] = host.decode(cid, _frame_data(sample)) or {}
            except Exception:
                item["signals"] = {}
        top.append(item)

    return {
        "total": len(frames),
        "unique_ids": len(counts),
        "top": top,
    }


def write_json_artifact(project_dir: str, name: str, payload: dict) -> str:
    base = project_dir or os.path.join(os.path.expanduser("~"), ".openbus", "ai-agent", "artifacts")
    os.makedirs(base, exist_ok=True)
    safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in (name or "artifact"))
    path = os.path.join(base, safe + ".json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)
    return path


def write_text_artifact(project_dir: str, name: str, text: str, ext: str = ".csv") -> str:
    base = project_dir or os.path.join(os.path.expanduser("~"), ".openbus", "ai-agent", "artifacts")
    os.makedirs(base, exist_ok=True)
    safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in (name or "artifact"))
    path = os.path.join(base, safe + ext)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return path


def parse_can_id(value) -> int:
    if isinstance(value, int):
        return value
    s = str(value).strip().lower()
    if s.startswith("0x"):
        return int(s, 16)
    return int(s, 0) if s.startswith("0") and len(s) > 1 and s[1].isalpha() else int(s)


def parse_data_bytes(value) -> bytes:
    if isinstance(value, (bytes, bytearray)):
        return bytes(value)
    s = str(value).replace(" ", "").replace("-", "")
    if not s:
        return b""
    return bytes.fromhex(s)
