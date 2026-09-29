# -*- coding: utf-8 -*-
"""EDS-driven CANopen frame decode (PDO payload → OD objects).

Industry pattern (Vector CANalyzer.CANopen / Ixxat / python-canopen):
  EDS/DCF → Object Dictionary + PDO maps + COB-IDs
  → classify frame → unpack PDO bits / interpret SDO mux
  → named application values (like DBC signal decode).

First-tier Trace extras:
  - Full CiA 301 SDO abort text
  - Segmented + block SDO reassembly
  - EMCY / SYNC / TIME / LSS
  - Heartbeat vs Node Guarding (toggle)
  - CiA 402 Statusword/Controlword
  - Multi-node PDO layouts
  - Protocol anomaly hints (timeout, SYNC gap, toggle)
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

from core.cia301_codes import (
    CONTROLWORD_BITS,
    STATUSWORD_BITS,
    bitfield_summary,
    emcy_error_text,
    error_register_text,
    lss_cs_text,
    sdo_abort_text,
    statusword_state,
)
from core.cob_classify import classify_cob
from core.pdo_map import decode_mapping


_CIA_DTYPE = {
    0x0001: (1, False, False),
    0x0002: (8, True, False),
    0x0003: (16, True, False),
    0x0004: (32, True, False),
    0x0005: (8, False, False),
    0x0006: (16, False, False),
    0x0007: (32, False, False),
    0x0008: (32, False, True),
    0x0009: (64, False, False),
    0x000A: (64, False, False),
    0x000B: (48, False, False),
    0x000C: (48, False, False),
    0x000D: (64, False, False),
    0x000F: (48, False, False),
    0x0010: (24, True, False),
    0x0011: (64, False, False),
    0x0012: (40, True, False),
    0x0013: (48, True, False),
    0x0014: (56, True, False),
    0x0015: (64, True, False),
    0x0016: (24, False, False),
    0x0018: (40, False, False),
    0x0019: (48, False, False),
    0x001A: (56, False, False),
    0x001B: (64, False, False),
}

_NMT_CMD = {
    0x01: "Start",
    0x02: "Stop",
    0x80: "Pre-op",
    0x81: "Reset node",
    0x82: "Reset communication",
}

_HB_STATE = {
    0x00: "Boot-up",
    0x04: "Stopped",
    0x05: "Operational",
    0x7F: "Pre-operational",
}


@dataclass
class MappedObject:
    index: int
    subindex: int
    name: str
    bit_offset: int
    bit_length: int
    signed: bool = False
    is_float: bool = False

    @property
    def key(self) -> Tuple[int, int]:
        return (self.index, self.subindex)

    def display(self) -> str:
        if self.subindex:
            return "0x%04X:%02X %s" % (self.index, self.subindex, self.name)
        return "0x%04X %s" % (self.index, self.name)


@dataclass
class PdoLayout:
    cob_id: int
    kind: str
    node_id: int
    objects: List[MappedObject] = field(default_factory=list)


@dataclass
class DecodedValue:
    index: int
    subindex: int
    name: str
    raw: int
    value: object
    text: str
    extra: str = ""

    def display(self) -> str:
        if self.subindex:
            return "0x%04X:%02X %s" % (self.index, self.subindex, self.name)
        return "0x%04X %s" % (self.index, self.name)


@dataclass
class Interaction:
    kind: str
    node_id: Optional[int]
    summary: str
    detail: str = ""
    values: List[DecodedValue] = field(default_factory=list)
    layer: str = "protocol"
    anomalies: List[str] = field(default_factory=list)


@dataclass
class _SegSdo:
    index: int
    subindex: int
    name: str
    direction: str
    size: int = 0
    toggle: int = 0
    buf: bytearray = field(default_factory=bytearray)


@dataclass
class _BlockSdo:
    index: int
    subindex: int
    name: str
    direction: str  # upload | download
    size: int = 0
    blksize: int = 127
    expect_seq: int = 1
    buf: bytearray = field(default_factory=bytearray)
    last_unused: int = 0
    phase: str = "init"  # init | data


def _parse_int(text: str, default: int = 0) -> int:
    t = (text or "").strip()
    if not t:
        return default
    try:
        return int(t, 0)
    except ValueError:
        return default


def _dtype_info(data_type: str, mapped_bits: int) -> Tuple[int, bool, bool]:
    code = _parse_int(data_type, 0)
    info = _CIA_DTYPE.get(code)
    if info:
        bits, signed, is_float = info
        if mapped_bits > 0:
            bits = mapped_bits
        return bits, signed, is_float
    bits = mapped_bits if mapped_bits > 0 else 8
    return bits, False, False


def _entry_map(entries: Sequence) -> Dict[Tuple[int, int], object]:
    out = {}
    for e in entries or ():
        out[(int(e.index), int(e.subindex))] = e
    return out


def _entry_value(em: Dict, index: int, sub: int) -> str:
    e = em.get((index, sub))
    if e is None:
        return ""
    return getattr(e, "effective_value", lambda: "")() or getattr(
        e, "parameter_value", "") or getattr(e, "default_value", "") or ""


def _entry_name(em: Dict, index: int, sub: int) -> str:
    e = em.get((index, sub))
    if e is None:
        return ""
    return getattr(e, "name", "") or ""


def _entry_dtype(em: Dict, index: int, sub: int) -> str:
    e = em.get((index, sub))
    if e is None:
        return ""
    return getattr(e, "data_type", "") or ""


def _cob_from_comm(raw: str, fallback: int) -> Optional[int]:
    v = _parse_int(raw, -1)
    if v < 0:
        return fallback
    if v & 0x80000000:
        return None
    return int(v) & 0x7FF


def build_pdo_layouts(
        entries: Sequence,
        node_id: int = 1) -> Dict[int, PdoLayout]:
    em = _entry_map(entries)
    node = max(1, min(127, int(node_id)))
    layouts: Dict[int, PdoLayout] = {}
    specs = (
        (0x1800, 0x1A00, "TPDO", 0x180, True),
        (0x1400, 0x1600, "RPDO", 0x200, False),
    )
    for slot in range(4):
        for comm_base, map_base, prefix, cob_base, _tx in specs:
            default_cob = (cob_base + slot * 0x100 + node) & 0x7FF
            cob = _cob_from_comm(
                _entry_value(em, comm_base + slot, 1), default_cob)
            if cob is None:
                continue
            objs: List[MappedObject] = []
            bit_off = 0
            n_raw = _entry_value(em, map_base + slot, 0)
            n_map = _parse_int(n_raw, 0) & 0xFF
            if n_map <= 0:
                n_map = 8
            for sub in range(1, n_map + 1):
                raw = _entry_value(em, map_base + slot, sub)
                mapped = decode_mapping(_parse_int(raw, 0))
                if mapped is None:
                    continue
                idx, subi, bits = mapped
                bits = int(bits) & 0xFF
                if bits <= 0:
                    continue
                dtype = _entry_dtype(em, idx, subi)
                _blen, signed, is_float = _dtype_info(dtype, bits)
                name = _entry_name(em, idx, subi) or (
                    "0x%04X:%02X" % (idx, subi))
                objs.append(MappedObject(
                    index=idx, subindex=subi, name=name,
                    bit_offset=bit_off, bit_length=bits,
                    signed=signed, is_float=is_float,
                ))
                bit_off += bits
                if bit_off >= 64:
                    break
            if not objs and cob == default_cob:
                continue
            kind = "%s%d" % (prefix, slot + 1)
            layouts[cob] = PdoLayout(
                cob_id=cob, kind=kind, node_id=node, objects=objs)
    return layouts


def extract_bits(data: bytes, start_bit: int, length: int) -> int:
    if length <= 0:
        return 0
    value = 0
    for i in range(length):
        bit = start_bit + i
        byte_i = bit // 8
        bit_i = bit % 8
        if byte_i >= len(data):
            break
        if data[byte_i] & (1 << bit_i):
            value |= (1 << i)
    return value


def _format_value(raw: int, bits: int, signed: bool, is_float: bool) -> Tuple[object, str]:
    if is_float and bits == 32:
        import struct
        phys = struct.unpack("<f", int(raw & 0xFFFFFFFF).to_bytes(4, "little"))[0]
        return phys, ("%.6g" % phys)
    if signed and bits > 0:
        sign_bit = 1 << (bits - 1)
        mask = (1 << bits) - 1
        v = raw & mask
        if v & sign_bit:
            v -= (1 << bits)
        return v, str(v)
    return raw, ("0x%X" % raw) if raw > 9 else str(raw)


def _annotate_cia402(index: int, raw: int, bits: int) -> str:
    if bits < 16:
        return ""
    v = int(raw) & 0xFFFF
    if index == 0x6041:
        return "state=%s; %s" % (
            statusword_state(v), bitfield_summary(v, STATUSWORD_BITS))
    if index == 0x6040:
        return bitfield_summary(v, CONTROLWORD_BITS)
    return ""


def decode_pdo_payload(layout: PdoLayout, data: bytes) -> List[DecodedValue]:
    out: List[DecodedValue] = []
    for obj in layout.objects:
        raw = extract_bits(data, obj.bit_offset, obj.bit_length)
        val, text = _format_value(raw, obj.bit_length, obj.signed, obj.is_float)
        extra = _annotate_cia402(obj.index, raw, obj.bit_length)
        if extra and obj.index == 0x6041:
            text = "%s [%s]" % (text, statusword_state(raw))
        out.append(DecodedValue(
            index=obj.index, subindex=obj.subindex, name=obj.name,
            raw=raw, value=val, text=text, extra=extra,
        ))
    return out


def _decode_time_payload(payload: bytes) -> str:
    if len(payload) < 6:
        return "TIME stamp (%d bytes)" % len(payload)
    ms = (payload[0] | (payload[1] << 8) | (payload[2] << 16)
          | ((payload[3] & 0x0F) << 24))
    days = payload[4] | (payload[5] << 8)
    h = ms // 3600000
    m = (ms % 3600000) // 60000
    s = (ms % 60000) // 1000
    milli = ms % 1000
    return "TIME %02d:%02d:%02d.%03d + %d days" % (h, m, s, milli, days)


def _decode_lss(payload: bytes, master: bool) -> str:
    if not payload:
        return "LSS %s (empty)" % ("master" if master else "slave")
    cs = payload[0]
    role = "LSS↓" if master else "LSS↑"
    base = "%s %s" % (role, lss_cs_text(cs))
    if cs == 0x04 and len(payload) >= 2:
        mode = "configuration" if payload[1] else "operation"
        return "%s → %s" % (base, mode)
    if cs == 0x40 and len(payload) >= 2:
        return "%s → node %d" % (base, payload[1])
    if cs in (0x44, 0x45, 0x46, 0x47) and len(payload) >= 5 and not master:
        val = int.from_bytes(payload[1:5], "little")
        return "%s = 0x%08X" % (base, val)
    if cs == 0x4E and len(payload) >= 2 and not master:
        return "%s = %d" % (base, payload[1])
    return base


def _finalize_sdo_value(
        em: Dict, index: int, sub: int, name: str,
        raw_bytes: bytes) -> Tuple[str, List[DecodedValue]]:
    if not raw_bytes:
        return ("SDO %s (empty data)" % name, [])
    size = len(raw_bytes)
    if size <= 4:
        raw = int.from_bytes(raw_bytes, "little", signed=False)
        dtype = _entry_dtype(em, index, sub)
        _b, signed, is_float = _dtype_info(dtype, size * 8)
        val, text = _format_value(raw, size * 8, signed, is_float)
        extra = _annotate_cia402(index, raw, size * 8)
        if extra and index == 0x6041:
            text = "%s [%s]" % (text, statusword_state(raw))
        dv = DecodedValue(index, sub, name, raw, val, text, extra)
        return ("SDO %s = %s" % (name, text), [dv])
    hex_txt = " ".join("%02X" % b for b in raw_bytes[:16])
    if size > 16:
        hex_txt += "…"
    dv = DecodedValue(
        index, sub, name, 0, raw_bytes, "%d bytes: %s" % (size, hex_txt))
    return ("SDO %s = %d bytes" % (name, size), [dv])


class ProtocolMonitor:
    """Light Trace-class sequence checks (Vector protocol monitoring)."""

    def __init__(self):
        self._last_hb_ts: Dict[int, float] = {}
        self._hb_period_ms: Dict[int, float] = {}
        self._last_sync: Optional[int] = None
        self._nmt: Dict[int, int] = {}
        self._hb_state: Dict[int, int] = {}

    def set_heartbeat_period(self, node: int, period_ms: int) -> None:
        if period_ms > 0:
            self._hb_period_ms[int(node)] = float(period_ms)

    def load_from_entries(self, entries: Sequence, node_id: int) -> None:
        em = _entry_map(entries)
        # 0x1017 Producer Heartbeat Time
        raw = _entry_value(em, 0x1017, 0)
        period = _parse_int(raw, 0)
        if period > 0:
            self.set_heartbeat_period(node_id, period)

    def observe(self, ts: float, ix: "Interaction",
                data: bytes) -> List[str]:
        notes: List[str] = []
        if ix.kind == "SYNC" and data:
            cur = data[0]
            if self._last_sync is not None:
                expect = (self._last_sync + 1) & 0xFF
                if cur != expect and not (self._last_sync == 0xFF and cur == 0):
                    # allow wrap; flag only large jumps
                    if ((cur - self._last_sync) & 0xFF) not in (1,):
                        notes.append("SYNC gap %d→%d" % (self._last_sync, cur))
            self._last_sync = cur

        if ix.kind in ("HB", "GUARD") and ix.node_id:
            nid = int(ix.node_id)
            period = self._hb_period_ms.get(nid)
            prev_ts = self._last_hb_ts.get(nid)
            if prev_ts is not None and period and period > 0:
                dt_ms = (ts - prev_ts) * 1000.0
                # CiA: consumer deadline typically 1.5× producer time
                if dt_ms > period * 1.5:
                    notes.append(
                        "HB late N%d: %.0fms > 1.5×%gms" % (nid, dt_ms, period))
            self._last_hb_ts[nid] = ts
            if data:
                st = data[0] & 0x7F
                prev = self._hb_state.get(nid)
                if prev is not None and prev != st:
                    notes.append(
                        "N%d state %s→%s" % (
                            nid,
                            _HB_STATE.get(prev, "0x%02X" % prev),
                            _HB_STATE.get(st, "0x%02X" % st)))
                self._hb_state[nid] = st

        if ix.kind == "NMT" and data and len(data) >= 2:
            cmd, tgt = data[0], data[1]
            if cmd in _NMT_CMD:
                targets = range(1, 128) if tgt == 0 else (tgt,)
                for n in targets:
                    self._nmt[n] = cmd

        if "abort" in ix.summary.lower():
            notes.append("SDO abort")
        if ix.kind == "EMCY" and data and len(data) >= 2:
            code = data[0] | (data[1] << 8)
            if code != 0:
                notes.append("EMCY active")

        return notes


class EdsBusDecoder:
    """Session-bound decoder: rebuild when EDS / Node-ID changes."""

    def __init__(self):
        self.node_id = 1
        self._layouts: Dict[int, PdoLayout] = {}
        self._entries: List = []
        self._em: Dict = {}
        self._node_entries: Dict[int, Dict] = {}
        self._seg: Dict[Tuple[int, str], _SegSdo] = {}
        self._block: Dict[int, _BlockSdo] = {}
        # Guarding: last toggle bit per node (None = unknown)
        self._guard_toggle: Dict[int, int] = {}
        self._guard_mode: Dict[int, str] = {}  # heartbeat | guarding
        self.monitor = ProtocolMonitor()
        self._last_ts = 0.0

    def rebuild(self, entries: Iterable, node_id: int = 1) -> int:
        self._layouts.clear()
        self._node_entries.clear()
        self._seg.clear()
        self._block.clear()
        self._guard_toggle.clear()
        self._guard_mode.clear()
        self.monitor = ProtocolMonitor()
        return self.add_node(entries, node_id, primary=True)

    def add_node(self, entries: Iterable, node_id: int = 1,
                 primary: bool = False) -> int:
        node = max(1, min(127, int(node_id)))
        elist = list(entries or ())
        em = _entry_map(elist)
        self._node_entries[node] = em
        if primary or not self._entries:
            self._entries = elist
            self.node_id = node
            self._em = em
        layouts = build_pdo_layouts(elist, node)
        self._layouts.update(layouts)
        self.monitor.load_from_entries(elist, node)
        return len(layouts)

    def layout_count(self) -> int:
        return len(self._layouts)

    def node_count(self) -> int:
        return len(self._node_entries) or (1 if self._layouts or self._em else 0)

    def _em_for(self, node: Optional[int]) -> Dict:
        if node is not None and node in self._node_entries:
            return self._node_entries[node]
        return self._em

    def _clear_transfers(self, nid: int) -> None:
        self._seg.pop((nid, "up"), None)
        self._seg.pop((nid, "dn"), None)
        self._block.pop(nid, None)

    def _decode_block(self, kind: str, nid: int, em: Dict,
                      data: bytes) -> Optional[Tuple[str, List[DecodedValue], str]]:
        """Handle block-mode frames; return None if not block traffic."""
        cmd = data[0]
        cs = (cmd >> 5) & 0x07
        blk = self._block.get(nid)
        seq = cmd & 0x7F

        # Data-phase segments: first byte is c|seqno (1..127), not ccs/scs
        if blk is not None and blk.phase == "data" and 1 <= seq <= 127:
            # End/initiate still use cs 5/6 with ss bit — don't steal those
            if cs in (5, 6):
                pass  # fall through to end/init handlers
            else:
                last_in_block = bool(cmd & 0x80)
                chunk = bytes(data[1:8])
                detail = ""
                if seq != blk.expect_seq:
                    detail = "seq gap expect %d got %d" % (blk.expect_seq, seq)
                blk.buf.extend(chunk)
                if last_in_block:
                    blk.expect_seq = 1
                    return ("SDO block %s end-of-block (%d bytes)" % (
                        blk.name, len(blk.buf)), [], detail)
                blk.expect_seq = seq + 1
                return ("SDO block %s seq=%d (%d bytes)" % (
                    blk.name, seq, len(blk.buf)), [], detail)

        if cs not in (5, 6):
            return None

        index = data[1] | (data[2] << 8) if len(data) >= 3 else 0
        sub = data[3] if len(data) >= 4 else 0
        name = _entry_name(em, index, sub) or ("0x%04X:%02X" % (index, sub))

        # End block (ss/cs bit0 = 1)
        if cmd & 0x01 and blk is not None:
            n = (cmd >> 2) & 0x07
            blk.last_unused = n
            # Server end-upload (scs=6) or server end-download response (scs=5):
            # finalize when we have payload. Client end-download (ccs=6 on RSDO)
            # only announces; wait for server scs=5 ss=1 unless already ending upload.
            finalize = False
            if cs == 5:
                finalize = True
            elif cs == 6 and (blk.direction == "upload" or kind == "TSDO"):
                finalize = True
            elif cs == 6 and kind == "RSDO":
                return ("SDO block end %s (n=%d)" % (blk.name, n), [], "")
            if finalize:
                raw = bytes(blk.buf)
                if n and len(raw) >= n:
                    raw = raw[:len(raw) - n]
                summary, values = _finalize_sdo_value(
                    em, blk.index, blk.subindex, blk.name, raw)
                verb = "read" if blk.direction == "upload" else "write"
                summary = summary.replace("SDO ", "SDO %s " % verb, 1)
                if verb == "write":
                    summary = summary.replace(" = ", " ← ", 1)
                self._block.pop(nid, None)
                return (summary + " [block]", values,
                        values[0].extra if values else "")

        # Initiate block download (client) / upload response (server): ccs/scs=6
        if cs == 6 and not (cmd & 0x01):
            size = 0
            if (cmd & 0x02) and len(data) >= 8:
                size = int.from_bytes(data[4:8], "little")
            elif len(data) >= 8 and any(data[4:8]):
                size = int.from_bytes(data[4:8], "little")
            if kind == "TSDO":
                self._block[nid] = _BlockSdo(
                    index=index, subindex=sub, name=name,
                    direction="upload", size=size, expect_seq=1, phase="data")
                return ("SDO block read initiate %s (%d bytes)" % (name, size),
                        [], "")
            self._block[nid] = _BlockSdo(
                index=index, subindex=sub, name=name,
                direction="download", size=size, expect_seq=1, phase="init")
            return ("SDO block write initiate %s (%d bytes)" % (name, size),
                    [], "")

        # Initiate block upload request (client) / download ACK (server): cs=5
        if cs == 5 and not (cmd & 0x01):
            blksize = data[4] if len(data) >= 5 else 127
            if kind == "RSDO":
                self._block[nid] = _BlockSdo(
                    index=index, subindex=sub, name=name,
                    direction="upload", blksize=blksize or 127,
                    expect_seq=1, phase="init")
                return ("SDO block read request %s (blk=%d)" % (name, blksize),
                        [], "")
            if nid in self._block:
                self._block[nid].blksize = blksize or 127
                self._block[nid].phase = "data"
                return ("SDO block write ACK %s (blk=%d)" % (
                    self._block[nid].name, blksize), [], "")
            self._block[nid] = _BlockSdo(
                index=index, subindex=sub, name=name,
                direction="download", blksize=blksize or 127,
                expect_seq=1, phase="data")
            return ("SDO block write ACK %s (blk=%d)" % (name, blksize), [], "")

        return ("SDO block %s (cmd 0x%02X)" % (name, cmd), [], "")

    def _decode_sdo(self, kind: str, node: Optional[int],
                    data: bytes) -> Tuple[str, List[DecodedValue], str]:
        em = self._em_for(node)
        nid = int(node or 0)
        if len(data) < 1:
            return ("SDO (empty)", [], "")
        cmd = data[0]
        cs = (cmd >> 5) & 0x07

        if cmd == 0x80 and len(data) >= 8:
            index = data[1] | (data[2] << 8)
            sub = data[3]
            name = _entry_name(em, index, sub) or ("0x%04X:%02X" % (index, sub))
            abort = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24)
            msg = sdo_abort_text(abort)
            self._clear_transfers(nid)
            return ("SDO abort %s → %s" % (name, msg), [],
                    "0x%08X %s" % (abort, msg))

        # Block mode (before generic cs handlers — segments steal seq space)
        block_res = self._decode_block(kind, nid, em, data)
        if block_res is not None:
            return block_res

        index = data[1] | (data[2] << 8) if len(data) >= 3 else 0
        sub = data[3] if len(data) >= 4 else 0
        name = _entry_name(em, index, sub) or ("0x%04X:%02X" % (index, sub))

        if cs == 0x02:
            if (cmd & 0xE0) == 0x40 and not (cmd & 0x02) and (cmd & 0x01) == 0:
                if kind == "RSDO":
                    return ("SDO read request %s" % name, [], "")
            if cmd & 0x02:
                n = (cmd >> 2) & 0x03
                size = 4 - n
                raw_b = bytes(data[4:4 + size]) if len(data) >= 4 + size else b""
                summary, values = _finalize_sdo_value(em, index, sub, name, raw_b)
                return (summary.replace("SDO ", "SDO read ", 1), values,
                        values[0].extra if values else "")
            size = 0
            if cmd & 0x01 and len(data) >= 8:
                size = int.from_bytes(data[4:8], "little")
            self._seg[(nid, "up")] = _SegSdo(
                index=index, subindex=sub, name=name,
                direction="upload", size=size, toggle=0)
            return ("SDO read initiate %s (%d bytes)" % (name, size), [], "")

        if cs == 0x01:
            if cmd & 0x02:
                n = (cmd >> 2) & 0x03
                size = 4 - n
                raw_b = bytes(data[4:4 + size]) if len(data) >= 4 + size else b""
                summary, values = _finalize_sdo_value(em, index, sub, name, raw_b)
                return (summary.replace("SDO ", "SDO write ", 1).replace(" = ", " ← ", 1),
                        values, values[0].extra if values else "")
            size = 0
            if cmd & 0x01 and len(data) >= 8:
                size = int.from_bytes(data[4:8], "little")
            self._seg[(nid, "dn")] = _SegSdo(
                index=index, subindex=sub, name=name,
                direction="download", size=size, toggle=0)
            return ("SDO write initiate %s (%d bytes)" % (name, size), [], "")

        if cs == 0x03:
            return ("SDO write ACK %s" % name, [], "")

        if cs == 0x00:
            last = bool(cmd & 0x01)
            unused = (cmd >> 1) & 0x07
            toggle = (cmd >> 4) & 0x01
            key_dn, key_up = (nid, "dn"), (nid, "up")
            if key_dn in self._seg:
                key, seg = key_dn, self._seg[key_dn]
            elif key_up in self._seg:
                key, seg = key_up, self._seg[key_up]
            else:
                key, seg = key_up, None
            n_data = max(0, 7 - unused)
            chunk = bytes(data[1:1 + n_data])
            if seg is None:
                return ("SDO segment t=%d%s (%d bytes)" % (
                    toggle, " last" if last else "", len(chunk)), [], "")
            anomaly = ""
            if seg.buf and toggle != seg.toggle:
                anomaly = "toggle mismatch"
            seg.buf.extend(chunk)
            seg.toggle = 1 - toggle
            if last:
                summary, values = _finalize_sdo_value(
                    em, seg.index, seg.subindex, seg.name, bytes(seg.buf))
                verb = "read" if seg.direction == "upload" else "write"
                summary = summary.replace("SDO ", "SDO %s " % verb, 1)
                if verb == "write":
                    summary = summary.replace(" = ", " ← ", 1)
                self._seg.pop(key, None)
                return (summary + " [segmented]", values, anomaly or (
                    values[0].extra if values else ""))
            return ("SDO segment %s t=%d (%d/%s)" % (
                seg.name, toggle, len(seg.buf),
                str(seg.size) if seg.size else "?"), [], anomaly)

        return ("SDO %s (cmd 0x%02X)" % (name, cmd), [], "")

    def _decode_heartbeat(self, node: Optional[int],
                          payload: bytes) -> Interaction:
        st = payload[0] if payload else 0
        state = st & 0x7F
        toggle = (st >> 7) & 0x01
        nid = int(node or 0)
        state_txt = _HB_STATE.get(state, "0x%02X" % state)
        anomalies: List[str] = []

        mode = self._guard_mode.get(nid)
        if nid:
            prev = self._guard_toggle.get(nid)
            if prev is None:
                self._guard_toggle[nid] = toggle
                # First sample: bit7=1 strongly suggests guarding already mid-stream
                if toggle == 1:
                    mode = "guarding"
                    self._guard_mode[nid] = mode
            else:
                if toggle != prev:
                    mode = "guarding"
                    self._guard_mode[nid] = mode
                elif mode is None and toggle == 0:
                    # Stable bit7=0 after ≥1 prior sample → heartbeat
                    mode = "heartbeat"
                    self._guard_mode[nid] = mode
                if mode == "guarding" and toggle == prev and prev is not None:
                    anomalies.append("guard toggle stuck")
                self._guard_toggle[nid] = toggle

        if mode == "guarding":
            summary = "Node Guarding N%d → %s (t=%d)" % (nid, state_txt, toggle)
            kind = "GUARD"
        elif mode == "heartbeat":
            summary = "Heartbeat N%d → %s" % (nid, state_txt)
            kind = "HB"
        else:
            summary = "HB/Guard N%d → %s" % (nid, state_txt)
            kind = "HB"
        return Interaction(
            kind, node, summary, layer="protocol", anomalies=anomalies)

    def decode(self, can_id: int, data: bytes,
               ts: Optional[float] = None) -> Interaction:
        kind, node, label = classify_cob(can_id)
        payload = bytes(data or b"")
        cob = int(can_id) & 0x7FF
        now = float(ts) if ts is not None else self._last_ts
        self._last_ts = now

        layout = self._layouts.get(cob)
        if layout is not None and layout.objects:
            vals = decode_pdo_payload(layout, payload)
            parts = [
                "%s=%s" % (v.name or ("0x%04X" % v.index), v.text) for v in vals]
            summary = "%s N%d: %s" % (
                layout.kind, layout.node_id,
                ", ".join(parts) if parts else "(empty map)")
            detail_parts = []
            for v in vals:
                line = "%s=%s" % (v.display(), v.text)
                if v.extra:
                    line += " · " + v.extra
                detail_parts.append(line)
            ix = Interaction(
                kind=layout.kind, node_id=layout.node_id,
                summary=summary, detail="; ".join(detail_parts),
                values=vals, layer="application")
            ix.anomalies = self.monitor.observe(now, ix, payload)
            return ix

        if kind == "NMT" and payload:
            cmd = payload[0]
            tgt = payload[1] if len(payload) > 1 else 0
            who = "all" if tgt == 0 else ("node %d" % tgt)
            summary = "NMT %s → %s" % (_NMT_CMD.get(cmd, "0x%02X" % cmd), who)
            ix = Interaction(kind, None, summary, layer="protocol")
            ix.anomalies = self.monitor.observe(now, ix, payload)
            return ix

        if kind == "HB" and payload:
            ix = self._decode_heartbeat(node, payload)
            ix.anomalies = list(ix.anomalies) + self.monitor.observe(now, ix, payload)
            return ix

        if kind == "EMCY" and payload:
            code = payload[0] | (payload[1] << 8) if len(payload) >= 2 else 0
            reg = payload[2] if len(payload) >= 3 else 0
            err_txt = emcy_error_text(code)
            reg_txt = error_register_text(reg)
            mfg = ""
            if len(payload) > 3:
                mfg = " mfg=" + " ".join("%02X" % b for b in payload[3:8])
            summary = "EMCY N%d: %s [%s]%s" % (
                node or 0, err_txt, reg_txt, mfg)
            detail = "code=0x%04X register=0x%02X (%s)" % (code, reg, reg_txt)
            ix = Interaction(
                kind, node, summary, detail=detail, layer="application")
            ix.anomalies = self.monitor.observe(now, ix, payload)
            return ix

        if kind == "SYNC":
            ix = Interaction(
                kind, None,
                "SYNC counter=%d" % payload[0] if payload else "SYNC",
                layer="protocol")
            ix.anomalies = self.monitor.observe(now, ix, payload)
            return ix

        if kind == "TIME":
            ix = Interaction(
                kind, None, _decode_time_payload(payload), layer="protocol")
            return ix

        if kind == "LSS_M":
            return Interaction(
                kind, None, _decode_lss(payload, master=True), layer="protocol")
        if kind == "LSS_S":
            return Interaction(
                kind, None, _decode_lss(payload, master=False), layer="protocol")

        if kind in ("TSDO", "RSDO"):
            summary, values, detail = self._decode_sdo(kind, node, payload)
            prefix = "TSDO" if kind == "TSDO" else "RSDO"
            if node is not None:
                summary = "%s N%d · %s" % (prefix, node, summary)
            anomalies = []
            if detail == "toggle mismatch":
                anomalies.append("SDO toggle mismatch")
            elif detail.startswith("seq gap"):
                anomalies.append(detail)
            ix = Interaction(
                kind, node, summary, detail=detail, values=values,
                layer="application" if values or "abort" in summary.lower()
                or "block" in summary.lower() else "protocol",
                anomalies=anomalies)
            ix.anomalies = list(ix.anomalies) + self.monitor.observe(now, ix, payload)
            return ix

        if kind.startswith("TPDO") or kind.startswith("RPDO"):
            return Interaction(
                kind, node,
                "%s N%d (%d bytes)" % (kind, node or 0, len(payload)),
                detail="Load EDS with PDO mapping to decode objects",
                layer="protocol")

        return Interaction(
            kind, node, label or ("0x%03X" % cob), layer="protocol")
