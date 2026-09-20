# -*- coding: utf-8 -*-
"""dbcparse.py — 自包含 DBC 解析/编码/序列化（纯 Python）

供插件离线使用：不依赖宿主 RPC，逐帧解码性能可控。
- 解析：BO_ / SG_ / BU_ / CM_ / VAL_ / BA_(GenMsgCycleTime) / BA_DEF_
- 解码：Intel（小端）与 Motorola（大端）位抽取，factor/offset 物理值
- 编码：物理值 → 原始位 → 数据字节
- 序列化：回写标准 DBC 文本（供合并/另存）

容错：无法识别的行跳过并记录 warnings（可被 lint 类工具复用）。
编码：文件按 UTF-8 读取，失败回退 GBK（国产工具常见）。
"""

import re

_EXT_FLAG = 0x80000000
_ID_MASK = 0x1FFFFFFF

_TOKEN_RE = re.compile(r'"(?:[^"\\]|\\.)*"|[^\s]+')

_DBC_SG = re.compile(
    r'^\s*SG_\s+(\S+)\s*'
    r'(?:(M|m\d+))?\s*:\s*'
    r'(\d+)\|(\d+)@([01])([+-])\s*'
    r'\(([^,]+),([^)]+)\)\s*'
    r'\[([^|]*)\|([^\]]*)\]\s*'
    r'"([^"]*)"\s*(.*)$'
)


class Signal:
    __slots__ = ("name", "start_bit", "bit_length", "little_endian", "is_signed",
                 "factor", "offset", "minimum", "maximum", "unit", "receivers",
                 "comment", "value_table", "mux_type", "mux_value", "attributes")

    def __init__(self):
        self.name = ""
        self.start_bit = 0
        self.bit_length = 0
        self.little_endian = True
        self.is_signed = False
        self.factor = 1.0
        self.offset = 0.0
        self.minimum = 0.0
        self.maximum = 0.0
        self.unit = ""
        self.receivers = []
        self.comment = ""
        self.value_table = {}
        self.mux_type = ""      # '' / 'multiplexor' / 'multiplexed'
        self.mux_value = None
        self.attributes = {}    # BA_ SG_ name -> value (str)

    def raw_to_phys(self, raw):
        return raw * self.factor + self.offset

    def phys_to_raw(self, phys):
        if self.factor == 0:
            return 0
        return int(round((float(phys) - self.offset) / self.factor))

    def phys_to_raw_float(self, phys):
        if self.factor == 0:
            return 0.0
        return (float(phys) - self.offset) / self.factor


class Message:
    __slots__ = ("can_id", "extended", "name", "dlc", "sender", "signals",
                 "cycle_time", "comment", "attributes")

    def __init__(self):
        self.can_id = 0
        self.extended = False
        self.name = ""
        self.dlc = 8
        self.sender = ""
        self.signals = []
        self.cycle_time = 0
        self.comment = ""
        self.attributes = {}    # BA_ BO_ name -> value (str)

    def signal(self, name):
        for s in self.signals:
            if s.name == name:
                return s
        return None


class DbcFile:
    def __init__(self):
        self.version = ""
        self.nodes = []
        self.messages = {}       # can_id(已去扩展位) -> Message（保持插入序）
        self.warnings = []
        self.path = ""

    def message_by_name(self, name):
        for m in self.messages.values():
            if m.name == name:
                return m
        return None


# ------------------------------------------------------------
#  位抽取 / 写入
# ------------------------------------------------------------

def signal_bit_numbers(start, length, little_endian):
    """Vector / CANdb++ bit numbers. Bit 0 is the LSB of byte 0.

    Intel: start is the LSB, then start+1 ...
    Motorola: start is the MSB, then toward lower bits, wrapping to the next byte's bit 7.
    Motorola list is MSB-first. Intel list is LSB-first.
    """
    start = int(start)
    length = int(length)
    if length <= 0:
        return []
    if little_endian:
        return [start + i for i in range(length)]
    bits = []
    bit = start
    for _ in range(length):
        bits.append(bit)
        if (bit & 7) == 0:
            bit += 15
        else:
            bit -= 1
    return bits


