# -*- coding: utf-8 -*-
"""Interop MIME helpers (no Qt required for payload encode/decode)."""

from __future__ import annotations

import ast
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SUITE = os.path.dirname(_HERE)
if _SUITE not in sys.path:
    sys.path.insert(0, _SUITE)


def _load_payload_fns():
    path = os.path.join(_SUITE, "pages", "interop.py")
    with open(path, "r", encoding="utf-8") as f:
        tree = ast.parse(f.read(), filename=path)
    wanted = {
        "od_payload", "parse_od_payload",
        "profile_payload", "parse_profile_payload",
        "node_payload", "parse_node_payload",
        "MIME_OD", "MIME_PROFILE", "MIME_NODE",
    }
    ns: dict = {"json": __import__("json")}
    for node in tree.body:
        if isinstance(node, ast.Assign):
            names = [t.id for t in node.targets if isinstance(t, ast.Name)]
            if any(n in wanted for n in names):
                exec(compile(ast.Module([node], type_ignores=[]), path, "exec"), ns)
        elif isinstance(node, ast.FunctionDef) and node.name in wanted:
            exec(compile(ast.Module([node], type_ignores=[]), path, "exec"), ns)
    missing = wanted - ns.keys()
    if missing:
        raise AssertionError("missing %s" % missing)
    return ns


def test_payloads():
    ns = _load_payload_fns()
    raw = ns["od_payload"](0x1018, 1, "Vendor-ID")
    parsed = ns["parse_od_payload"](raw)
    assert parsed["index"] == 0x1018
    assert parsed["subindex"] == 1
    assert parsed["name"] == "Vendor-ID"
    assert ns["parse_profile_payload"](ns["profile_payload"]("402")) == "402"
    assert ns["parse_node_payload"](ns["node_payload"](32)) == 32
    assert ns["parse_od_payload"](b"not-json") is None
    assert ns["MIME_OD"].startswith("application/x-canopen")
    print("PASS interop payloads")


if __name__ == "__main__":
    test_payloads()
    print("All interop tests passed")
