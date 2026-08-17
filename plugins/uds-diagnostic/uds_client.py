"""uds_client.py — UDS（ISO 14229-1）客户端：请求-响应匹配与超时管理

G10 UDS 插件强化版：
- 单请求-响应事务：P2（默认 2s）超时；收到 NRC 0x78 自动切 P2*（默认 5s）续等
- 功能寻址 / 抑制响应（子功能 bit7）不启动响应定时器
- 服务请求编码器（每服务一个纯函数）与响应解码器（describe_response）
- DTC 解码（3 字节 → P/C/B/U + 编号文本、状态字节位图）
- 安全访问密钥算法：演示异或 / Python 表达式 / 算法文件三种
"""

import importlib.util

from PyQt6.QtCore import QTimer


SESSIONS = {
    0x01: "默认会话 (default)",
    0x02: "编程会话 (programming)",
    0x03: "扩展会话 (extended)",
}

NRCS = {
    0x10: "GeneralReject 一般拒绝",
    0x11: "ServiceNotSupported 服务不支持",
    0x12: "SubFunctionNotSupported 子功能不支持",
    0x13: "IncorrectMessageLengthOrInvalidFormat 长度/格式错误",
    0x14: "ResponseTooLong 响应过长",
    0x21: "BusyRepeatRequest 忙-重发请求",
    0x22: "ConditionsNotCorrect 条件不满足",
    0x24: "RequestSequenceError 请求序列错误",
    0x25: "NoResponseFromSubnetComponent 子组件无响应",
    0x26: "FailurePreventsExecution 失败阻止执行",
    0x31: "RequestOutOfRange 请求超出范围",
    0x33: "SecurityAccessDenied 安全访问被拒",
    0x35: "InvalidKey 密钥无效",
    0x36: "ExceededNumberOfAttempts 尝试次数超限",
    0x37: "RequiredTimeDelayNotExpired 时间延迟未到",
    0x70: "UploadDownloadNotAccepted 不允许下载",
    0x71: "TransferDataSuspended 传输挂起",
    0x72: "GeneralProgrammingFailure 编程失败",
    0x73: "WrongBlockSequenceCounter 块序号错误",
    0x78: "ResponsePending 响应挂起（ECU 处理中）",
    0x7E: "SubFunctionNotSupportedInActiveSession 当前会话不支持子功能",
    0x7F: "ServiceNotSupportedInActiveSession 当前会话不支持服务",
}

DTC_STATUS_BITS = {
    0x01: "testFailed 最近测试失败",
    0x02: "testFailedThisOperationCycle 本周期测试失败",
    0x04: "pendingDTC 待定 DTC",
    0x08: "confirmedDTC 已确认 DTC",
    0x10: "testNotCompletedSinceLastClear 清除后未测试",
    0x20: "testFailedSinceLastClear 清除后测试失败",
    0x40: "testNotCompletedThisOperationCycle 本周期未测试",
    0x80: "warningIndicatorRequested 请求警告指示",
}

DTC_KIND = {0: "P", 1: "C", 2: "B", 3: "U"}


def dtc_to_text(dtc_bytes):
    """3 字节 DTC → 'P0147' 文本"""
    if len(dtc_bytes) < 2:
        return "??"
    kind = DTC_KIND.get((dtc_bytes[0] >> 6) & 0x03, "?")
    num = ((dtc_bytes[0] & 0x3F) << 8) | dtc_bytes[1]
    return f"{kind}{num:04X}"


def dtc_status_text(status):
    """状态字节 → 置位项列表文本"""
    bits = [name for mask, name in DTC_STATUS_BITS.items() if status & mask]
    return "; ".join(bits) if bits else "无置位"


# ============================================================
#  请求编码器（返回 bytes；参数非法抛 ValueError）
# ============================================================

def encode_10(session):
    if session not in SESSIONS:
        raise ValueError(f"非法会话 0x{session:02X}")
    return bytes([0x10, session])


def encode_11(reset_type):
    if reset_type not in (0x01, 0x02, 0x03):
        raise ValueError("复位类型需为 01/02/03")
    return bytes([0x11, reset_type])


def encode_14(group_int):
    if not 0 <= group_int <= 0xFFFFFF:
        raise ValueError("DTC 组需为 3 字节（0xFFFFFF=全部）")
    return bytes([0x14]) + group_int.to_bytes(3, "big")


def encode_19(sub, mask=None):
    """0x01/0x02 附带状态掩码字节；0x04（按 DTC 号）与 0x0A 不带，由表单直接拼"""
    req = bytes([0x19, sub])
    if sub in (0x01, 0x02) and mask is not None:
        req += bytes([mask & 0xFF])
    return req