def extract_intel(data, start, length):
    """Intel: start is the LSB (CANdb++). Bit 0 of a byte is its least significant bit."""
    value = 0
    n = len(data) * 8
    for i in range(length):
        bit = start + i
        if bit >= n:
            return None
        if (data[bit >> 3] >> (bit & 7)) & 1:
            value |= 1 << i
    return value


def extract_motorola(data, start, length):
    """Motorola: start is the MSB in the same bit numbering as Intel."""
    value = 0
    n = len(data) * 8
    for bit in signal_bit_numbers(start, length, False):
        if bit >= n or bit < 0:
            return None
        value = (value << 1) | ((data[bit >> 3] >> (bit & 7)) & 1)
    return value


def insert_intel(buf, start, length, value):
    for i in range(length):
        if (value >> i) & 1:
            bit = start + i
            if 0 <= bit < len(buf) * 8:
                buf[bit >> 3] |= 1 << (bit & 7)


def insert_motorola(buf, start, length, value):
    bits = signal_bit_numbers(start, length, False)
    for i, bit in enumerate(bits):
        if (value >> (length - 1 - i)) & 1:
            if 0 <= bit < len(buf) * 8:
                buf[bit >> 3] |= 1 << (bit & 7)


def signal_raw(sig, data):
    if sig.little_endian:
        return extract_intel(data, sig.start_bit, sig.bit_length)
    return extract_motorola(data, sig.start_bit, sig.bit_length)


def signal_phys(sig, data):
    raw = signal_raw(sig, data)
    if raw is None:
        return None
    if sig.is_signed and sig.bit_length > 1:
        half = 1 << (sig.bit_length - 1)
        if raw >= half:
            raw -= 1 << sig.bit_length
    return sig.raw_to_phys(raw)


def decode_message(msg, data):
    """返回 OrderedDict：信号名 → 物理值（越界/长度不足的信号跳过）"""
    out = {}
    for sig in msg.signals:
        v = signal_phys(sig, data)
        if v is not None:
            out[sig.name] = v
    return out


def encode_message(msg, values, dlc=None):
    """values: {信号名: 物理值}；返回 bytearray（缺省信号填 0）"""
    length = dlc if dlc else max(msg.dlc, 8)
    buf = bytearray(length)
    for sig in msg.signals:
        if sig.name not in values:
            continue
        raw = sig.phys_to_raw(values[sig.name])
        mask = (1 << sig.bit_length) - 1
        raw &= mask
        if sig.little_endian:
            insert_intel(buf, sig.start_bit, sig.bit_length, raw)
        else:
            insert_motorola(buf, sig.start_bit, sig.bit_length, raw)
    return buf


# ------------------------------------------------------------
#  解析
# ------------------------------------------------------------

def _tokens(line):
    return _TOKEN_RE.findall(line)


def _unquote(tok):
    if tok.startswith('"') and tok.endswith('"') and len(tok) >= 2:
        return tok[1:-1].replace('\\"', '"')
    return tok


def _num(text, default=0.0):
    try:
        return float(text)
    except (TypeError, ValueError):
        return default


