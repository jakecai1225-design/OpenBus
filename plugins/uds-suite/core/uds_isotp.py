"""uds_isotp.py — ISO-TP (ISO 15765-2) transport (Qt event-loop driven).

- TX: SF / FF + wait FC (CTS/WAIT/OVFLW) -> CF batches with BS/STmin
- RX: SF deliver; FF auto FC (BS/STmin configurable) -> CF reassembly -> PDU callback
- Timeouts: FC wait 1s; incomplete RX discarded after 1s
- Functional addressing: single-frame only
"""

from PyQt6.QtCore import QTimer

PAD = b"\xCC"


def decode_stmin(raw):
    """FC STmin byte -> milliseconds (0xF1-0xF9 are 100us units; coarsened to 1ms)."""
    if raw <= 0x7F:
        return raw
    if 0xF1 <= raw <= 0xF9:
        return 1
    return 0


class IsotpLayer:
    """Single request/response direction ISO-TP client (tester).

    send_frame: callable(can_id, data: bytes) — usually sin.frames.send
    qt_parent: QObject for QTimer ownership (auto-stop on destroy)
    """

    def __init__(self, send_frame, qt_parent=None):
        self.send_frame = send_frame
        self.tx_id = 0x7E0
        self.func_id = 0x7DF
        self.rx_id = 0x7E8
        self.fc_bs = 8
        self.fc_stmin_ms = 10
        self.on_received = None
        self.on_log = None
        self.on_error = None
        self._alive = True

        self._tx_payload = b""
        self._tx_seq = 1
        self._tx_can_id = 0
        self._stmin_ms = 0
        self._bs_left = 0
        self._fc_timer = QTimer(qt_parent)
        self._fc_timer.setSingleShot(True)
        self._fc_timer.timeout.connect(self._on_fc_timeout)
        self._fc_wait_count = 0

        self._rx_expected = 0
        self._rx_buffer = b""
        self._rx_active = False
        self._rx_timer = QTimer(qt_parent)
        self._rx_timer.setSingleShot(True)
        self._rx_timer.timeout.connect(self._on_rx_timeout)

    def send(self, pdu, functional=False):
        """Send one UDS PDU. Functional addressing allows SF only (<=7 bytes)."""
        if not self._alive or not pdu:
            return
        can_id = self.func_id if functional else self.tx_id
        if len(pdu) <= 7:
            self._send_sf(can_id, pdu)
            return
        if functional:
            if self.on_error:
                self.on_error(
                    "Functional addressing does not support multi-frame "
                    "(ISO 15765-2); request length %d" % len(pdu))
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
                self.on_error("FC wait timeout (1000ms); multi-frame TX aborted")

    def _on_fc(self, data):
        fs = data[0] & 0x0F
        if not self._tx_payload and not self._fc_timer.isActive():
            return
        if fs == 0x00:
            self._fc_timer.stop()
            bs = data[1] if len(data) > 1 else 0
            stmin = decode_stmin(data[2]) if len(data) > 2 else 0
            self._bs_left = bs if bs > 0 else 0xFFFF
            self._stmin_ms = stmin
            self._send_cf_scheduled(0)
        elif fs == 0x01:
            self._fc_wait_count += 1
            if self._fc_wait_count > 10:
                self._tx_payload = b""
                if self.on_error:
                    self.on_error("FC WAIT exceeded 10 times; multi-frame TX aborted")
            else:
                self._fc_timer.start(1000)
        elif fs == 0x02:
            self._tx_payload = b""
            self._fc_timer.stop()
            if self.on_error:
                self.on_error("FC OVFLW: peer buffer overflow; TX aborted")

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
            return
        if self._bs_left <= 0:
            self._fc_wait_count = 0
            self._start_fc_wait()
            return
        self._send_cf_scheduled(self._stmin_ms)

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
            self._rx_timer.start(1000)
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
        frame = bytes([0x30, self.fc_bs & 0xFF, self.fc_stmin_ms & 0xFF]).ljust(8, PAD)
        self.send_frame(self.tx_id, frame)
        self._log("TX", self.tx_id, frame,
                  "FC bs=%d stmin=%dms" % (self.fc_bs, self.fc_stmin_ms))

    def _on_rx_timeout(self):
        if self._rx_active:
            received = len(self._rx_buffer)
            self._rx_active = False
            self._rx_buffer = b""
            if self.on_error:
                self.on_error(
                    "Multi-frame RX timeout; discarded incomplete PDU "
                    "(%d/%d bytes)" % (received, self._rx_expected))

    def shutdown(self):
        self._alive = False
        self._fc_timer.stop()
        self._rx_timer.stop()
        self._tx_payload = b""
        self._rx_active = False

    def _log(self, direction, can_id, data, note):
        if self.on_log:
            self.on_log(direction, can_id, data, note)
