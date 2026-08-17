# -*- coding: utf-8 -*-
"""UDS 插件协议层自测（G10）— 模拟 ECU 交互验证 ISO-TP/客户端状态机

用法: python plugins/uds-diagnostic/tests/test_proto.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from PyQt6.QtCore import QCoreApplication, QTimer

from uds_isotp import IsotpLayer, decode_stmin
from uds_client import (UdsClient, encode_22, encode_34, describe_response,
                        dtc_to_text, dtc_status_text, calc_key_demo,
                        calc_key_expr, calc_key_file)

app = QCoreApplication([])
sent = []
fails = []


def check(cond, msg):
    if cond:
        print(f"  PASS {msg}")
    else:
        fails.append(msg)
        print(f"  FAIL {msg}")


print("[1] 纯函数")
check(decode_stmin(0x05) == 5 and decode_stmin(0xF1) == 1 and decode_stmin(0xFA) == 0,
      "decode_stmin")
check(dtc_to_text(bytes([0x01, 0x47])) == "P0147", "dtc_to_text P0147")
check(dtc_to_text(bytes([0xC1, 0x47])) == "U0147", "dtc_to_text U0147")
check("testFailed" in dtc_status_text(0x09) and "confirmedDTC" in dtc_status_text(0x09),
      "dtc_status_text")
check("InvalidKey" in describe_response(bytes([0x7F, 0x27, 0x35])), "NRC 0x35 解码")
check("P2" in describe_response(bytes([0x50, 0x03, 0x00, 0x32, 0x01, 0x90])),
      "0x50 会话 P2 解码")
check(calc_key_demo(b"\x01\x02\x03\x04", 4) == b"\xA4\xA7\xA6\xA1", "演示密钥 XOR")
check(calc_key_expr("seed ^ 0x11223344", b"\x01\x02\x03\x04") == (0x01020304 ^ 0x11223344).to_bytes(4, "big"),
      "表达式密钥")
algo_path = os.path.join(HERE, "_algo_tmp.py")
with open(algo_path, "w") as f:
    f.write("def calculate_key(seed):\n    return bytes(b ^ 0x5A for b in seed)\n")
try:
    check(calc_key_file(algo_path, b"\x00\xFF") == b"\x5A\xA5",
          "算法文件密钥")
finally:
    os.remove(algo_path)
check(encode_34(0x08040000, 0x10000) == bytes.fromhex("3400440804000000010000"),
      "encode_34（11 字节：34 00 44 + 地址 4B + 长度 4B）")

print("[2] ISO-TP 发送：SF")
iso = IsotpLayer(lambda cid, d: sent.append((cid, bytes(d))))
iso.send(bytes([0x22, 0xF1, 0x90]))
check(sent[-1] == (0x7E0, bytes([3, 0x22, 0xF1, 0x90]) + b"\xCC" * 4), "SF 帧")

print("[3] ISO-TP 发送：FF+CF 按 BS 分批 + STmin")
sent.clear()
pdu = bytes([0x2E, 0xF1, 0x90]) + bytes(range(20))      # 23 字节：FF 带 6，剩 17 = 3 个 CF
iso.send(pdu)
check(sent[0][1][0] >> 4 == 1 and (sent[0][1][0] & 0x0F) == 0 and sent[0][1][1] == 23, "FF 总长")
iso.on_frame(0x7E8, bytes([0x30, 2, 0]) + b"\xCC" * 5)   # FC CTS bs=2
check([f[1][0] >> 4 for f in sent[1:]] == [2, 2], "BS=2 先发 2 个 CF")
iso.on_frame(0x7E8, bytes([0x30, 2, 0]) + b"\xCC" * 5)   # 第二个 FC
check([f[1][0] >> 4 for f in sent[3:]] == [2], "再发剩余 1 个 CF")
seqs = [f[1][0] & 0x0F for f in sent[1:]]
check(seqs == [1, 2, 3], f"CF 序号 {seqs}")
payload = sent[0][1][2:8] + b"".join(f[1][1:8] for f in sent[1:])
check(payload[:23] == pdu, "CF 重组还原 PDU")

print("[4] ISO-TP 发送：FC WAIT 与 OVFLW")
iso2 = IsotpLayer(lambda cid, d: sent.append((cid, bytes(d))))
sent.clear()
iso2.send(bytes(range(30)))
check(len(sent) == 1, "FF 后等待 FC 不发 CF")
iso2.on_frame(0x7E8, bytes([0x31]) + b"\xCC" * 7)        # WAIT
iso2.on_frame(0x7E8, bytes([0x30, 0, 0]) + b"\xCC" * 5)  # CTS bs=0
check(len(sent) == 1 + 4, f"BS=0 一次发完 4 CF（{len(sent)-1}）")

print("[5] ISO-TP 接收：FF 自动回 FC（在 tx_id 上）+ CF 组包")
received = []
iso3 = IsotpLayer(lambda cid, d: sent.append((cid, bytes(d))))
iso3.on_received = lambda p: received.append(bytes(p))
sent.clear()
resp = bytes([0x62, 0xF1, 0x90]) + b"VIN1234567890AB12"   # 20 字节
ff = (bytes([0x10 | (20 >> 8), 20 & 0xFF]) + resp[:6]).ljust(8, b"\xCC")
iso3.on_frame(0x7E8, ff)
check(sent[-1][0] == 0x7E0 and sent[-1][1][0] >> 4 == 0x03, "FC 发在 tx_id=0x7E0")
rest, seq = resp[6:], 1
while rest:
    cf = (bytes([0x20 | seq]) + rest[:7]).ljust(8, b"\xCC")
    iso3.on_frame(0x7E8, cf)
    rest = rest[7:]
    seq = (seq + 1) & 0x0F
check(received and received[0] == resp, "多帧响应组包还原")

print("[6] UDS 客户端：0x78 续等 + 正响应匹配")
client = UdsClient(iso3)
client.p2_ms = 100
client.p2star_ms = 100
got = []
client.request(encode_22(0xF190), on_done=lambda ok, r, note: got.append((ok, bytes(r) if r else r, note)))
iso3.on_frame(0x7E8, (bytes([3, 0x7F, 0x22, 0x78]) + b"\xCC" * 4))
check(not got, "0x78 后继续等待（P2*）")
iso3.on_frame(0x7E8, (bytes([4, 0x62, 0xF1, 0x90, 0x41]) + b"\xCC" * 3))
check(got and got[0][0] and got[0][1] == bytes([0x62, 0xF1, 0x90, 0x41]), "正响应匹配")

print("[7] UDS 客户端：P2 超时")
got.clear()
client.request(encode_22(0xF190), on_done=lambda ok, r, note: got.append((ok, r, note)))
QTimer.singleShot(400, app.quit)
app.exec()
check(got and not got[0][0], "P2 超时回调失败")

print("[8] 功能寻址拒绝多帧")
err = []
iso4 = IsotpLayer(lambda cid, d: sent.append((cid, bytes(d))))
iso4.on_error = lambda m: err.append(m)
iso4.send(bytes(range(20)), functional=True)
check(err and "功能寻址" in err[0], "多帧功能寻址报错")
check(sent[-1][0] == 0x7E0, "未实际发送")

print()
if fails:
    print(f"结果: {len(fails)} 项失败: {fails}")
    sys.exit(1)
print("结果: 全部通过")
