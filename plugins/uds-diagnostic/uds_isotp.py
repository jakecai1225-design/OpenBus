"""uds_isotp.py — ISO-TP（ISO 15765-2）传输层（Qt 事件循环驱动）

G10 UDS 插件强化版，对标 CANoe/TSMaster/ZCANPro 的传输层能力：
- 发送：SF / FF + 等待 FC（CTS/WAIT/OVFLW 状态机）→ 按 BS 分批 CF，间隔 STmin
- 接收：SF 直递；FF 自动回 FC（BS/STmin 可配）→ CF 组包 → 完整 PDU 回调
- 超时：FC 等待 1s、接收半包 1s 丢弃
- 功能寻址仅支持单帧（协议规定），多帧请求自动拒绝
"""

from PyQt6.QtCore import QTimer

PAD = b"\xCC"


def decode_stmin(raw):
    """FC STmin 字节 → 毫秒（0xF1-0xF9 为 100μs 单位即 0.1~0.9ms，取 1ms 粒度）"""
    if raw <= 0x7F:
        return raw
    if 0xF1 <= raw <= 0xF9:
        return 1
    return 0


class IsotpLayer:
    """单请求-响应方向的 ISO-TP 客户端（tester）。

    send_frame: callable(can_id, data: bytes)，由上层接到 sin.frames.send
    qt_parent:  QObject，QTimer 挂靠（随窗口销毁自动停）
    """

    def __init__(self, send_frame, qt_parent=None):
        self.send_frame = send_frame
        # 寻址（ZCANPro 风格：物理/功能/响应三 ID 可配）
        self.tx_id = 0x7E0
        self.func_id = 0x7DF
        self.rx_id = 0x7E8
        # 我方接收时通告的流控参数
        self.fc_bs = 8            # 接收回 FC 的 Block Size
        self.fc_stmin_ms = 10     # 接收回 FC 的 STmin
        # 回调
        self.on_received = None   # (pdu: bytes) 完整 UDS PDU
        self.on_log = None        # (dir, can_id, frame: bytes, note)
        self.on_error = None      # (msg: str)
        self._alive = True        # deactivate 置 False，防悬挂回调

        # ---- 发送状态机 ----
        self._tx_payload = b""    # 待发送剩余 payload
        self._tx_seq = 1
        self._tx_can_id = 0
        self._stmin_ms = 0
        self._bs_left = 0
        self._fc_timer = QTimer(qt_parent)
        self._fc_timer.setSingleShot(True)
        self._fc_timer.timeout.connect(self._on_fc_timeout)
        self._fc_wait_count = 0

        # ---- 接收状态 ----
        self._rx_expected = 0
        self._rx_buffer = b""
        self._rx_active = False
        self._rx_timer = QTimer(qt_parent)
        self._rx_timer.setSingleShot(True)
        self._rx_timer.timeout.connect(self._on_rx_timeout)

    # ------------------------------------------------------------
    #  发送
    # ------------------------------------------------------------
    def send(self, pdu, functional=False):
        """发送一个 UDS PDU。功能寻址仅允许单帧（≤7 字节）。"""
        if not self._alive or not pdu:
            return
        can_id = self.func_id if functional else self.tx_id
        if len(pdu) <= 7:
            self._send_sf(can_id, pdu)
            return
        if functional:
            if self.on_error:
                self.on_error("功能寻址不支持多帧（ISO 15765-2），请求长度 %d" % len(pdu))
            return
        self._send_ff(can_id, pdu)

    def _send_sf(self, can_id, pdu):
        frame = (bytes([len(pdu) & 0x0F]) + pdu).ljust(8, PAD)
        self.send_frame(can_id, frame)
        self._log("TX", can_id, frame, "SF")

    def _send_ff(self, can_id, pdu):
        total = len(pdu)
        ff = (bytes([0x10 | ((total >> 8) & 0x0F), total & 0xFF]) + pdu[:6]).ljust(8, PAD)
        self.send_frame(can_id, ff)
        self._log("TX", can_id, ff, "FF total=%d" % total)
        self._tx_payload = pdu[6:]
        self._tx_seq = 1
        self._tx_can_id = can_id
        self._fc_wait_count = 0
        self._start_fc_wait()

    def _start_fc_wait(self):
        self._fc_timer.start(1000)

    def _on_fc_timeout(self):
        if self._tx_payload:
            self._tx_payload = b""
            if self.on_error:
                self.on_error("等待流控帧 FC 超时（1000ms），多帧发送中止")

    def _on_fc(self, data):
        fs = data[0] & 0x0F
        if not self._tx_payload and not self._fc_timer.isActive():
            return  # 无发送中事务，忽略迟到 FC
        if fs == 0x00:
            # CTS：继续发 CF
            self._fc_timer.stop()
            bs = data[1] if len(data) > 1 else 0
            stmin = decode_stmin(data[2]) if len(data) > 2 else 0
            self._bs_left = bs if bs > 0 else 0xFFFF
            self._stmin_ms = stmin
            self._send_cf_scheduled(0)
        elif fs == 0x01:
            # WAIT：重置 FC 等待，最多 10 次
            self._fc_wait_count += 1
            if self._fc_wait_count > 10:
                self._tx_payload = b""
                if self.on_error:
                    self.on_error("FC WAIT 超过 10 次，多帧发送中止")
            else:
                self._fc_timer.start(1000)
        elif fs == 0x02:
            self._tx_payload = b""
            self._fc_timer.stop()
            if self.on_error:
                self.on_error("FC OVFLW：接收方缓冲区溢出，发送中止")

    def _send_cf_scheduled(self, delay_ms):
        if not self._alive:
            return
        if delay_ms > 0:
            QTimer.singleShot(delay_ms, lambda: self._send_one_cf())
        else:
            self._send_one_cf()

    def _send_one_cf(self):
        if not self._alive or not self._tx_payload:
            return
        chunk = self._tx_payload[:7]
        cf = (bytes([0x20 | (self._tx_seq & 0x0F)]) + chunk).ljust(8, PAD)
        self.send_frame(self._tx_can_id, cf)
        self._log("TX", self._tx_can_id, cf, "CF sn=%d" % self._tx_seq)
        self._tx_seq = (self._tx_seq + 1) & 0x0F
        self._tx_payload = self._tx_payload[7:]
        self._bs_left -= 1
        if not self._tx_payload:
            return  # 全部发完
        if self._bs_left <= 0:
            self._fc_wait_count = 0
            self._start_fc_wait()   # 块发完，等下一个 FC
            return
        self._send_cf_scheduled(self._stmin_ms)

    # ------------------------------------------------------------
    #  接收（喂入 rx_id 上的原始帧）
    # ------------------------------------------------------------
    def on_frame(self, can_id, data):
        if not self._alive or can_id != self.rx_id or not data:
            return
        pci = data[0]
        kind = pci & 0xF0
        if kind == 0x00:
            length = pci & 0x0F
            pdu = data[1:1 + length]
            self._log("RX", can_id, data, "SF")
            self._rx_timer.stop()
            self._rx_active = False
            if pdu and self.on_received:
                self.on_received(pdu)
        elif kind == 0x10:
            self._rx_expected = ((pci & 0x0F) << 8) | data[1]
            self._rx_buffer = data[2:]
            self._rx_active = True
            self._log("RX", can_id, data, "FF total=%d" % self._rx_expected)
            self._send_fc()
            self._rx_timer.start(1000)
        elif kind == 0x20:
            if not self._rx_active:
                return
            self._rx_timer.start(1000)   # 续期
            self._rx_buffer += data[1:]
            self._log("RX", can_id, data, "CF sn=%d" % (pci & 0x0F))
            if len(self._rx_buffer) >= self._rx_expected:
                pdu = self._rx_buffer[:self._rx_expected]
                self._rx_active = False
                self._rx_timer.stop()
                self._log("RX", can_id, pdu, "PDU %d bytes" % len(pdu))
                if self.on_received:
                    self.on_received(pdu)
        elif kind == 0x30:
            self._on_fc(data)

    def _send_fc(self):
        # FC 发在 tester 自己的发送 ID（tx_id）上：ECU 在 tx_id 收流控，
        # 在 rx_id 上发 FF/CF（此前误用 rx_id，ECU 无法收到流控）
        frame = bytes([0x30, self.fc_bs & 0xFF, self.fc_stmin_ms & 0xFF]).ljust(8, PAD)
        self.send_frame(self.tx_id, frame)
        self._log("TX", self.tx_id, frame, "FC bs=%d stmin=%dms" % (self.fc_bs, self.fc_stmin_ms))

    def _on_rx_timeout(self):
        if self._rx_active:
            received = len(self._rx_buffer)   # 先取计数再清空，否则恒为 0
            self._rx_active = False
            self._rx_buffer = b""
            if self.on_error:
                self.on_error("多帧接收超时，丢弃半包（已收 %d/%d 字节）"
                              % (received, self._rx_expected))

    def shutdown(self):
        self._alive = False
        self._fc_timer.stop()
        self._rx_timer.stop()
        self._tx_payload = b""
        self._rx_active = False

    def _log(self, direction, can_id, data, note):
        if self.on_log:
            self.on_log(direction, can_id, data, note)