def parse_file(path):
    db = DbcFile()
    db.path = path
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except UnicodeDecodeError:
        with open(path, "r", encoding="gbk", errors="replace") as f:
            text = f.read()

    cur_msg = None
    send_type_names = {}

    for lineno, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped:
            continue
        try:
            if stripped.startswith("VERSION "):
                toks = _tokens(stripped)
                if len(toks) >= 2:
                    db.version = _unquote(toks[1])
            elif stripped.startswith("BU_:"):
                toks = _tokens(stripped)
                db.nodes = [t for t in toks[1:] if not t.endswith(":")]
            elif stripped.startswith("BO_ "):
                toks = _tokens(stripped)
                # BO_ <id> <name>: <dlc> <sender>
                if len(toks) >= 5:
                    raw_id = int(toks[1])
                    m = Message()
                    m.can_id = raw_id & _ID_MASK
                    m.extended = bool(raw_id & _EXT_FLAG)
                    m.name = toks[2].rstrip(":")
                    m.dlc = int(toks[3])
                    m.sender = toks[4]
                    db.messages[m.can_id] = m
                    cur_msg = m
                else:
                    cur_msg = None
            elif stripped.startswith("SG_ ") and cur_msg is not None:
                m = _DBC_SG.match(line)
                if not m:
                    db.warnings.append("%d: 无法解析 SG_ 行: %s" % (lineno, stripped[:60]))
                    continue
                sig = Signal()
                sig.name = m.group(1)
                mux = m.group(2)
                if mux == "M":
                    sig.mux_type = "multiplexor"
                elif mux and mux.startswith("m"):
                    sig.mux_type = "multiplexed"
                    try:
                        sig.mux_value = int(mux[1:])
                    except ValueError:
                        sig.mux_value = None
                sig.start_bit = int(m.group(3))
                sig.bit_length = int(m.group(4))
                sig.little_endian = m.group(5) == "1"
                sig.is_signed = m.group(6) == "-"
                sig.factor = _num(m.group(7), 1.0)
                sig.offset = _num(m.group(8), 0.0)
                sig.minimum = _num(m.group(9))
                sig.maximum = _num(m.group(10))
                sig.unit = m.group(11) or ""
                sig.receivers = [r for r in (m.group(12) or "").split(",") if r]
                cur_msg.signals.append(sig)
            elif stripped.startswith("CM_ "):
                toks = _tokens(stripped)
                # CM_ SG_ <id> <sig> "text"; / CM_ BO_ <id> "text";
                if len(toks) >= 4 and toks[1] == "SG_":
                    mid = int(toks[2]) & _ID_MASK
                    sig_name = toks[3]
                    msg = db.messages.get(mid)
                    if msg:
                        sig = msg.signal(sig_name)
                        if sig:
                            sig.comment = _unquote(toks[4]) if len(toks) > 4 else ""
                elif len(toks) >= 3 and toks[1] == "BO_":
                    mid = int(toks[2]) & _ID_MASK
                    msg = db.messages.get(mid)
                    if msg:
                        msg.comment = _unquote(toks[3]) if len(toks) > 3 else ""
            elif stripped.startswith("VAL_ "):
                toks = _tokens(stripped)
                # VAL_ <id> <sig> <num> "desc" ... ;
                if len(toks) >= 4:
                    mid = int(toks[1]) & _ID_MASK
                    sig_name = toks[2]
                    msg = db.messages.get(mid)
                    sig = msg.signal(sig_name) if msg else None
                    if sig:
                        i = 3
                        while i + 1 < len(toks):
                            if toks[i] == ";":
                                break
                            try:
                                val = int(toks[i])
                            except ValueError:
                                break
                            sig.value_table[val] = _unquote(toks[i + 1])
                            i += 2
            elif stripped.startswith("BA_DEF_"):
                # BA_DEF_ BO_ "GenMsgSendType" ENUM "Cyclic","Event",...;
                m = re.match(r'^BA_DEF_\s+(?:BO_\s+)?"([^"]+)"\s+ENUM\s+(.*);\s*$', stripped)
                if m and m.group(1) == "GenMsgSendType":
                    send_type_names = [_unquote(t) for t in
                                       re.findall(r'"(?:[^"\\]|\\.)*"', m.group(2))]
            elif stripped.startswith("BA_ "):
                m_sg = re.match(
                    r'^BA_\s+"([^"]+)"\s+SG_\s+(\d+)\s+(\S+)\s+(.+);\s*$', stripped)
                m_bo = re.match(
                    r'^BA_\s+"([^"]+)"\s+BO_\s+(\d+)\s+(.+);\s*$', stripped)
                if m_sg:
                    attr, mid_s, sig_name, val_s = (
                        m_sg.group(1), m_sg.group(2), m_sg.group(3), m_sg.group(4).strip())
                    msg = db.messages.get(int(mid_s) & _ID_MASK)
                    sig = msg.signal(sig_name) if msg else None
                    if sig is not None:
                        sig.attributes[attr] = _unquote(val_s) if val_s.startswith('"') else val_s
                elif m_bo:
                    attr, mid_s, val_s = m_bo.group(1), m_bo.group(2), m_bo.group(3).strip()
                    msg = db.messages.get(int(mid_s) & _ID_MASK)
                    if not msg:
                        continue
                    stored = _unquote(val_s) if val_s.startswith('"') else val_s
                    if attr == "GenMsgSendType" and send_type_names:
                        try:
                            idx = int(val_s)
                            if 0 <= idx < len(send_type_names):
                                stored = send_type_names[idx]
                        except ValueError:
                            pass
                    msg.attributes[attr] = stored
                    if attr == "GenMsgCycleTime":
                        try:
                            msg.cycle_time = int(float(val_s))
                        except ValueError:
                            pass
        except Exception as exc:  # per-line tolerance
            db.warnings.append("%d: %s (%s)" % (lineno, stripped[:60], exc))
    return db


