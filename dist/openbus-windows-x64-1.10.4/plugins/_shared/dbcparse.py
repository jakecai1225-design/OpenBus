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
                 "comment", "value_table", "value_table_name", "mux_type",
                 "mux_value", "attributes")

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
        self.value_table_name = ""
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


class AttrDef:
    """BA_DEF_ entry (CANdb++ user-defined attribute)."""

    __slots__ = ("name", "object_type", "value_type", "minimum", "maximum",
                 "enum_values", "default")

    def __init__(self, name="", object_type="", value_type="STRING"):
        self.name = name
        # "" network | BO_ | SG_ | BU_ | EV_ | BU_SG_REL_ | …
        self.object_type = object_type
        self.value_type = value_type    # INT FLOAT STRING ENUM HEX
        self.minimum = None
        self.maximum = None
        self.enum_values = []
        self.default = None


class DbcFile:
    def __init__(self):
        self.version = ""
        self.nodes = []
        self.messages = {}       # can_id -> Message
        self.warnings = []
        self.path = ""
        self.attribute_defs = []           # list[AttrDef]
        self.network_attributes = {}       # BA_ "name" value;
        self.node_attributes = {}          # node -> {attr: value}
        self.value_tables = {}             # name -> {int: str}

    def message_by_name(self, name):
        for m in self.messages.values():
            if m.name == name:
                return m
        return None

    def attr_def(self, name):
        for d in self.attribute_defs:
            if d.name == name:
                return d
        return None

    def ensure_default_attr_defs(self):
        if not self.attr_def("GenMsgCycleTime"):
            d = AttrDef("GenMsgCycleTime", "BO_", "INT")
            d.minimum, d.maximum, d.default = 0, 65535, 0
            self.attribute_defs.append(d)
        if not self.attr_def("GenMsgSendType"):
            d = AttrDef("GenMsgSendType", "BO_", "ENUM")
            d.enum_values = ["Cyclic", "Event", "NotUsed"]
            d.default = "Cyclic"
            self.attribute_defs.append(d)
        # Signal-scope Gen* so Attributes Values is never empty for a signal
        # when the DBC only ships message-level BA_DEF_ (common OEM strip).
        if not self.attr_def("GenSigStartValue"):
            d = AttrDef("GenSigStartValue", "SG_", "INT")
            d.minimum, d.maximum, d.default = 0, 65535, 0
            self.attribute_defs.append(d)
        if not self.attr_def("GenSigSendType"):
            d = AttrDef("GenSigSendType", "SG_", "ENUM")
            d.enum_values = [
                "Cyclic", "OnChange", "OnWrite", "IfActive",
                "OnChangeWithRepetition", "OnWriteWithRepetition",
                "NoSigSendType"]
            d.default = "Cyclic"
            self.attribute_defs.append(d)

    def sync_value_tables_from_signals(self):
        """Promote signal inline VAL_ maps into `value_tables` for the catalog UI.

        Most Vector DBCs only emit `VAL_ <id> <sig> 0 "A" 1 "B" ;` without a
        prior `VAL_TABLE_`. Without this sync the Value tables page stays empty
        even though Messages shows those encodings on each signal.
        """
        by_content = {}
        for name, table in self.value_tables.items():
            by_content[_value_table_key(table)] = name
        for msg in self.messages.values():
            for sig in msg.signals:
                if not sig.value_table:
                    continue
                named = (sig.value_table_name or '').strip()
                if named and named in self.value_tables:
                    if not self.value_tables[named]:
                        self.value_tables[named] = dict(sig.value_table)
                    continue
                key = _value_table_key(sig.value_table)
                if key in by_content:
                    sig.value_table_name = by_content[key]
                    continue
                base = 'VT_%s_%s' % (
                    _safe_vt_token(msg.name), _safe_vt_token(sig.name))
                name = base
                n = 2
                while name in self.value_tables:
                    name = '%s_%d' % (base, n)
                    n += 1
                self.value_tables[name] = dict(sig.value_table)
                sig.value_table_name = name
                by_content[key] = name


def _value_table_key(table):
    if not table:
        return ()
    return tuple(sorted((int(k), str(v)) for k, v in table.items()))


def _safe_vt_token(text):
    raw = re.sub(r'[^A-Za-z0-9_]+', '_', (text or '').strip())
    raw = raw.strip('_') or 'X'
    return raw[:40]


