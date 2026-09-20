# -*- coding: utf-8 -*-
"""EtherCAT frame header and datagrams (little-endian).

Ethernet type 0x88A4 is stripped when present.
Datagram length word: bits 0-10 data length, bit 15 = more datagrams follow.
"""

from __future__ import annotations

from dataclasses import dataclass


ETHER_TYPE = 0x88A4

CMDS = {
    0: "NOP",
    1: "APRD",
    2: "APWR",
    3: "APRW",
    4: "FPRD",
    5: "FPWR",
    6: "FPRW",
    7: "BRD",
    8: "BWR",
    9: "BRW",
    10: "LRD",
    11: "LWR",
    12: "LRW",
    13: "ARMW",
    14: "FRMW",
}

CMD_BY_NAME = {v: k for k, v in CMDS.items()}
MORE = 0x8000


@dataclass
class Datagram:
    cmd: int
    index: int
    adp: int
    ado: int
    data: bytes
    irq: int
    wkc: int

    @property
    def cmd_name(self) -> str:
        return CMDS.get(self.cmd, "CMD%d" % self.cmd)


@dataclass
class EthercatFrame:
    length: int
    frame_type: int
    datagrams: list


def _u16(data: bytes, off: int) -> int:
    return int.from_bytes(data[off:off + 2], "little")


def encode_datagram(dg: Datagram, more: bool) -> bytes:
    dlen = len(dg.data) & 0x7FF
    if more:
        dlen |= MORE
    return bytes([
        dg.cmd & 0xFF,
        dg.index & 0xFF,
    ]) + int(dg.adp & 0xFFFF).to_bytes(2, "little") \
        + int(dg.ado & 0xFFFF).to_bytes(2, "little") \
        + int(dlen).to_bytes(2, "little") \
        + int(dg.irq & 0xFFFF).to_bytes(2, "little") \
        + bytes(dg.data) \
        + int(dg.wkc & 0xFFFF).to_bytes(2, "little")


def encode_frame(datagrams: list, frame_type: int = 1) -> bytes:
    body = b""
    for i, dg in enumerate(datagrams):
        body += encode_datagram(dg, more=i < len(datagrams) - 1)
    hdr = (len(body) & 0x07FF) | ((int(frame_type) & 0xF) << 12)
    return int(hdr).to_bytes(2, "little") + body


def parse_frame(raw: bytes) -> EthercatFrame:
    data = bytes(raw)
    if len(data) >= 16 and int.from_bytes(data[12:14], "big") == ETHER_TYPE:
        data = data[14:]
    if len(data) < 2:
        raise ValueError("EtherCAT frame too short")
    hdr = _u16(data, 0)
    length = hdr & 0x07FF
    ftype = (hdr >> 12) & 0xF
    end = min(len(data), 2 + length) if length else len(data)
    pos = 2
    out = []
    while pos + 12 <= len(data) and pos < end:
        cmd = data[pos]
        index = data[pos + 1]
        adp = _u16(data, pos + 2)
        ado = _u16(data, pos + 4)
        dl = _u16(data, pos + 6)
        irq = _u16(data, pos + 8)
        dlen = dl & 0x7FF
        more = bool(dl & MORE)
        pos += 10
        if pos + dlen + 2 > len(data):
            break
        payload = data[pos:pos + dlen]
        pos += dlen
        wkc = _u16(data, pos)
        pos += 2
        out.append(Datagram(cmd, index, adp, ado, payload, irq, wkc))
        if not more:
            break
    return EthercatFrame(length=length, frame_type=ftype, datagrams=out)