# ------------------------------------------------------------
#  序列化（供合并另存）
# ------------------------------------------------------------

def _fmt_num(v):
    if v is None:
        return "0"
    if isinstance(v, float) and v == int(v):
        return str(int(v))
    return repr(v)


def serialize(db):
    lines = ['VERSION ""', "", "NS_ :", "", "BS_:", ""]
    nodes = list(db.nodes)
    for m in db.messages.values():
        if m.sender and m.sender not in nodes:
            nodes.append(m.sender)
        for s in m.signals:
            for r in s.receivers:
                if r not in nodes:
                    nodes.append(r)
    lines.append("BU_: " + " ".join(nodes) if nodes else "BU_:")
    lines.append("")

    for m in db.messages.values():
        raw_id = m.can_id | (_EXT_FLAG if m.extended else 0)
        lines.append("BO_ %d %s: %d %s" % (raw_id, m.name, m.dlc, m.sender or "Vector__XXX"))
        for s in m.signals:
            mux = ""
            if s.mux_type == "multiplexor":
                mux = " M"
            elif s.mux_type == "multiplexed" and s.mux_value is not None:
                mux = " m%d" % s.mux_value
            order = "1" if s.little_endian else "0"
            sign = "-" if s.is_signed else "+"
            lines.append(
                " SG_ %s%s : %d|%d@%s%s (%s,%s) [%s|%s] \"%s\" %s"
                % (s.name, mux, s.start_bit, s.bit_length, order, sign,
                   _fmt_num(s.factor), _fmt_num(s.offset),
                   _fmt_num(s.minimum), _fmt_num(s.maximum), s.unit,
                   ",".join(s.receivers) or "Vector__XXX"))
        lines.append("")

    for m in db.messages.values():
        if m.comment:
            lines.append('CM_ BO_ %d "%s";' % (m.can_id, m.comment.replace('"', '\\"')))
        for s in m.signals:
            if s.comment:
                lines.append('CM_ SG_ %d %s "%s";'
                             % (m.can_id, s.name, s.comment.replace('"', '\\"')))
    for m in db.messages.values():
        for s in m.signals:
            if s.value_table:
                pairs = " ".join('%d "%s"' % (v, d.replace('"', '\\"'))
                                 for v, d in sorted(s.value_table.items()))
                lines.append("VAL_ %d %s %s ;" % (m.can_id, s.name, pairs))

    lines.append('')
    lines.append('BA_DEF_ BO_ "GenMsgCycleTime" INT 0 65535;')
    lines.append('BA_DEF_ BO_ "GenMsgSendType" ENUM "Cyclic","Event","NotUsed";')
    for m in db.messages.values():
        if m.cycle_time:
            lines.append('BA_ "GenMsgCycleTime" BO_ %d %d;' % (m.can_id, m.cycle_time))
    lines.append("")
    return "\n".join(lines)
