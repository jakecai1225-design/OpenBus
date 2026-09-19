# -*- coding: utf-8 -*-
"""Load firmware images used by workshop tools (Intel HEX, Motorola S-record, raw BIN).

Returns (start_address, payload). Multi-region files are packed into one span
filled with 0xFF between regions so RequestDownload can use one address/size.
"""

from __future__ import annotations

import os


def load_firmware(path: str) -> tuple[int, bytes, str]:
    """Return (address, data, note). Address is 0 for raw binaries."""
    ext = os.path.splitext(path)[1].lower()
    with open(path, "rb") as f:
        raw = f.read()
    if ext in (".hex", ".ihex"):
        regions = _parse_ihex(raw.decode("ascii", "replace"))
        note = "Intel HEX"
    elif ext in (".s19", ".s28", ".s37", ".srec", ".mot"):
        regions = _parse_srec(raw.decode("ascii", "replace"))
        note = "Motorola S-record"
    else:
        return 0, raw, "raw binary"
    if not regions:
        raise ValueError("No data records in %s" % os.path.basename(path))
    regions.sort()
    start = regions[0][0]
    end = max(a + len(b) for a, b in regions)
    span = bytearray(b"\xFF" * (end - start))
    for addr, blob in regions:
        off = addr - start
        span[off:off + len(blob)] = blob
    gaps = len(regions) - 1
    if gaps:
        note += " (%d regions, gaps filled 0xFF)" % len(regions)
    return start, bytes(span), note


def _parse_ihex(text: str) -> list[tuple[int, bytes]]:
    upper = 0
    out: list[tuple[int, bytes]] = []
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        raw = bytes.fromhex(line[1:])
        if len(raw) < 5:
            continue
        count, addr, rtype = raw[0], int.from_bytes(raw[1:3], "big"), raw[3]
        data, _csum = raw[4:4 + count], raw[4 + count:4 + count + 1]
        if rtype == 0x00:
            out.append((upper + addr, bytes(data)))
        elif rtype == 0x04 and len(data) >= 2:
            upper = int.from_bytes(data[:2], "big") << 16
        elif rtype == 0x02 and len(data) >= 2:
            upper = int.from_bytes(data[:2], "big") << 4
        elif rtype == 0x01:
            break
    return out


def _parse_srec(text: str) -> list[tuple[int, bytes]]:
    out: list[tuple[int, bytes]] = []
    alen = {"1": 2, "2": 3, "3": 4}
    for line in text.splitlines():
        line = line.strip()
        if len(line) < 4 or line[0] not in "Ss":
            continue
        kind = line[1]
        if kind not in alen:
            continue
        body = bytes.fromhex(line[2:])
        naddr = alen[kind]
        if len(body) < 1 + naddr + 1:
            continue
        addr = int.from_bytes(body[1:1 + naddr], "big")
        data = body[1 + naddr:-1]
        if data:
            out.append((addr, bytes(data)))
    return out
