#!/usr/bin/env python3
"""Smoke test: binary codec + ZMQ host.hello / shutdown."""
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "sdk"))
from sin._databus import decode_frame_batch  # noqa: E402


def enc(frames):
    out = bytearray(b"OBUS")
    out += struct.pack("<HHI", 1, 1, len(frames))
    for f in frames:
        flags = 0
        if f.get("extended"):
            flags |= 0x80
        if f.get("fd"):
            flags |= 0x40
        if f.get("direction") == "Tx":
            flags |= 0x08
        data = bytes.fromhex(f["data"]) if isinstance(f["data"], str) else f["data"]
        out += struct.pack("<IBBBB", f["id"], flags, f["dlc"], f["channel"], len(data))
        out += struct.pack("<Q", int(f["timestamp"] * 1e9))
        out += data
    return bytes(out)


def main():
    import zmq

    payload = enc([{
        "id": 0x123, "extended": False, "fd": False, "dlc": 2, "channel": 1,
        "data": "0102", "timestamp": 1.5, "direction": "Rx",
    }])
    decoded = decode_frame_batch(payload)
    assert len(decoded) == 1 and decoded[0]["id"] == 0x123 and decoded[0]["data"] == "0102"
    print("OK decode_frame_batch")

    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    ctx = zmq.Context.instance()
    router = ctx.socket(zmq.ROUTER)
    router.bind("tcp://127.0.0.1:*")
    ctrl = router.getsockopt(zmq.LAST_ENDPOINT).decode()
    pub = ctx.socket(zmq.PUB)
    pub.bind("tcp://127.0.0.1:*")
    data_ep = pub.getsockopt(zmq.LAST_ENDPOINT).decode()

    env = os.environ.copy()
    env["SIN_ZMQ_CTRL"] = ctrl
    env["SIN_ZMQ_DATA"] = data_ep
    env["SIN_SDK_DIR"] = os.path.join(root, "sdk")

    host_script = os.path.join(root, "scripts", "sin_host.py")
    p = subprocess.Popen(
        [sys.executable, host_script],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    router.setsockopt(zmq.RCVTIMEO, 5000)
    try:
        parts = router.recv_multipart()
    except zmq.Again:
        err = p.stderr.read().decode("utf-8", "replace") if p.stderr else ""
        p.kill()
        raise SystemExit(f"timeout waiting host.hello\nstderr={err}")

    msg = json.loads(parts[-1])
    assert msg.get("method") == "host.hello", msg
    print("OK host.hello", msg.get("params"))

    router.send_multipart([
        parts[0],
        json.dumps({"jsonrpc": "2.0", "method": "shutdown", "params": {}}).encode(),
    ])
    try:
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        p.kill()
        raise SystemExit("host did not exit")
    print("OK host exit", p.returncode)
    print("SMOKE PASS")


if __name__ == "__main__":
    main()
