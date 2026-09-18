# -*- coding: utf-8 -*-
"""UDS Suite protocol smoke: ISO-TP / client state machine against a fake ECU.

Usage: python plugins/uds-suite/tests/test_proto.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from PyQt6.QtCore import QCoreApplication, QTimer

from core.uds_isotp import IsotpLayer, decode_stmin
from core.uds_client import (
    UdsClient, encode_22, encode_34, describe_response,
    dtc_to_text, dtc_status_text, calc_key_demo,
    calc_key_expr, calc_key_file,
)

app = QCoreApplication([])
sent = []
fails = []


def check(cond, msg):
    if cond:
        print("  PASS %s" % msg)
    else:
        fails.append(msg)
        print("  FAIL %s" % msg)


print("[1] pure functions")
check(decode_stmin(0x05) == 5 and decode_stmin(0xF1) == 1 and decode_stmin(0xFA) == 0,
      "decode_stmin")
check(dtc_to_text(bytes([0x01, 0x47])) == "P0147", "dtc_to_text P0147")
check(dtc_to_text(bytes([0xC1, 0x47])) == "U0147", "dtc_to_text U0147")
check("testFailed" in dtc_status_text(0x09) and "confirmedDTC" in dtc_status_text(0x09),
      "dtc_status_text")
check("InvalidKey" in describe_response(bytes([0x7F, 0x27, 0x35])), "NRC 0x35 decode")
check("P2" in describe_response(bytes([0x50, 0x03, 0x00, 0x32, 0x01, 0x90])),
      "0x50 session P2 decode")
check(calc_key_demo(b"\x01\x02\x03\x04", 4) == b"\xA4\xA7\xA6\xA1", "demo key XOR")
check(calc_key_expr("seed ^ 0x11223344", b"\x01\x02\x03\x04")
      == (0x01020304 ^ 0x11223344).to_bytes(4, "big"),
      "expression key")
algo_path = os.path.join(HERE, "_algo_tmp.py")
with open(algo_path, "w", encoding="utf-8") as f:
    f.write("def calculate_key(seed):\n    return bytes(b ^ 0x5A for b in seed)\n")
try:
    check(calc_key_file(algo_path, b"\x00\xFF") == b"\x5A\xA5", "algorithm file key")
finally:
    if os.path.isfile(algo_path):
        os.remove(algo_path)
check(encode_34(0x08040000, 0x10000) == bytes.fromhex("3400440804000000010000"),
      "encode_34 (11 bytes)")

print("[2] ISO-TP TX: SF")
iso = IsotpLayer(lambda cid, d: sent.append((cid, bytes(d))))
iso.send(bytes([0x22, 0xF1, 0x90]))
check(sent[-1] == (0x7E0, bytes([3, 0x22, 0xF1, 0x90]) + b"\xCC" * 4), "SF frame")

print("[3] ISO-TP TX: FF+CF with BS and STmin")
sent.clear()
pdu = bytes([0x2E, 0xF1, 0x90]) + bytes(range(20))
iso.send(pdu)
check(sent[0][1][0] >> 4 == 1 and (sent[0][1][0] & 0x0F) == 0 and sent[0][1][1] == 23,
      "FF length")
iso.on_frame(0x7E8, bytes([0x30, 2, 0]) + b"\xCC" * 5)
check([f[1][0] >> 4 for f in sent[1:]] == [2, 2], "BS=2 sends 2 CF first")
iso.on_frame(0x7E8, bytes([0x30, 2, 0]) + b"\xCC" * 5)
check([f[1][0] >> 4 for f in sent[3:]] == [2], "remaining 1 CF")
seqs = [f[1][0] & 0x0F for f in sent[1:]]
check(seqs == [1, 2, 3], "CF sequence %s" % seqs)
payload = sent[0][1][2:8] + b"".join(f[1][1:8] for f in sent[1:])
check(payload[:23] == pdu, "CF reassembly")

print("[4] ISO-TP TX: FC WAIT and OVFLW")
iso2 = IsotpLayer(lambda cid, d: sent.append((cid, bytes(d))))
sent.clear()
iso2.send(bytes(range(30)))
check(len(sent) == 1, "wait for FC before CF")
iso2.on_frame(0x7E8, bytes([0x31]) + b"\xCC" * 7)
iso2.on_frame(0x7E8, bytes([0x30, 0, 0]) + b"\xCC" * 5)
check(len(sent) == 1 + 4, "BS=0 sends 4 CF (%d)" % (len(sent) - 1))

print("[5] ISO-TP RX: FF auto FC on tx_id + CF reassembly")
received = []
iso3 = IsotpLayer(lambda cid, d: sent.append((cid, bytes(d))))
iso3.on_received = lambda p: received.append(bytes(p))
sent.clear()
resp = bytes([0x62, 0xF1, 0x90]) + b"VIN1234567890AB12"
ff = (bytes([0x10 | (20 >> 8), 20 & 0xFF]) + resp[:6]).ljust(8, b"\xCC")
iso3.on_frame(0x7E8, ff)
check(sent[-1][0] == 0x7E0 and sent[-1][1][0] >> 4 == 0x03, "FC on tx_id=0x7E0")
rest, seq = resp[6:], 1
while rest:
    cf = (bytes([0x20 | seq]) + rest[:7]).ljust(8, b"\xCC")
    iso3.on_frame(0x7E8, cf)
    rest = rest[7:]
    seq = (seq + 1) & 0x0F
check(received and received[0] == resp, "multi-frame response reassembly")

print("[6] UDS client: 0x78 pending + positive response")
client = UdsClient(iso3)
client.p2_ms = 100
client.p2star_ms = 100
got = []
client.request(
    encode_22(0xF190),
    on_done=lambda ok, r, note: got.append((ok, bytes(r) if r else r, note)))
iso3.on_frame(0x7E8, (bytes([3, 0x7F, 0x22, 0x78]) + b"\xCC" * 4))
check(not got, "0x78 keeps waiting (P2*)")
iso3.on_frame(0x7E8, (bytes([4, 0x62, 0xF1, 0x90, 0x41]) + b"\xCC" * 3))
check(got and got[0][0] and got[0][1] == bytes([0x62, 0xF1, 0x90, 0x41]),
      "positive response match")

print("[7] UDS client: P2 timeout")
got.clear()
client.request(
    encode_22(0xF190),
    on_done=lambda ok, r, note: got.append((ok, r, note)))
QTimer.singleShot(400, app.quit)
app.exec()
check(got and not got[0][0], "P2 timeout fails the callback")

print("[8] functional addressing rejects multi-frame")
err = []
iso4 = IsotpLayer(lambda cid, d: sent.append((cid, bytes(d))))
iso4.on_error = lambda m: err.append(m)
iso4.send(bytes(range(20)), functional=True)
check(err and "Functional addressing" in err[0], "multi-frame functional error")
check(sent[-1][0] == 0x7E0, "frame was not sent")

print()
if fails:
    print("result: %d failed: %s" % (len(fails), fails))
    sys.exit(1)
print("result: all passed")
