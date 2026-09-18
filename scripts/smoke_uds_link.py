#!/usr/bin/env python3
"""E2E: activate uds-suite, confirm subscribeFrames, push a FRAME_BATCH, shutdown."""
from __future__ import annotations

import json
import os
import pathlib
import struct
import subprocess
import sys
import time

import zmq


def encode_batch(frames):
    out = bytearray(b"OBUS")
    out += struct.pack("<HHI", 1, 1, len(frames))
    for f in frames:
        flags = 0x08 if f.get("direction") == "Tx" else 0
        data = bytes.fromhex(f["data"])
        out += struct.pack("<IBBBB", f["id"], flags, f["dlc"], f["channel"], len(data))
        out += struct.pack("<Q", int(f.get("timestamp", 0) * 1e9))
        out += data
    return bytes(out)


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    bin_dir = root / "build" / "bin"
    plugin_dir = bin_dir / "plugins" / "uds-suite"
    host_script = bin_dir / "scripts" / "sin_host.py"
    for name in ("sin_host.py", "plugin_tool.py"):
        (bin_dir / "scripts" / name).write_bytes((root / "scripts" / name).read_bytes())

    ctx = zmq.Context.instance()
    router = ctx.socket(zmq.ROUTER)
    router.bind("tcp://127.0.0.1:*")
    ctrl = router.getsockopt(zmq.LAST_ENDPOINT).decode()
    pub = ctx.socket(zmq.PUB)
    pub.bind("tcp://127.0.0.1:*")
    data_ep = pub.getsockopt(zmq.LAST_ENDPOINT).decode()

    env = os.environ.copy()
    env.update({
        "SIN_ZMQ_CTRL": ctrl,
        "SIN_ZMQ_DATA": data_ep,
        "SIN_SDK_DIR": str(bin_dir / "sdk"),
        "SIN_PLUGINS_DIR": str(bin_dir / "plugins"),
        "QT_QPA_PLATFORM": "offscreen",
    })
    p = subprocess.Popen([sys.executable, str(host_script)], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    router.setsockopt(zmq.RCVTIMEO, 15000)

    identity = None
    saw_sub = False
    activated = False
    deadline = time.time() + 25
    while time.time() < deadline:
        try:
            parts = router.recv_multipart()
        except zmq.Again:
            break
        identity = parts[0]
        msg = json.loads(parts[-1])
        method = msg.get("method")
        if method == "host.hello":
            router.send_multipart([identity, json.dumps({
                "jsonrpc": "2.0", "method": "activate", "id": 1,
                "params": {
                    "plugin": "uds-suite",
                    "directory": str(plugin_dir),
                    "main": "main.py",
                },
            }).encode()])
        elif method == "subscribeFrames":
            saw_sub = True
            print("OK subscribeFrames", msg.get("params"))
        elif method == "log":
            text = (msg.get("params") or {}).get("message", "")
            if "activated" in text:
                print("OK", text)
        elif "result" in msg and msg.get("id") == 1:
            activated = bool((msg.get("result") or {}).get("success"))
            print("OK activate result", msg.get("result"))
            # Give SUB a moment to connect, then publish one UDS-looking frame
            time.sleep(0.3)
            payload = encode_batch([{
                "id": 0x7E8, "dlc": 8, "channel": 1, "timestamp": 1.0,
                "data": "067ef10191010000", "direction": "Rx",
            }])
            pub.send_multipart([b"frames", payload])
            print("OK published FRAME_BATCH")
            time.sleep(0.5)
            router.send_multipart([identity, json.dumps({
                "jsonrpc": "2.0", "method": "shutdown", "params": {},
            }).encode()])
            break

    try:
        p.wait(timeout=8)
    except subprocess.TimeoutExpired:
        p.kill()

    err = p.stderr.read().decode("utf-8", "replace") if p.stderr else ""
    if err.strip():
        print("STDERR", err[-1200:])

    ok = activated and saw_sub
    print("RESULT", "PASS" if ok else "FAIL",
          "activated=", activated, "subscribe=", saw_sub, "exit=", p.returncode)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
