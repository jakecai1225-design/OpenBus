# -*- coding: utf-8 -*-
"""DBC lint rule engine (ported from dbc-lint)."""

from __future__ import annotations

import json
import os
import re
from typing import Optional

from _shared import state_store

PLUGIN_ID = "dbc-studio"

RESERVED = {
    "class", "struct", "union", "enum", "typedef", "static", "const",
    "void", "int", "long", "short", "float", "double", "char", "if",
    "else", "for", "while", "switch", "case", "return", "break",
}

DEFAULT_RULES = {
    "naming": {
        "enabled": True,
        "severity": "error",
        "suppress": [],
        "title": "Invalid identifier characters",
    },
    "reserved": {
        "enabled": True,
        "severity": "error",
        "suppress": [],
        "title": "C reserved word",
    },
    "overlap": {
        "enabled": True,
        "severity": "error",
        "suppress": [],
        "title": "Signal bit overlap",
    },
    "bounds": {
        "enabled": True,
        "severity": "error",
        "suppress": [],
        "title": "Signal exceeds DLC",
    },
    "minmax_factor": {
        "enabled": True,
        "severity": "warning",
        "suppress": [],
        "title": "min/max vs factor/offset",
    },
    "missing_cycle": {
        "enabled": True,
        "severity": "info",
        "suppress": [],
        "title": "Missing GenMsgCycleTime",
    },
    "missing_comment": {
        "enabled": True,
        "severity": "info",
        "suppress": [],
        "title": "Missing comment",
    },
    "missing_valuetable": {
        "enabled": True,
        "severity": "info",
        "suppress": [],
        "title": "Missing value table",
    },
    "parse_warning": {
        "enabled": True,
        "severity": "warning",
        "suppress": [],
        "title": "Parser warning",
    },
}

_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_MAX_NAME_LEN = 64


def load_rules() -> dict:
    rules = {k: dict(v) for k, v in DEFAULT_RULES.items()}
    data = state_store.load_state(PLUGIN_ID, "lint_rules.json")
    if not isinstance(data, dict):
        return rules
    for rid, cfg in data.items():
        if rid not in rules or not isinstance(cfg, dict):
            continue
        if "enabled" in cfg:
            rules[rid]["enabled"] = bool(cfg["enabled"])
        if "severity" in cfg and cfg["severity"] in ("error", "warning", "info"):
            rules[rid]["severity"] = cfg["severity"]
        if "suppress" in cfg and isinstance(cfg["suppress"], list):
            rules[rid]["suppress"] = [str(x) for x in cfg["suppress"]]
    return rules


def save_rules(rules: dict) -> str:
    payload = {}
    for rid, cfg in rules.items():
        payload[rid] = {
            "enabled": bool(cfg.get("enabled", True)),
            "severity": cfg.get("severity", "warning"),
            "suppress": list(cfg.get("suppress") or []),
        }
    return state_store.save_state(PLUGIN_ID, payload, "lint_rules.json")


def _signal_bits(sig) -> set:
    from _shared.dbcparse import signal_bit_numbers
    return set(signal_bit_numbers(sig.start_bit, sig.bit_length, sig.little_endian))


def _raw_range(sig) -> tuple:
    if not sig.is_signed:
        return 0.0, float((1 << sig.bit_length) - 1) if sig.bit_length > 0 else 0.0
    if sig.bit_length <= 1:
        return -1.0, 0.0
    half = 1 << (sig.bit_length - 1)
    return float(-half), float(half - 1)


