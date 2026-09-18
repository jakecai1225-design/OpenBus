"""uds_client.py — UDS (ISO 14229-1) client: request/response matching and timeouts.

- Single request-response: P2 (default 2s); NRC 0x78 switches to P2* (default 5s)
- Functional addressing / suppress-positive (subfn bit7) skips response timer
- Per-service encoders and describe_response decoder
- DTC decode (3 bytes -> P/C/B/U + status bitmap)
- SecurityAccess key: demo XOR / Python expr / algo file
"""

import importlib.util

from PyQt6.QtCore import QTimer


SESSIONS = {
    0x01: "Default session",
    0x02: "Programming session",
    0x03: "Extended session",
}

NRCS = {
    0x10: "GeneralReject",
    0x11: "ServiceNotSupported",
    0x12: "SubFunctionNotSupported",
    0x13: "IncorrectMessageLengthOrInvalidFormat",
    0x14: "ResponseTooLong",
    0x21: "BusyRepeatRequest",
    0x22: "ConditionsNotCorrect",
    0x24: "RequestSequenceError",
    0x25: "NoResponseFromSubnetComponent",
    0x26: "FailurePreventsExecution",
    0x31: "RequestOutOfRange",
    0x33: "SecurityAccessDenied",
    0x35: "InvalidKey",
    0x36: "ExceededNumberOfAttempts",
    0x37: "RequiredTimeDelayNotExpired",
    0x70: "UploadDownloadNotAccepted",
    0x71: "TransferDataSuspended",
    0x72: "GeneralProgrammingFailure",
    0x73: "WrongBlockSequenceCounter",
    0x78: "ResponsePending",
    0x7E: "SubFunctionNotSupportedInActiveSession",
    0x7F: "ServiceNotSupportedInActiveSession",
}

DTC_STATUS_BITS = {
    0x01: "testFailed",
    0x02: "testFailedThisOperationCycle",
    0x04: "pendingDTC",
    0x08: "confirmedDTC",
    0x10: "testNotCompletedSinceLastClear",
    0x20: "testFailedSinceLastClear",
    0x40: "testNotCompletedThisOperationCycle",
    0x80: "warningIndicatorRequested",
}

DTC_KIND = {0: "P", 1: "C", 2: "B", 3: "U"}


def dtc_to_text(dtc_bytes):
    """3-byte DTC -> 'P0147' text."""
    if len(dtc_bytes) < 2:
        return "??"
    kind = DTC_KIND.get((dtc_bytes[0] >> 6) & 0x03, "?")
    num = ((dtc_bytes[0] & 0x3F) << 8) | dtc_bytes[1]
    return f"{kind}{num:04X}"


def dtc_status_text(status):
    """Status byte -> list of set bit names."""
    bits = [name for mask, name in DTC_STATUS_BITS.items() if status & mask]
    return "; ".join(bits) if bits else "(none)"


# ============================================================
#  Request encoders (return bytes; raise ValueError on bad args)
# ============================================================

def encode_10(session):
    if session not in SESSIONS:
        raise ValueError(f"Invalid session 0x{session:02X}")
    return bytes([0x10, session])


def encode_11(reset_type):
    if reset_type not in (0x01, 0x02, 0x03):
        raise ValueError("Reset type must be 01/02/03")
    return bytes([0x11, reset_type])


def encode_14(group_int):
    if not 0 <= group_int <= 0xFFFFFF:
        raise ValueError("DTC group must be 3 bytes (0xFFFFFF=all)")
    return bytes([0x14]) + group_int.to_bytes(3, "big")


def encode_19(sub, mask=None):
    """0x01/0x02 take status mask; 0x04/0x0A assembled by caller forms."""
    req = bytes([0x19, sub])
    if sub in (0x01, 0x02) and mask is not None:
        req += bytes([mask & 0xFF])
    return req


def encode_22(did):
    if not 0 <= did <= 0xFFFF:
        raise ValueError("DID must be 2 bytes")
    return bytes([0x22]) + did.to_bytes(2, "big")


def encode_2e(did, data):
    if not data:
        raise ValueError("Write data must not be empty")
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
    """dataFormatIdentifier: high nibble compression / low encryption;
    addressAndLengthFormatIdentifier: high nibble addr len / low size len."""
    dfi = ((compression & 0x0F) << 4) | (encryption & 0x0F)
    fmt = (addr_len << 4) | size_len
    return (bytes([0x34, dfi, fmt])
            + addr.to_bytes(addr_len, "big") + size.to_bytes(size_len, "big"))


def encode_36(counter, data):
    if not 0 <= counter <= 0xFF:
        raise ValueError("Block counter must be 1 byte")
    if not data:
        raise ValueError("Block data must not be empty")
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
#  Response decode
# ============================================================

def describe_response(resp, req=None):
    """resp: full response PDU; req: matching request (optional) -> text."""
    if not resp:
        return "Empty response"
    sid = resp[0]
    if sid == 0x7F and len(resp) >= 3:
        req_sid, nrc = resp[1], resp[2]
        name = NRCS.get(nrc, "unknown")
        return f"Negative response service 0x{req_sid:02X} NRC 0x{nrc:02X} {name}"
    pos = sid - 0x40
    if not 0 < pos < 0xC0:
        return "Not a UDS response"
    hex_tail = " ".join(f"{b:02X}" for b in resp[1:])
    detail = _positive_detail(pos, resp, req)
    return f"Positive 0x{sid:02X} (service 0x{pos:02X}) {hex_tail}{(' — ' + detail) if detail else ''}"


