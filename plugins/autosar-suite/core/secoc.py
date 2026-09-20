# -*- coding: utf-8 -*-
"""Secured I-PDU tail: truncated freshness value then truncated MAC.

Layout from the end of the PDU (AUTOSAR SecOC authentic I-PDU):
  payload | freshness (fv_bits) | authenticator (mac_bits)
Demo authenticator is HMAC-SHA256 truncated to mac_bits. It is not a
production CMAC-AES stack; the bit split matches the on-wire layout.
"""

from __future__ import annotations

import hashlib
import hmac


def _int_from_bits(data: bytes) -> int:
    return int.from_bytes(data, "big") if data else 0


def split_secured(pdu: bytes, fv_bits: int, mac_bits: int) -> dict:
    fv_bits = max(0, int(fv_bits))
    mac_bits = max(0, int(mac_bits))
    width = len(pdu) * 8
    if fv_bits + mac_bits > width:
        return {"ok": False, "reason": "tail longer than PDU"}
    val = _int_from_bits(pdu)
    mac_mask = (1 << mac_bits) - 1 if mac_bits else 0
    mac = val & mac_mask
    val >>= mac_bits
    fv_mask = (1 << fv_bits) - 1 if fv_bits else 0
    fv = val & fv_mask
    val >>= fv_bits
    payload_bits = width - fv_bits - mac_bits
    payload_len = payload_bits // 8
    # Keep leftover high bits inside the payload integer, then emit full bytes.
    payload = val.to_bytes(max(1, (payload_bits + 7) // 8), "big") if payload_bits else b""
    if payload_bits % 8:
        payload = payload[-payload_len:] if payload_len else b""
        # Non-byte payload: return the integer via hex of the remaining value.
        payload = val.to_bytes((payload_bits + 7) // 8, "big")
    else:
        payload = val.to_bytes(payload_len, "big") if payload_len else b""
    return {
        "ok": True,
        "payload": payload,
        "freshness": fv,
        "mac": mac,
        "payload_bits": payload_bits,
    }


def demo_mac(payload: bytes, freshness: int, fv_bits: int, mac_bits: int, key: bytes) -> int:
    if mac_bits <= 0:
        return 0
    fv_len = (max(0, fv_bits) + 7) // 8
    msg = bytes(payload) + int(freshness).to_bytes(max(1, fv_len), "big")
    digest = hmac.new(bytes(key or b"\x00"), msg, hashlib.sha256).digest()
    full = int.from_bytes(digest, "big")
    return full & ((1 << int(mac_bits)) - 1)


def build_secured(payload: bytes, freshness: int, fv_bits: int, mac_bits: int,
                  key: bytes) -> bytes:
    fv_bits = max(0, int(fv_bits))
    mac_bits = max(0, int(mac_bits))
    mac = demo_mac(payload, freshness, fv_bits, mac_bits, key)
    body = int.from_bytes(payload, "big") if payload else 0
    packed = (((body << fv_bits) | (int(freshness) & ((1 << fv_bits) - 1 if fv_bits else 0)))
              << mac_bits) | mac
    total_bits = len(payload) * 8 + fv_bits + mac_bits
    nbytes = (total_bits + 7) // 8
    return packed.to_bytes(nbytes, "big")


def verify_secured(pdu: bytes, fv_bits: int, mac_bits: int, key: bytes) -> dict:
    parts = split_secured(pdu, fv_bits, mac_bits)
    if not parts.get("ok"):
        return parts
    expect = demo_mac(parts["payload"], parts["freshness"], fv_bits, mac_bits, key)
    parts["expect_mac"] = expect
    parts["mac_ok"] = expect == parts["mac"]
    return parts
