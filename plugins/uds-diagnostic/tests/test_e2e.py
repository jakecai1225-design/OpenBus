# -*- coding: utf-8 -*-
"""UDS 插件 E2E 冒烟测试（G10）— offscreen 启动完整 UI + 模拟 ECU 驱动 5 Tab 关键链路

用法: python plugins/uds-diagnostic/tests/test_e2e.py
"""
import os
import sys
import time
import types

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

PLUGIN_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, PLUGIN_DIR)

from PyQt6.QtWidgets import QApplication, QPushButton, QTreeWidget, QTableWidget, \
    QLineEdit, QLabel, QMainWindow

app = QApplication(sys.argv)
sent = []           # tester 实际发出的 CAN 帧 (can_id, bytes)
pdus = []           # 模拟 ECU 收到的完整请求 PDU
fails = []


def check(cond, msg):
    print(("  PASS " if cond else "  FAIL ") + msg)
    if not cond:
        fails.append(msg)


# ---------------- 假 sin 模块 ----------------
fake_sin = types.ModuleType("sin")


class _Output:
    def append(self, text):
        print("[output]", text)


class _UI:
    def create_window(self, title):
        from PyQt6.QtWidgets import QMainWindow
        w = QMainWindow()
        w.setWindowTitle(title)
        return w


class _Frames:
    def send(self, fid, data, extended=False, fd=False):
        sent.append((fid, bytes(data)))


fake_sin.output = _Output()
fake_sin.ui = _UI()
fake_sin.frames = _Frames()
sys.modules["sin"] = fake_sin

import importlib.util

spec = importlib.util.spec_from_file_location("uds_main", os.path.join(PLUGIN_DIR, "main.py"))
main = importlib.util.module_from_spec(spec)
spec.loader.exec_module(main)


class Ctx:
    def __init__(self):
        self.frame_cb = None
        self.commands = []

    def on_frame(self, cb):
        self.frame_cb = cb

    def register_command(self, cid, handler, title):
        self.commands.append(cid)


class FakeFrame:
    def __init__(self, fid, data):
        self.id = fid
        self.data = data


ctx = Ctx()
print("[activate]")
main.activate(ctx)
_c = main._client
_orig_pdu = _c.on_pdu
_c.on_pdu = lambda resp, desc: (print("[client] RX:", resp.hex(),
                                       "pending:", _c._pending[0].hex()
                                       if _c._pending else None),
                                      _orig_pdu(resp, desc))[1]
_orig_req = _c.request


def _req_spy(pdu, functional=False, expect_response=True, on_done=None):
    print("[client] req:", pdu.hex(), "busy:", _c._pending is not None,
          "expect:", expect_response, "func:", functional)
    return _orig_req(pdu, functional, expect_response, on_done)


_c.request = _req_spy
_iso = main._isotp
_orig_sf_send = _iso.send


def _iso_spy(pdu, functional=False):
    print("[isotp] send:", pdu.hex(), "alive:", _iso._alive,
          "pending_tx:", bool(_iso._tx_payload))
    return _orig_sf_send(pdu, functional)


_iso.send = _iso_spy
check(ctx.commands == ["udsDiagnostic.open"], "命令注册")
win = None
for w in QApplication.topLevelWidgets():
    if isinstance(w, QMainWindow):
        win = w
        break
check(win is not None, "主窗口已创建")

# ---------------- 模拟 ECU ----------------
ecu = {"exp": 0, "buf": b"", "active": False, "fed": 0}


def ecu_feed(frame_fid, frame_data):
    """tester TX 帧 → 组包，完整 PDU 生成响应并注入插件"""
    if frame_fid != 0x7E0 or not frame_data:
        return
    kind = frame_data[0] & 0xF0
    if kind == 0x00:
        ecu_handle_pdu(frame_data[1:1 + (frame_data[0] & 0x0F)])
    elif kind == 0x10:
        ecu["exp"] = ((frame_data[0] & 0x0F) << 8) | frame_data[1]
        ecu["buf"] = frame_data[2:]
        ecu["active"] = True
        # ECU 立即回 FC CTS bs=0 stmin=0
        ctx.frame_cb(FakeFrame(0x7E8, bytes([0x30, 0, 0]) + b"\xCC" * 5))
    elif kind == 0x20:
        ecu["buf"] += frame_data[1:]
        if len(ecu["buf"]) >= ecu["exp"]:
            ecu["active"] = False
            ecu_handle_pdu(ecu["buf"][:ecu["exp"]])


