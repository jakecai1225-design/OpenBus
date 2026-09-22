# -*- coding: utf-8 -*-
"""Compatibility shim — implementation lives in plugins/_shared/arxmlparse.py."""

from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SUITE)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)

from _shared import arxmlparse as _ap  # noqa: E402
from core.ipdu import Ipdu, Signal  # noqa: E402


def _to_suite(ipdu: _ap.Ipdu) -> Ipdu:
    return Ipdu.from_dict(ipdu.to_dict())


def _from_suite(ipdu: Ipdu) -> _ap.Ipdu:
    return _ap.Ipdu.from_dict(ipdu.to_dict())


def parse_arxml(path: str):
    return [_to_suite(p) for p in _ap.parse_arxml(path)]


def parse_root(root):
    return [_to_suite(p) for p in _ap.parse_root(root)]


def serialize_arxml(ipdus: list, package: str = "OpenBus") -> str:
    return _ap.serialize_arxml(
        [_from_suite(p) for p in ipdus], package=package)


def validate_ipdus(ipdus: list) -> list:
    findings = _ap.validate_ipdus(
        [_from_suite(p) for p in ipdus], deep=True)
    # Keep legacy keys for suite UI
    out = []
    for f in findings:
        out.append({
            "level": f.get("level", "info"),
            "message": f.get("message", ""),
            "pdu": f.get("pdu", ""),
            "signal": f.get("signal", ""),
        })
    return out


def export_dbc(ipdus: list, bus_name: str = "AUTOSAR") -> str:
    return _ap.export_dbc([_from_suite(p) for p in ipdus], bus_name=bus_name)


__all__ = [
    "parse_arxml",
    "parse_root",
    "serialize_arxml",
    "validate_ipdus",
    "export_dbc",
]
