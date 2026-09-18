# -*- coding: utf-8 -*-
"""Fix nested activate() and mangled sin.output remnants in suite pages."""
from __future__ import annotations

import ast
import os
import re

PAGES = [
    r"d:/openbus/openbus_20260727/sin/plugins/protocol-hub/pages/nm.py",
    r"d:/openbus/openbus_20260727/sin/plugins/protocol-hub/pages/isotp.py",
    r"d:/openbus/openbus_20260727/sin/plugins/protocol-hub/pages/isobus.py",
    r"d:/openbus/openbus_20260727/sin/plugins/protocol-hub/pages/nmea2000.py",
    r"d:/openbus/openbus_20260727/sin/plugins/protocol-hub/pages/gbt27930.py",
    r"d:/openbus/openbus_20260727/sin/plugins/protocol-hub/pages/xcp.py",
    r"d:/openbus/openbus_20260727/sin/plugins/bus-utilities/pages/bit_timing.py",
    r"d:/openbus/openbus_20260727/sin/plugins/bus-utilities/pages/gateway.py",
    r"d:/openbus/openbus_20260727/sin/plugins/j1939-suite/pages/analyzer.py",
    r"d:/openbus/openbus_20260727/sin/plugins/obd-suite/pages/scanner.py",
]


def unwrap(path: str) -> None:
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()

    # Remove mangled leftovers like:    ")\n  or orphan quote lines
    text = re.sub(r'\n\s+"\)\s*\n', "\n", text)
    text = re.sub(r"\n\s+\"\)\s*\n", "\n", text)

    marker = "def build(parent, session, log_fn):\n"
    idx = text.find(marker)
    if idx < 0:
        print("no build:", path)
        return
    head = text[: idx + len(marker)]
    rest = text[idx + len(marker) :]

    # If nested activate exists, strip its def line and dedent one level
    rest2 = re.sub(r"^\n?\s*def activate\(context\):\n", "", rest, count=1)
    if rest2 == rest:
        print("no nested activate:", path)
        body = rest
    else:
        body = rest2
        lines = []
        for ln in body.splitlines(True):
            if ln.startswith("        "):
                lines.append(ln[4:])
            elif ln.startswith("\t\t"):
                lines.append(ln[1:])
            else:
                lines.append(ln)
        body = "".join(lines)

    # Drop duplicate trailing return root / empty activate leftovers
    body = body.rstrip() + "\n"
    # Ensure exactly one return root at end of build
    # Remove any return root that isn't the final statement at indent 4
    parts = body.split("\n")
    # Keep all but ensure last non-empty is "    return root"
    while parts and not parts[-1].strip():
        parts.pop()
    # Remove orphan lines that are just ")"
    cleaned = []
    for ln in parts:
        if ln.strip() in ('")', "')", ")"):
            continue
        cleaned.append(ln)
    parts = cleaned
    while parts and parts[-1].strip() == "return root":
        parts.pop()
    # Also remove wrongly indented return root
    while parts and parts[-1].strip().endswith("return root"):
        parts.pop()
    parts.append("    return root")
    body = "\n".join(parts) + "\n"

    new_text = head + "\n" + body
    # Validate syntax
    try:
        ast.parse(new_text)
    except SyntaxError as e:
        print("SYNTAX FAIL", path, e)
        # still write for inspection
        with open(path + ".broken", "w", encoding="utf-8", newline="\n") as f:
            f.write(new_text)
        return

    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(new_text)
    print("ok", path)


def main():
    for p in PAGES:
        unwrap(p)


if __name__ == "__main__":
    main()