def encode_22(did):
    if not 0 <= did <= 0xFFFF:
        raise ValueError("DID 需为 2 字节")
    return bytes([0x22]) + did.to_bytes(2, "big")


def encode_2e(did, data):
    if not data:
        raise ValueError("写数据不能为空")
    return bytes([0x2E]) + did.to_bytes(2, "big") + data


def encode_27(sub):
    return bytes([0x27, sub])


def encode_28(control, comm_type=0x01):
    return bytes([0x28, control, comm_type])


def encode_2f(did, control, data=b""):
    return bytes([0x2F]) + did.to_bytes(2, "big") + bytes([control]) + data


def encode_31(sub, rid):
    return bytes([0x31, sub]) + rid.to_bytes(2, "big")


def encode_34(addr, size, addr_len=4, size_len=4, compression=0x0, encryption=0x0):
    """dataFormatIdentifier 为单字节（高半字节压缩方法/低半字节加密方法）；
    addressAndLengthFormatIdentifier 高半字节=地址字节数、低半字节=长度字节数"""
    dfi = ((compression & 0x0F) << 4) | (encryption & 0x0F)
    fmt = (addr_len << 4) | size_len
    return (bytes([0x34, dfi, fmt])
            + addr.to_bytes(addr_len, "big") + size.to_bytes(size_len, "big"))


def encode_36(counter, data):
    if not 0 <= counter <= 0xFF:
        raise ValueError("块序号需 1 字节")
    if not data:
        raise ValueError("块数据不能为空")
    return bytes([0x36, counter]) + data


def encode_37():
    return bytes([0x37])


def encode_3e(sub=0x80):
    return bytes([0x3E, sub])


def encode_85(sub):
    return bytes([0x85, sub])


def encode_raw(sid, extra):
    return bytes([sid]) + extra


# ============================================================
#  响应解码
# ============================================================

def describe_response(resp, req=None):
    """resp: 完整响应 PDU；req: 对应请求（可 None，用于补充上下文）→ 可读文本"""
    if not resp:
        return "空响应"
    sid = resp[0]
    if sid == 0x7F and len(resp) >= 3:
        req_sid, nrc = resp[1], resp[2]
        name = NRCS.get(nrc, "未知")
        return f"否定响应 服务 0x{req_sid:02X} NRC 0x{nrc:02X} {name}"
    pos = sid - 0x40
    if not 0 < pos < 0xC0:
        return "非 UDS 响应"
    hex_tail = " ".join(f"{b:02X}" for b in resp[1:])
    detail = _positive_detail(pos, resp, req)
    return f"正响应 0x{sid:02X}（服务 0x{pos:02X}）{hex_tail}{(' — ' + detail) if detail else ''}"


def _positive_detail(pos, resp, req):
    try:
        if pos == 0x10 and len(resp) >= 3:          # 0x50 会话
            s = SESSIONS.get(resp[1], f"0x{resp[1]:02X}")
            p2 = int.from_bytes(resp[2:4], "big") * 10 if len(resp) >= 4 else "?"
            p2s = int.from_bytes(resp[4:6], "big") * 10 if len(resp) >= 6 else "?"
            return f"{s} P2={p2}ms P2*={p2s}ms"
        if pos == 0x19:                              # 0x59 读 DTC
            sub = resp[1]
            if sub == 0x01 and len(resp) >= 4:
                return (f"状态可用性 0x{resp[2]:02X} DTC 数 {int.from_bytes(resp[3:5], 'big')}")
            if sub in (0x02, 0x04, 0x06, 0x0A):
                return f"包含 {len(resp[2:]) // 3} 条 DTC（详见 DTC 面板表格）"
        if pos == 0x22:                              # 0x62 读 DID
            if len(resp) >= 3:
                did = int.from_bytes(resp[1:3], "big")
                return f"DID 0x{did:04X} 数据 {len(resp[3:])} 字节"
        if pos == 0x27:                              # 0x67 安全访问
            sub = resp[1]
            if sub % 2 == 1:
                return f"种子 seed = {resp[2:].hex().upper()}"
            return "密钥校验通过"
        if pos == 0x31:                              # 0x71 例程
            rid = int.from_bytes(resp[2:4], "big") if len(resp) >= 4 else 0
            st = resp[4] if len(resp) >= 5 else 0
            return f"例程 0x{rid:04X} 状态 0x{st:02X}"
        if pos == 0x34 and len(resp) >= 3:           # 0x74 请求下载
            fmt = resp[1]
            n = fmt >> 4                             # 高半字节 = maxNumberOfBlockLength 字节数
            if n and len(resp) >= 2 + n:
                max_block = int.from_bytes(resp[2:2 + n], "big")
                return f"单块最大长度 {max_block} 字节（0x{max_block:X}）"
        if pos == 0x36:                              # 0x76 数据传输
            return f"块序号 {resp[1]} 传输确认"
        if pos == 0x37:
            return "传输退出成功"
        if pos == 0x3E:
            return "在线确认"
    except Exception:
        pass
    return ""