def ecu_respond(resp):
    if not resp:
        return
    print("[ecu] respond:", resp.hex())
    if len(resp) <= 7:
        ctx.frame_cb(FakeFrame(0x7E8, (bytes([len(resp)]) + resp).ljust(8, b"\xCC")))
        return
    ff = (bytes([0x10 | (len(resp) >> 8), len(resp) & 0xFF]) + resp[:6]).ljust(8, b"\xCC")
    ctx.frame_cb(FakeFrame(0x7E8, ff))
    rest, sn = resp[6:], 1
    while rest:
        cf = (bytes([0x20 | sn]) + rest[:7]).ljust(8, b"\xCC")
        ctx.frame_cb(FakeFrame(0x7E8, cf))
        rest = rest[7:]
        sn = (sn + 1) & 0xF


def ecu_handle_pdu(pdu):
    pdus.append(bytes(pdu))
    sid = pdu[0]
    if sid == 0x10:
        ecu_respond(bytes([0x50, pdu[1], 0x00, 0x32, 0x01, 0x90]))
    elif sid == 0x27:
        if pdu[1] % 2 == 1:
            ecu_respond(bytes([0x67, pdu[1], 0x11, 0x22, 0x33, 0x44]))
        else:
            ecu_respond(bytes([0x67, pdu[1]]))
    elif sid == 0x22:
        did = pdu[1:2].hex().upper()
        if did.startswith("F1") and pdu[2] == 0x90:
            ecu_respond(bytes([0x62, 0xF1, 0x90]) + b"ABC")
        else:
            ecu_respond(bytes([0x7F, 0x22, 0x31]))
    elif sid == 0x19 and pdu[1] == 0x02:
        ecu_respond(bytes([0x59, 0x02, 0xFF, 0x01, 0x47, 0x2F, 0x09]))
    elif sid == 0x85:
        ecu_respond(bytes([0xC5, pdu[1]]))
    elif sid == 0x28:
        ecu_respond(bytes([0x68, pdu[1]]))
    elif sid == 0x34:
        ecu_respond(bytes([0x74, 0x20, 0x01, 0x00]))     # maxblk = 0x100
    elif sid == 0x36:
        ecu_respond(bytes([0x76, pdu[1]]))
    elif sid == 0x37:
        ecu_respond(bytes([0x77]))
    elif sid == 0x31:
        ecu_respond(bytes([0x71, pdu[1], pdu[2], pdu[3], 0x00]))
    else:
        ecu_respond(bytes([0x7F, sid, 0x11]))


def pump(seconds):
    end = time.time() + seconds
    idle = 0
    while time.time() < end:
        app.processEvents()
        if len(sent) > ecu["fed"]:
            for fid, data in sent[ecu["fed"]:]:
                ecu["fed"] += 1     # 先推进再处理：同步链里嵌套发出的新帧下轮可见
                ecu_feed(fid, data)
            idle = 0
        else:
            idle += 1
            if idle > 400:      # ~4s 无新帧 → 结束本轮
                break
        time.sleep(0.01)


def find_btn(text):
    for b in win.findChildren(QPushButton):
        if b.text() == text:
            return b
    return None


def table_by_header(first):
    for t in win.findChildren(QTableWidget):
        h = t.horizontalHeaderItem(0)
        if h and h.text() == first:
            return t
    return None


def edit_by_placeholder(key):
    for e in win.findChildren(QLineEdit):
        if key in (e.placeholderText() or "") or key in (e.text() or ""):
            return e
    return None


# ---------------- Tab1 服务：10 会话 ----------------
print("[dump] buttons:")
for b in win.findChildren(QPushButton):
    print("   BTN:", repr(b.text()), "enabled=", b.isEnabled())
print("[dump] trees:", len(win.findChildren(QTreeWidget)),
      "tables:", [t.horizontalHeaderItem(0).text() if t.horizontalHeaderItem(0) else "?"
                   for t in win.findChildren(QTableWidget)])
print("[Tab1 服务]")
tree = None
for t in win.findChildren(QTreeWidget):
    tree = t
    break
item = tree.topLevelItem(0).child(0)      # 10 会话控制
print("[dump] item:", repr(item.text(0)), "data=", item.data(0, 0x0100))
tree.setCurrentItem(item)
app.processEvents()
b = find_btn("发送请求")
print("[dump] send btn:", b, "maker ok")
b.click()
pump(0.5)
print("[dump] sent:", [(hex(f), d.hex()) for f, d in sent[:6]])
print("[dump] pdus:", [p.hex() for p in pdus[:6]])
check(any(p[:2] == b"\x10\x01" for p in pdus), "10 01 已发送")