def lint_dbc(db, rules: Optional[dict] = None) -> list:
    """Return list of finding dicts with severity, location, rule, message."""
    rules = rules or load_rules()
    findings = []

    def _emit(rule_id, location, message, default_sev=None, can_id=None, signal=None):
        cfg = rules.get(rule_id) or {}
        if not cfg.get("enabled", True):
            return
        for pat in cfg.get("suppress") or []:
            if pat and (pat in location or pat in message):
                return
        sev = cfg.get("severity") or default_sev or "warning"
        findings.append({
            "severity": sev,
            "location": location,
            "rule": rule_id,
            "message": message,
            "title": cfg.get("title") or rule_id,
            "can_id": can_id,
            "signal": signal,
        })

    for cid, m in db.messages.items():
        loc = "message 0x%X %s" % (cid, m.name)
        if not _NAME_RE.match(m.name) or len(m.name) > _MAX_NAME_LEN:
            _emit("naming", loc, "Invalid message name %r (length<=%d)" % (
                m.name, _MAX_NAME_LEN), can_id=cid)
        if m.name.lower() in RESERVED:
            _emit("reserved", loc, "Message name is a C reserved word: %s" % m.name,
                  can_id=cid)
        if m.cycle_time == 0 and "GenMsgCycleTime" not in (m.attributes or {}):
            _emit("missing_cycle", loc, "No GenMsgCycleTime (event frame?)", can_id=cid)
        if not m.comment:
            _emit("missing_comment", loc, "Message has no comment", can_id=cid)

        bit_owner = {}
        bits_available = m.dlc * 8
        for s in m.signals:
            sloc = "%s / signal %s" % (loc, s.name)
            if not _NAME_RE.match(s.name) or len(s.name) > _MAX_NAME_LEN:
                _emit("naming", sloc, "Invalid signal name %r" % s.name,
                      can_id=cid, signal=s.name)
            if s.name.lower() in RESERVED:
                _emit("reserved", sloc, "Signal name is a C reserved word: %s" % s.name,
                      can_id=cid, signal=s.name)

            occupied = _signal_bits(s)
            if any(b < 0 or b >= bits_available for b in occupied) or (
                not occupied and s.bit_length > 0
            ):
                _emit(
                    "bounds",
                    sloc,
                    "start %d + length %d exceeds DLC %d*8" % (
                        s.start_bit, s.bit_length, m.dlc),
                    can_id=cid, signal=s.name,
                )
            for b in occupied:
                if b in bit_owner:
                    _emit(
                        "overlap",
                        sloc,
                        "bit %d overlaps signal %s" % (b, bit_owner[b]),
                        can_id=cid, signal=s.name,
                    )
                    break
                bit_owner[b] = s.name

            if s.factor and s.factor != 0:
                raw_min, raw_max = _raw_range(s)
                phys_min = raw_min * s.factor + s.offset
                phys_max = raw_max * s.factor + s.offset
                lo, hi = ((phys_min, phys_max) if phys_min <= phys_max
                          else (phys_max, phys_min))
                tol_lo = abs(lo) * 0.01
                tol_hi = abs(hi) * 0.01
                if s.minimum < lo - tol_lo or s.minimum > hi + tol_hi:
                    _emit(
                        "minmax_factor",
                        sloc,
                        "min %g outside representable [%g, %g]" % (s.minimum, lo, hi),
                        can_id=cid, signal=s.name,
                    )
                if s.maximum > hi + tol_hi or s.maximum < lo - tol_lo:
                    _emit(
                        "minmax_factor",
                        sloc,
                        "max %g outside representable [%g, %g]" % (s.maximum, lo, hi),
                        can_id=cid, signal=s.name,
                    )
                if s.minimum > s.maximum:
                    _emit(
                        "minmax_factor",
                        sloc,
                        "min %g > max %g" % (s.minimum, s.maximum),
                        default_sev="error",
                        can_id=cid, signal=s.name,
                    )

            if s.value_table:
                for val, _desc in s.value_table.items():
                    if s.minimum != s.maximum and (val < s.minimum or val > s.maximum):
                        _emit(
                            "minmax_factor",
                            sloc,
                            "value-table entry %s outside [%g, %g]" % (
                                val, s.minimum, s.maximum),
                            can_id=cid, signal=s.name,
                        )
            elif s.bit_length <= 2 and not s.comment:
                _emit(
                    "missing_valuetable",
                    sloc,
                    "<=2-bit signal should define a VAL_ table",
                    can_id=cid, signal=s.name,
                )
            if not s.comment and s.bit_length > 2:
                _emit("missing_comment", sloc, "Signal has no comment",
                      can_id=cid, signal=s.name)

    for w in db.warnings[:80]:
        _emit("parse_warning", "file", w)

    return findings


def findings_to_rows(findings: list) -> list:
    return [[f["severity"], f["location"], f["rule"], f["message"]] for f in findings]


def to_sarif(findings: list, dbc_path: str = "") -> dict:
    rules_seen = {}
    results = []
    for f in findings:
        rid = f["rule"]
        if rid not in rules_seen:
            rules_seen[rid] = {
                "id": rid,
                "name": rid,
                "shortDescription": {"text": f.get("title") or rid},
            }
        level = {"error": "error", "warning": "warning", "info": "note"}.get(
            f["severity"], "warning"
        )
        results.append({
            "ruleId": rid,
            "level": level,
            "message": {"text": f["message"]},
            "locations": [{
                "physicalLocation": {
                    "artifactLocation": {"uri": dbc_path or "dbc"},
                    "region": {"snippet": {"text": f["location"]}},
                }
            }],
        })
    return {
        "version": "2.1.0",
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "runs": [{
            "tool": {
                "driver": {
                    "name": "dbc-studio",
                    "informationUri": "https://github.com/openbus",
                    "rules": list(rules_seen.values()),
                }
            },
            "results": results,
        }],
    }


def write_json_report(path: str, findings: list, dbc_path: str, msg_count: int) -> None:
    payload = {
        "file": dbc_path,
        "messages": msg_count,
        "findings": findings,
        "counts": {
            "error": sum(1 for f in findings if f["severity"] == "error"),
            "warning": sum(1 for f in findings if f["severity"] == "warning"),
            "info": sum(1 for f in findings if f["severity"] == "info"),
        },
    }
    with open(path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False)


def write_sarif_report(path: str, findings: list, dbc_path: str) -> None:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(to_sarif(findings, dbc_path), f, indent=2, ensure_ascii=False)