# ============================================================
#  安全访问密钥算法
# ============================================================

def calc_key_demo(seed, length):
    """演示算法：seed 与 0xA5A5...A5 逐位异或（长度跟随 seed）"""
    mask = int.from_bytes(b"\xA5" * length, "big")
    return (int.from_bytes(seed, "big") ^ mask).to_bytes(length, "big")


def calc_key_expr(expr, seed):
    """Python 表达式算法：可用变量 seed（大端整数）/ seed_bytes，返回 int 或 bytes"""
    env = {"seed": int.from_bytes(seed, "big"), "seed_bytes": seed}
    result = eval(expr, {"__builtins__": {}}, env)  # noqa: S307 — 用户本地自定义表达式
    if isinstance(result, int):
        return result.to_bytes(max(1, (result.bit_length() + 7) // 8), "big")
    return bytes(result)


def calc_key_file(path, seed):
    """算法文件：需提供 calculate_key(seed_bytes) -> int 或 bytes"""
    spec = importlib.util.spec_from_file_location("uds_key_algo", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    result = mod.calculate_key(seed)
    if isinstance(result, int):
        return result.to_bytes(max(1, (result.bit_length() + 7) // 8), "big")
    return bytes(result)


# ============================================================
#  客户端
# ============================================================

class UdsClient:
    """单事务 UDS 客户端（挂在 IsotpLayer 之上）。

    回调:
        on_sent(pdu)                     — 请求发出（PDU 级）
        on_pdu(resp, desc)               — 收到任意响应 PDU（日志用）
        on_pending()                     — 收到 0x78
    """

    def __init__(self, isotp, qt_parent=None):
        self.isotp = isotp
        self.p2_ms = 2000
        self.p2star_ms = 5000
        self.on_sent = None
        self.on_pdu = None
        self.on_pending = None

        self._pending = None          # (req_pdu, on_done)
        self._timer = QTimer(qt_parent)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._on_timeout)

        isotp.on_received = self._on_pdu

    # --------------------------------------------------------
    def request(self, pdu, functional=False, expect_response=True, on_done=None):
        """发送请求。on_done(ok, resp, note) 在收到匹配响应或超时后回调（主线程）。

        expect_response=False（3E 抑制响应 / 功能寻址广播）时不占用事务槽。
        """
        if self._pending is not None:
            if on_done:
                on_done(False, None, "上一个请求仍在等待响应（P2/P2* 未超时）")
            return
        self.isotp.send(pdu, functional)
        if self.on_sent:
            self.on_sent(pdu)
        if not expect_response or functional:
            if on_done:
                on_done(True, None, "已发送（不等待响应）")
            return
        self._pending = (bytes(pdu), on_done)
        self._timer.start(self.p2_ms)

    @property
    def busy(self):
        return self._pending is not None

    def cancel(self):
        if self._pending:
            _, cb = self._pending
            self._pending = None
            self._timer.stop()
            if cb:
                cb(False, None, "已取消")

    # --------------------------------------------------------
    def _on_pdu(self, resp):
        desc = describe_response(resp, self._pending[0] if self._pending else None)
        if self.on_pdu:
            self.on_pdu(bytes(resp), desc)
        if not self._pending:
            return                      # 无等待事务（如功能寻址多 ECU 回复），仅记录
        req, cb = self._pending
        # 匹配：正响应 sid = req sid + 0x40；或 0x78 pending；或否定响应指向本服务
        if len(resp) >= 1 and resp[0] == req[0] + 0x40:
            self._finish(True, resp, cb, desc)
        elif len(resp) >= 3 and resp[0] == 0x7F and resp[1] == req[0]:
            if resp[2] == 0x78:
                if self.on_pending:
                    self.on_pending()
                self._timer.start(self.p2star_ms)   # P2* 续等
                return
            self._finish(False, resp, cb, desc)
        # 其他 PDU：不匹配当前事务，仅记录日志

    def _on_timeout(self):
        if self._pending:
            _, cb = self._pending
            self._pending = None
            if cb:
                cb(False, None, "P2/P2* 超时，ECU 未响应")

    def _finish(self, ok, resp, cb, desc):
        self._pending = None
        self._timer.stop()
        if cb:
            cb(ok, resp, desc)

    def shutdown(self):
        self._timer.stop()
        self._pending = None
