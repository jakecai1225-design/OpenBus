# -*- coding: utf-8 -*-
"""AUTOSAR CanNm PDU (common layout).

CAN ID = base + node id (default base 0x400) when the id is in range.
Byte 0: Control Bit Vector
  bit 0 Repeat Message Request
  bit 4 Active Wakeup
  bit 6 Partial Network Information (PNI)
Byte 1: source node identifier (when present)
Bytes 2..: user data
"""

from __future__ import annotations

from dataclasses import dataclass


CBV_REPEAT = 0x01
CBV_ACTIVE = 0x10
CBV_PNI = 0x40


@dataclass
class NmPdu:
    node_id: int
    repeat_request: bool
    active_wakeup: bool
    pni: bool
    cbv: int
    user_data: bytes
    can_id: int


def decode_nm(can_id: int, data: bytes, base: int = 0x400) -> NmPdu | None:
    if not data:
        return None
    nid = int(can_id) - int(base)
    if nid < 0 or nid > 0xFF:
        return None
    cbv = data[0]
    src = data[1] if len(data) > 1 else nid & 0xFF
    return NmPdu(
        node_id=src,
        repeat_request=bool(cbv & CBV_REPEAT),
        active_wakeup=bool(cbv & CBV_ACTIVE),
        pni=bool(cbv & CBV_PNI),
        cbv=cbv,
        user_data=bytes(data[2:]),
        can_id=int(can_id),
    )


def encode_nm(node_id: int, user_data: bytes = b"", *,
              repeat: bool = False, active: bool = True, pni: bool = False,
              base: int = 0x400) -> tuple:
    cbv = 0
    if repeat:
        cbv |= CBV_REPEAT
    if active:
        cbv |= CBV_ACTIVE
    if pni:
        cbv |= CBV_PNI
    payload = bytes([cbv & 0xFF, int(node_id) & 0xFF]) + bytes(user_data[:6])
    return int(base) + (int(node_id) & 0xFF), payload


def nm_state_name(pdu: NmPdu) -> str:
    if pdu.repeat_request:
        return "Repeat Message"
    if pdu.active_wakeup:
        return "Normal"
    return "Ready Sleep"