def _positive_detail(pos, resp, req):
    try:
        if pos == 0x10 and len(resp) >= 3:
            s = SESSIONS.get(resp[1], f"0x{resp[1]:02X}")
            p2 = int.from_bytes(resp[2:4], "big") * 10 if len(resp) >= 4 else "?"
            p2s = int.from_bytes(resp[4:6], "big") * 10 if len(resp) >= 6 else "?"
            return f"{s} P2={p2}ms P2*={p2s}ms"
        if pos == 0x19:
            sub = resp[1]
            if sub == 0x01 and len(resp) >= 4:
                return (f"statusAvailability 0x{resp[2]:02X} DTC count "
                        f"{int.from_bytes(resp[3:5], 'big')}")
            if sub in (0x02, 0x04, 0x06, 0x0A):
                return f"{len(resp[2:]) // 3} DTC entries (see DTC panel)"
        if pos == 0x22:
            if len(resp) >= 3:
                did = int.from_bytes(resp[1:3], "big")
                return f"DID 0x{did:04X} data {len(resp[3:])} bytes"
        if pos == 0x27:
            sub = resp[1]
            if sub % 2 == 1:
                return f"seed = {resp[2:].hex().upper()}"
            return "Key accepted"
        if pos == 0x31:
            rid = int.from_bytes(resp[2:4], "big") if len(resp) >= 4 else 0
            st = resp[4] if len(resp) >= 5 else 0
            return f"Routine 0x{rid:04X} status 0x{st:02X}"
        if pos == 0x34 and len(resp) >= 3:
            fmt = resp[1]
            n = fmt >> 4
            if n and len(resp) >= 2 + n:
                max_block = int.from_bytes(resp[2:2 + n], "big")
                return f"max block length {max_block} bytes (0x{max_block:X})"
        if pos == 0x36:
            return f"Block {resp[1]} confirmed"
        if pos == 0x37:
            return "Transfer exit OK"
        if pos == 0x3E:
            return "TesterPresent ACK"
    except Exception:
        pass
    return ""


# ============================================================
#  SecurityAccess key algorithms
# ============================================================

def calc_key_demo(seed, length):
    """Demo: XOR seed with 0xA5...A5 (length follows seed)."""
    mask = int.from_bytes(b"\xA5" * length, "big")
    return (int.from_bytes(seed, "big") ^ mask).to_bytes(length, "big")


def calc_key_expr(expr, seed):
    """Python expression: vars seed (big-endian int) / seed_bytes; return int or bytes."""
    env = {"seed": int.from_bytes(seed, "big"), "seed_bytes": seed}
    result = eval(expr, {"__builtins__": {}}, env)  # noqa: S307 — local user expression
    if isinstance(result, int):
        return result.to_bytes(max(1, (result.bit_length() + 7) // 8), "big")
    return bytes(result)


def calc_key_file(path, seed):
    """Algo file must provide calculate_key(seed_bytes) -> int or bytes."""
    spec = importlib.util.spec_from_file_location("uds_key_algo", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    result = mod.calculate_key(seed)
    if isinstance(result, int):
        return result.to_bytes(max(1, (result.bit_length() + 7) // 8), "big")
    return bytes(result)


# ============================================================
#  Client
# ============================================================

class UdsClient:
    """Single-transaction UDS client on top of IsotpLayer.

    Callbacks:
        on_sent(pdu)
        on_pdu(resp, desc)
        on_pending()  — NRC 0x78
    """

    def __init__(self, isotp, qt_parent=None):
        self.isotp = isotp
        self.p2_ms = 2000
        self.p2star_ms = 5000
        self.on_sent = None
        self.on_pdu = None
        self.on_pending = None

        self._pending = None
        self._timer = QTimer(qt_parent)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._on_timeout)

        isotp.on_received = self._on_pdu

    def request(self, pdu, functional=False, expect_response=True, on_done=None):
        """Send request. on_done(ok, resp, note) on match or timeout (UI thread).

        expect_response=False (3E suppress / functional broadcast) does not take the slot.
        """
        if self._pending is not None:
            if on_done:
                on_done(False, None, "Previous request still waiting (P2/P2*)")
            return
        self.isotp.send(pdu, functional)
        if self.on_sent:
            self.on_sent(pdu)
        if not expect_response or functional:
            if on_done:
                on_done(True, None, "Sent (no response wait)")
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
                cb(False, None, "Cancelled")

    def _on_pdu(self, resp):
        desc = describe_response(resp, self._pending[0] if self._pending else None)
        if self.on_pdu:
            self.on_pdu(bytes(resp), desc)
        if not self._pending:
            return
        req, cb = self._pending
        if len(resp) >= 1 and resp[0] == req[0] + 0x40:
            self._finish(True, resp, cb, desc)
        elif len(resp) >= 3 and resp[0] == 0x7F and resp[1] == req[0]:
            if resp[2] == 0x78:
                if self.on_pending:
                    self.on_pending()
                self._timer.start(self.p2star_ms)
                return
            self._finish(False, resp, cb, desc)

    def _on_timeout(self):
        if self._pending:
            _, cb = self._pending
            self._pending = None
            if cb:
                cb(False, None, "P2/P2* timeout — no ECU response")

    def _finish(self, ok, resp, cb, desc):
        self._pending = None
        self._timer.stop()
        if cb:
            cb(ok, resp, desc)

    def shutdown(self):
        self._timer.stop()
        self._pending = None