# ------------------------------------------------------------
#  Bit extract / write
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
                    db.warnings.append("%d: cannot parse SG_ line: %s" % (lineno, stripped[:60]))
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
            elif stripped.startswith("VAL_TABLE_"):
                toks = _tokens(stripped)
                # VAL_TABLE_ <name> <num> "desc" ... ;
                if len(toks) >= 2:
                    tname = toks[1]
                    table = {}
                    i = 2
                    while i + 1 < len(toks):
                        if toks[i] == ";":
                            break
                        try:
                            val = int(toks[i], 0)
                        except ValueError:
                            break
                        table[val] = _unquote(toks[i + 1])
                        i += 2
                    db.value_tables[tname] = table
            elif stripped.startswith("VAL_ "):
                toks = _tokens(stripped)
                # VAL_ <id> <sig> <num> "desc" ... ;
                if len(toks) >= 4:
                    mid = int(toks[1], 0) & _ID_MASK
                    sig_name = toks[2]
                    msg = db.messages.get(mid)
                    sig = msg.signal(sig_name) if msg else None
                    if sig:
                        i = 3
                        # Named table reference only (rare): VAL_ id sig TableName ;
                        if (i + 1 < len(toks)
                                and toks[i] != ";"
                                and not toks[i].lstrip("-").isdigit()
                                and not toks[i].startswith("0x")
                                and (i + 1 >= len(toks) or toks[i + 1] == ";")):
                            tname = toks[i]
                            if tname in db.value_tables:
                                sig.value_table = dict(db.value_tables[tname])
                                sig.value_table_name = tname
                        else:
                            while i + 1 < len(toks):
                                if toks[i] == ";":
                                    break
                                try:
                                    val = int(toks[i], 0)
                                except ValueError:
                                    break
                                sig.value_table[val] = _unquote(toks[i + 1])
                                i += 2
            elif stripped.startswith("BA_DEF_DEF_"):
                # BA_DEF_DEF_ "name" <default>;
                m = re.match(
                    r'^BA_DEF_DEF_\s+"([^"]+)"\s+(.+);\s*$', stripped)
                if m:
                    name, raw = m.group(1), m.group(2).strip()
                    d = db.attr_def(name)
                    if d is None:
                        d = AttrDef(name)
                        db.attribute_defs.append(d)
                    d.default = _unquote(raw) if raw.startswith('"') else raw
            elif stripped.startswith("BA_DEF_"):
                # BA_DEF_ [object] "name" TYPE ...;
                # object may be empty (network), BO_/SG_/BU_, or rare
                # EV_ / BU_SG_REL_ / BU_BO_REL_ / SG_REL_ tokens.
                obj, name, vtype, rest = "", "", "", ""
                m_obj = re.match(
                    r'^BA_DEF_\s+([A-Za-z_][A-Za-z0-9_]*)\s+"([^"]+)"\s+(\w+)\s*(.*);\s*$',
                    stripped)
                m_net = re.match(
                    r'^BA_DEF_\s+"([^"]+)"\s+(\w+)\s*(.*);\s*$', stripped)
                if m_obj and m_obj.group(1).upper() not in (
                        "INT", "FLOAT", "STRING", "ENUM", "HEX"):
                    obj = m_obj.group(1)
                    name = m_obj.group(2)
                    vtype = m_obj.group(3).upper()
                    rest = (m_obj.group(4) or "").strip()
                elif m_net:
                    name = m_net.group(1)
                    vtype = m_net.group(2).upper()
                    rest = (m_net.group(3) or "").strip()
                if name and vtype:
                    d = db.attr_def(name)
                    if d is None:
                        d = AttrDef(name, obj, vtype)
                        db.attribute_defs.append(d)
                    else:
                        d.object_type = obj or d.object_type
                        d.value_type = vtype or d.value_type
                    if vtype == "ENUM":
                        d.enum_values = [
                            _unquote(t) for t in
                            re.findall(r'"(?:[^"\\]|\\.)*"', rest)]
                    elif vtype in ("INT", "HEX", "FLOAT"):
                        nums = re.findall(
                            r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', rest)
                        if len(nums) >= 2:
                            try:
                                d.minimum = (
                                    float(nums[0]) if vtype == "FLOAT"
                                    else int(float(nums[0])))
                                d.maximum = (
                                    float(nums[1]) if vtype == "FLOAT"
                                    else int(float(nums[1])))
                            except ValueError:
                                pass
            elif stripped.startswith("BA_ "):
                # CAN id may be decimal or 0x-prefixed hex (Vector exports vary).
                m_sg = re.match(
                    r'^BA_\s+"([^"]+)"\s+SG_\s+(0[xX][0-9A-Fa-f]+|\d+)\s+(\S+)\s+(.+);\s*$',
                    stripped)
                m_bo = re.match(
                    r'^BA_\s+"([^"]+)"\s+BO_\s+(0[xX][0-9A-Fa-f]+|\d+)\s+(.+);\s*$',
                    stripped)
                m_bu = re.match(
                    r'^BA_\s+"([^"]+)"\s+BU_\s+(\S+)\s+(.+);\s*$', stripped)
                m_net = re.match(
                    r'^BA_\s+"([^"]+)"\s+(.+);\s*$', stripped)
                if m_sg:
                    attr, mid_s, sig_name, val_s = (
                        m_sg.group(1), m_sg.group(2), m_sg.group(3),
                        m_sg.group(4).strip())
                    try:
                        mid = int(mid_s, 0) & _ID_MASK
                    except ValueError:
                        mid = None
                    msg = db.messages.get(mid) if mid is not None else None
                    sig = msg.signal(sig_name) if msg else None
                    if sig is not None:
                        sig.attributes[attr] = _decode_attr_value(
                            db, attr, val_s)
                elif m_bo:
                    attr, mid_s, val_s = (
                        m_bo.group(1), m_bo.group(2), m_bo.group(3).strip())
                    try:
                        mid = int(mid_s, 0) & _ID_MASK
                    except ValueError:
                        continue
                    msg = db.messages.get(mid)
                    if not msg:
                        continue
                    stored = _decode_attr_value(db, attr, val_s)
                    msg.attributes[attr] = stored
                    if attr == "GenMsgCycleTime":
                        try:
                            msg.cycle_time = int(float(str(stored)))
                        except ValueError:
                            pass
                elif m_bu:
                    attr, node, val_s = (
                        m_bu.group(1), m_bu.group(2), m_bu.group(3).strip())
                    db.node_attributes.setdefault(node, {})[attr] = (
                        _decode_attr_value(db, attr, val_s))
                elif m_net and m_net.group(2).split()[0] not in (
                        "SG_", "BO_", "BU_"):
                    # Network-level: BA_ "name" value; (no object type)
                    attr, val_s = m_net.group(1), m_net.group(2).strip()
                    if not re.match(r'^(SG_|BO_|BU_)\s', val_s):
                        db.network_attributes[attr] = _decode_attr_value(
                            db, attr, val_s)
        except Exception as exc:  # per-line tolerance
            db.warnings.append("%d: %s (%s)" % (lineno, stripped[:60], exc))
    db.ensure_default_attr_defs()
    db.sync_value_tables_from_signals()
    return db


def _decode_attr_value(db, attr_name, val_s):
    raw = val_s.strip()
    d = db.attr_def(attr_name)
    if raw.startswith('"'):
        return _unquote(raw)
    if d and d.value_type == "ENUM" and d.enum_values:
        try:
            idx = int(raw)
            if 0 <= idx < len(d.enum_values):
                return d.enum_values[idx]
        except ValueError:
            pass
    return raw


# ------------------------------------------------------------
#  Serialize (merge / save-as)
# ------------------------------------------------------------

def _fmt_num(v):
    if v is None:
        return "0"
    if isinstance(v, float) and v == int(v):
        return str(int(v))
    return repr(v)


def serialize(db):
    db.ensure_default_attr_defs()
    db.sync_value_tables_from_signals()
    lines = ['VERSION "%s"' % (db.version or "").replace('"', '\\"'),
             "", "NS_ :", "", "BS_:", ""]
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
        lines.append("BO_ %d %s: %d %s" % (
            raw_id, m.name, m.dlc, m.sender or "Vector__XXX"))
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
            lines.append('CM_ BO_ %d "%s";' % (
                m.can_id, m.comment.replace('"', '\\"')))
        for s in m.signals:
            if s.comment:
                lines.append('CM_ SG_ %d %s "%s";' % (
                    m.can_id, s.name, s.comment.replace('"', '\\"')))

    for tname, table in sorted(db.value_tables.items()):
        if not table:
            continue
        pairs = " ".join(
            '%d "%s"' % (v, d.replace('"', '\\"'))
            for v, d in sorted(table.items()))
        lines.append("VAL_TABLE_ %s %s ;" % (tname, pairs))

    for m in db.messages.values():
        for s in m.signals:
            if not s.value_table and not s.value_table_name:
                continue
            # Prefer named catalog reference when linked.
            if (s.value_table_name
                    and s.value_table_name in db.value_tables
                    and db.value_tables[s.value_table_name]):
                lines.append("VAL_ %d %s %s ;" % (
                    m.can_id, s.name, s.value_table_name))
                continue
            if s.value_table:
                pairs = " ".join(
                    '%d "%s"' % (v, d.replace('"', '\\"'))
                    for v, d in sorted(s.value_table.items()))
                lines.append("VAL_ %d %s %s ;" % (m.can_id, s.name, pairs))

    lines.append("")
    for d in db.attribute_defs:
        obj = (d.object_type + " ") if d.object_type else ""
        if d.value_type == "ENUM":
            enums = ",".join('"%s"' % e.replace('"', '\\"') for e in d.enum_values)
            lines.append('BA_DEF_ %s"%s" ENUM %s;' % (obj, d.name, enums))
        elif d.value_type in ("INT", "HEX"):
            lo = 0 if d.minimum is None else int(d.minimum)
            hi = 0 if d.maximum is None else int(d.maximum)
            lines.append('BA_DEF_ %s"%s" %s %d %d;' % (
                obj, d.name, d.value_type, lo, hi))
        elif d.value_type == "FLOAT":
            lo = 0.0 if d.minimum is None else float(d.minimum)
            hi = 0.0 if d.maximum is None else float(d.maximum)
            lines.append('BA_DEF_ %s"%s" FLOAT %s %s;' % (
                obj, d.name, _fmt_num(lo), _fmt_num(hi)))
        else:
            lines.append('BA_DEF_ %s"%s" STRING ;' % (obj, d.name))

    for d in db.attribute_defs:
        if d.default is None:
            continue
        dv = d.default
        if d.value_type == "STRING" or (
                isinstance(dv, str) and not str(dv).lstrip("-").replace(".", "", 1).isdigit()):
            if d.value_type == "ENUM" and dv in (d.enum_values or []):
                lines.append('BA_DEF_DEF_ "%s" "%s";' % (
                    d.name, str(dv).replace('"', '\\"')))
            elif d.value_type == "ENUM":
                lines.append('BA_DEF_DEF_ "%s" "%s";' % (
                    d.name, str(dv).replace('"', '\\"')))
            else:
                lines.append('BA_DEF_DEF_ "%s" "%s";' % (
                    d.name, str(dv).replace('"', '\\"')))
        else:
            lines.append('BA_DEF_DEF_ "%s" %s;' % (d.name, dv))

    for name, val in sorted(db.network_attributes.items()):
        lines.append(_fmt_ba_line(db, name, val))

    for node, attrs in sorted(db.node_attributes.items()):
        for name, val in sorted(attrs.items()):
            lines.append(_fmt_ba_line(db, name, val, "BU_", node))

    for m in db.messages.values():
        attrs = dict(m.attributes or {})
        if m.cycle_time and "GenMsgCycleTime" not in attrs:
            attrs["GenMsgCycleTime"] = m.cycle_time
        for name, val in sorted(attrs.items()):
            lines.append(_fmt_ba_line(db, name, val, "BO_", m.can_id))
        for s in m.signals:
            for name, val in sorted((s.attributes or {}).items()):
                lines.append(_fmt_ba_line(
                    db, name, val, "SG_", m.can_id, s.name))

    lines.append("")
    return "\n".join(lines)


def _fmt_ba_line(db, name, val, obj=None, key=None, sig=None):
    d = db.attr_def(name)
    encoded = val
    if d and d.value_type == "ENUM" and d.enum_values:
        if isinstance(val, str) and val in d.enum_values:
            encoded = d.enum_values.index(val)
        elif isinstance(val, int):
            encoded = val
    if isinstance(encoded, str) and not (
            str(encoded).lstrip("-").replace(".", "", 1).isdigit()):
        lit = '"%s"' % encoded.replace('"', '\\"')
    else:
        lit = str(encoded)
    if obj == "SG_":
        return 'BA_ "%s" SG_ %d %s %s;' % (name, int(key), sig, lit)
    if obj == "BO_":
        return 'BA_ "%s" BO_ %d %s;' % (name, int(key), lit)
    if obj == "BU_":
        return 'BA_ "%s" BU_ %s %s;' % (name, key, lit)
    return 'BA_ "%s" %s;' % (name, lit)
