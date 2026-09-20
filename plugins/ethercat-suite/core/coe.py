# -*- coding: utf-8 -*-
"""CoE mailbox: header + SDO expedited upload/download (CANopen-like).

Mailbox byte 5: low nibble type (CoE = 3), high nibble counter.
CoE header: service in the top 4 bits (SDO request = 2).
"""

from __future__ import annotations

MBX_COE = 0x03
COE_SDO_REQ = 0x02
SDO_UPLOAD_REQ = 0x40
SDO_DOWNLOAD_REQ = 0x23


def build_mailbox(payload: bytes, mtype: int = MBX_COE, address: int = 0,
                  counter: int = 1) -> bytes:
    body = bytes(payload)
    type_cnt = ((int(counter) & 0x7) << 4) | (int(mtype) & 0xF)
    hdr = len(body).to_bytes(2, "little") + int(address & 0xFFFF).to_bytes(2, "little")
    hdr += bytes([0, type_cnt])
    return hdr + body


def parse_mailbox(data: bytes) -> dict:
    if len(data) < 6:
        return {"ok": False, "reason": "short"}
    length = int.from_bytes(data[0:2], "little")
    address = int.from_bytes(data[2:4], "little")
    type_cnt = data[5]
    payload = data[6:6 + length]
    return {
        "ok": True,
        "length": length,
        "address": address,
        "type": type_cnt & 0xF,
        "counter": (type_cnt >> 4) & 0x7,
        "payload": payload,
    }


def _coe_hdr(service: int) -> bytes:
    return int((int(service) & 0xF) << 12).to_bytes(2, "little")


def sdo_upload_request(index: int, subindex: int, address: int = 0,
                       counter: int = 1) -> bytes:
    sdo = bytes([
        SDO_UPLOAD_REQ,
        index & 0xFF,
        (index >> 8) & 0xFF,
        subindex & 0xFF,
        0, 0, 0, 0,
    ])
    return build_mailbox(_coe_hdr(COE_SDO_REQ) + sdo, address=address, counter=counter)


def sdo_download_request(index: int, subindex: int, value: int, size: int = 4,
                         address: int = 0, counter: int = 1) -> bytes:
    size = 1 if size <= 1 else 2 if size <= 2 else 4
    # expedited size nibble: 4-size in bits 2..3, expedited+size indicated
    n = 4 - size
    cs = 0x23 | (n << 2)
    raw = int(value).to_bytes(4, "little")
    sdo = bytes([
        cs,
        index & 0xFF,
        (index >> 8) & 0xFF,
        subindex & 0xFF,
    ]) + raw
    return build_mailbox(_coe_hdr(COE_SDO_REQ) + sdo, address=address, counter=counter)


def parse_sdo(mailbox: bytes) -> dict:
    mbx = parse_mailbox(mailbox)
    if not mbx.get("ok"):
        return mbx
    payload = mbx["payload"]
    if len(payload) < 10:
        return {"ok": False, "reason": "coe short", **mbx}
    service = (int.from_bytes(payload[0:2], "little") >> 12) & 0xF
    cs = payload[2]
    index = payload[3] | (payload[4] << 8)
    sub = payload[5]
    data = payload[6:10]
    return {
        "ok": True,
        "service": service,
        "cs": cs,
        "index": index,
        "subindex": sub,
        "data": data,
        "type": mbx["type"],
        "address": mbx["address"],
    }
