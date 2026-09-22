# -*- coding: utf-8 -*-
"""CSV → message/signal import (Phase 2)."""

from __future__ import annotations

import csv
import io

from _shared import dbcparse


# Accepted header aliases (lower-case) → canonical field
_ALIASES = {
    "message": "message",
    "msg": "message",
    "msg_name": "message",
    "bo": "message",
    "id": "id",
    "can_id": "id",
    "canid": "id",
    "msgid": "id",
    "dlc": "dlc",
    "length": "dlc",
    "sender": "sender",
    "tx": "sender",
    "transmitter": "sender",
    "cycle_ms": "cycle_ms",
    "cycle": "cycle_ms",
    "cycletime": "cycle_ms",
    "signal": "signal",
    "sg": "signal",
    "sig": "signal",
    "start_bit": "start_bit",
    "startbit": "start_bit",
    "start": "start_bit",
    "bit_length": "bit_length",
    "bitlength": "bit_length",
    "bits": "bit_length",
    "len": "bit_length",
    "byte_order": "byte_order",
    "byteorder": "byte_order",
    "endian": "byte_order",
    "signed": "signed",
    "factor": "factor",
    "offset": "offset",
    "min": "min",
    "minimum": "min",
    "max": "max",
    "maximum": "max",
    "unit": "unit",
    "receivers": "receivers",
    "rx": "receivers",
    "comment": "comment",
    "value_table": "value_table",
    "values": "value_table",
}


def _canon_headers(raw_headers):
    out = []
    for h in raw_headers:
        key = (h or "").strip().lower().replace(" ", "_")
        out.append(_ALIASES.get(key, key))
    return out


def _parse_id(text) -> int:
    s = str(text or "").strip().lower().replace("0x", "")
    return int(s, 16) & 0x1FFFFFFF


def _parse_bool_signed(text) -> bool:
    s = str(text or "").strip().lower()
    return s in ("1", "true", "yes", "signed", "s")


def _parse_endian(text) -> bool:
    """Return True for Intel (little-endian)."""
    s = str(text or "").strip().lower()
    if s in ("0", "motorola", "big", "msb", "motorola (big)"):
        return False
    return True


def _parse_receivers(text) -> list:
    parts = [p.strip() for p in str(text or "").replace(";", ",").split(",")]
    return [p for p in parts if p] or ["Vector__XXX"]


def _parse_values(text) -> dict:
    out = {}
    for part in str(text or "").replace(",", ";").split(";"):
        part = part.strip()
        if not part or "=" not in part:
            continue
        k, v = part.split("=", 1)
        try:
            out[int(k.strip(), 0)] = v.strip()
        except ValueError:
            continue
    return out


def import_csv_text(db: dbcparse.DbcFile, text: str) -> dict:
    """Import CSV rows into db. Returns stats dict."""
    reader = csv.reader(io.StringIO(text))
    try:
        headers = _canon_headers(next(reader))
    except StopIteration:
        return {"messages": 0, "signals": 0, "errors": ["Empty CSV"]}

    if "signal" not in headers and "message" not in headers:
        return {
            "messages": 0, "signals": 0,
            "errors": ["CSV needs message and/or signal columns"],
        }

    stats = {"messages": 0, "signals": 0, "errors": []}
    created_msgs = set()

    for lineno, row in enumerate(reader, start=2):
        if not row or all(not (c or "").strip() for c in row):
            continue
        data = {}
        for i, h in enumerate(headers):
            if i < len(row):
                data[h] = row[i].strip()
        try:
            _apply_row(db, data, stats, created_msgs)
        except Exception as exc:
            stats["errors"].append("line %d: %s" % (lineno, exc))
    return stats


def import_csv_file(db: dbcparse.DbcFile, path: str) -> dict:
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        return import_csv_text(db, f.read())


def _apply_row(db, data, stats, created_msgs):
    msg_name = data.get("message") or ""
    sig_name = data.get("signal") or ""
    if not msg_name and not sig_name:
        raise ValueError("missing message/signal")

    cid = None
    if data.get("id"):
        cid = _parse_id(data["id"])
    elif msg_name:
        existing = db.message_by_name(msg_name)
        if existing:
            cid = existing.can_id

    if cid is None:
        cid = 0x100
        while cid in db.messages:
            cid += 1

    msg = db.messages.get(cid)
    if msg is None:
        msg = dbcparse.Message()
        msg.can_id = cid
        msg.name = msg_name or ("Msg_%X" % cid)
        msg.dlc = 8
        msg.sender = "Vector__XXX"
        db.messages[cid] = msg
        created_msgs.add(cid)
        stats["messages"] += 1
    else:
        if msg_name:
            msg.name = msg_name

    if data.get("dlc"):
        msg.dlc = max(1, min(64, int(float(data["dlc"]))))
    if data.get("sender"):
        msg.sender = data["sender"]
        if msg.sender not in db.nodes:
            db.nodes.append(msg.sender)
    if data.get("cycle_ms"):
        try:
            msg.cycle_time = int(float(data["cycle_ms"]))
            msg.attributes["GenMsgCycleTime"] = msg.cycle_time
        except ValueError:
            pass

    if not sig_name:
        return

    sig = msg.signal(sig_name)
    if sig is None:
        sig = dbcparse.Signal()
        sig.name = sig_name
        sig.bit_length = 1
        msg.signals.append(sig)
        stats["signals"] += 1
    if data.get("start_bit") not in (None, ""):
        sig.start_bit = int(float(data["start_bit"]))
    if data.get("bit_length") not in (None, ""):
        sig.bit_length = int(float(data["bit_length"]))
    if data.get("byte_order") not in (None, ""):
        sig.little_endian = _parse_endian(data["byte_order"])
    if data.get("signed") not in (None, ""):
        sig.is_signed = _parse_bool_signed(data["signed"])
    if data.get("factor") not in (None, ""):
        sig.factor = float(data["factor"])
    if data.get("offset") not in (None, ""):
        sig.offset = float(data["offset"])
    if data.get("min") not in (None, ""):
        sig.minimum = float(data["min"])
    if data.get("max") not in (None, ""):
        sig.maximum = float(data["max"])
    if data.get("unit") not in (None, ""):
        sig.unit = data["unit"]
    if data.get("receivers") not in (None, ""):
        sig.receivers = _parse_receivers(data["receivers"])
        for r in sig.receivers:
            if r not in db.nodes:
                db.nodes.append(r)
    if data.get("comment") not in (None, ""):
        sig.comment = data["comment"]
    if data.get("value_table") not in (None, ""):
        sig.value_table = _parse_values(data["value_table"])
        sig.value_table_name = ""


def template_csv() -> str:
    return (
        "message,id,dlc,sender,cycle_ms,signal,start_bit,bit_length,"
        "byte_order,signed,factor,offset,min,max,unit,receivers,comment\n"
        "EngineData,0x100,8,ECU1,10,RPM,0,16,Intel,unsigned,0.25,0,"
        "0,8000,rpm,ECU2,\n"
        "EngineData,0x100,8,ECU1,10,Temp,16,8,Intel,signed,1,-40,"
        "-40,215,C,ECU2,\n"
    )