# ---------------- Tab2 DID：全部读 ----------------
print("[Tab2 DID]")
find_btn("全部读").click()
pump(3)
check(any(p[:3] == b"\x22\xF1\x90" for p in pdus), "22 F190 已发送")
did_t = table_by_header("DID")
vin_row = None
for r in range(did_t.rowCount()):
    if did_t.item(r, 0).text() == "0xF190":
        vin_row = r
check(vin_row is not None and did_t.item(vin_row, 4).text() == "ABC",
      f"VIN 行解析值 = {did_t.item(vin_row, 4).text() if vin_row else '-'}")

# ---------------- Tab3 DTC：19 02 ----------------
print("[Tab3 DTC]")
find_btn("读 DTC 列表 (19 02)").click()
pump(0.5)
dtc_t = table_by_header("DTC")
check(dtc_t.rowCount() == 1 and dtc_t.item(0, 0).text() == "P0147"
      and "confirmedDTC" in dtc_t.item(0, 3).text(),
      f"DTC 表 {dtc_t.rowCount()} 行: {dtc_t.item(0, 0).text() if dtc_t.rowCount() else '-'}")

# ---------------- Tab4 安全访问 ----------------
print("[Tab4 安全]")
find_btn("请求种子 (27 odd)").click()
pump(0.5)
find_btn("发送密钥 (27 even)").click()
pump(0.5)
check(any(p[:2] == b"\x27\x01" for p in pdus), "27 01 已发送")
key_pdu = next((p for p in pdus if p[:2] == b"\x27\x02"), None)
check(key_pdu == bytes([0x27, 0x02, 0xB4, 0x87, 0x96, 0xE1]),
      f"27 02 密钥 = {key_pdu.hex().upper() if key_pdu else '-'}")

# ---------------- Tab5 刷写全流程 ----------------
print("[Tab5 刷写]")
fw_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_fw_test.bin")
with open(fw_path, "wb") as f:
    f.write(bytes((i * 7 + 3) & 0xFF for i in range(600)))   # 600B → 3 块(254/254/92)
fe = edit_by_placeholder("固件")
print("[dump] fe:", fe, repr(fe.placeholderText()) if fe else None,
      "file exists:", os.path.isfile(fw_path))
fe.setText(fw_path)
btn = find_btn("开始刷写")
print("[dump] flash btn:", btn)
btn.click()
app.processEvents()
print("[dump] after click sent tail:", [(hex(f), d.hex()) for f, d in sent[-4:]])
print("[dump] labels:", [lb.text() for lb in win.findChildren(QLabel)
                          if lb.text() and ("刷写" in lb.text() or "预检" in lb.text() or "传输" in lb.text())][:4])
pump(10)
print("[dump] all pdus:", [p.hex() for p in pdus])
seq = [p[:2] for p in pdus]
check(bytes([0x10, 0x02]) in seq, "预检 10 02（编程会话）")
check(any(p[:2] == b"\x27\x02" and len(p) == 6 and p[:2] == b"\x27\x02" for p in pdus), "预检 27 密钥")
check(bytes([0x85, 0x02]) in seq and bytes([0x28, 0x03]) in seq, "预检 85 02 + 28 03")
check(any(p[0] == 0x34 and len(p) == 11 for p in pdus), "34 请求（11 字节格式）")
blocks = [p for p in pdus if p[0] == 0x36]
check(len(blocks) == 3 and [b[1] for b in blocks] == [1, 2, 3],
      f"36 共 {len(blocks)} 块，序号 {[b[1] for b in blocks]}")
check(sum(len(b) - 2 for b in blocks) == 600, "36 数据总量 600B")
check(blocks and blocks[0][2:] == bytes((i * 7 + 3) & 0xFF for i in range(254)), "块数据内容一致")
check(any(p[0] == 0x37 for p in pdus), "37 退出")
check(any(p[:4] == bytes([0x31, 0x01, 0xFF, 0x01]) for p in pdus), "31 01 FF01 校验")
status = None
for lb in win.findChildren(QLabel):
    if "刷写" in lb.text() and ("完成" in lb.text() or "失败" in lb.text() or "传输" in lb.text()):
        status = lb.text()
        break
check(status and "完成" in status, f"状态: {status}")

print("[deactivate]")
main.deactivate()
check(True, "deactivate 无异常")

if os.path.isfile(fw_path):
    os.remove(fw_path)

print()
if fails:
    print(f"结果: {len(fails)} 项失败: {fails}")
    sys.exit(1)
print("结果: E2E 全部通过")
