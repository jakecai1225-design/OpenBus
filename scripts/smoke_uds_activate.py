#!/usr/bin/env python3
"""Activate uds-diagnostic through sin_host over ZMQ (offscreen Qt)."""
from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import time

import zmq


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    bin_dir = root / "build" / "bin"
    plugin_dir = bin_dir / "plugins" / "uds-diagnostic"
    host_script = bin_dir / "scripts" / "sin_host.py"
    assert plugin_dir.joinpath("main.py").is_file(), plugin_dir
    assert host_script.is_file(), host_script

    # Keep build/bin scripts in sync with repo
    for name in ("sin_host.py", "plugin_tool.py"):
        src = root / "scripts" / name
        dst = bin_dir / "scripts" / name
        dst.write_bytes(src.read_bytes())

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
    env["SIN_SDK_DIR"] = str(bin_dir / "sdk")
    env["SIN_PLUGINS_DIR"] = str(bin_dir / "plugins")
    env["QT_QPA_PLATFORM"] = "offscreen"

    p = subprocess.Popen(
        [sys.executable, str(host_script)],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    router.setsockopt(zmq.RCVTIMEO, 15000)
    identity = None
    activated = False
    logs: list[str] = []
    deadline = time.time() + 20

    while time.time() < deadline:
        try:
            parts = router.recv_multipart()
        except zmq.Again:
            break
        identity = parts[0]
        msg = json.loads(parts[-1])
        method = msg.get("method")
        if method == "host.hello":
            print("HELLO", msg.get("params"))
            act = {
                "jsonrpc": "2.0",
                "method": "activate",
                "params": {
                    "plugin": "uds-diagnostic",
                    "directory": str(plugin_dir),
                    "main": "main.py",
                },
                "id": 1,
            }
            router.send_multipart([identity, json.dumps(act).encode()])
        elif method == "log":
            text = (msg.get("params") or {}).get("message", "")
            logs.append(text)
            print("LOG", text[:300].replace("\n", " | "))
        elif method == "registerCommand":
            print("CMD", msg.get("params"))
        elif "result" in msg or "error" in msg:
            print("RESP", msg)
            if msg.get("id") == 1:
                if msg.get("result", {}).get("success"):
                    activated = True
                router.send_multipart([
                    identity,
                    json.dumps({"jsonrpc": "2.0", "method": "shutdown", "params": {}}).encode(),
                ])
                break
        else:
            print("MSG", method)

    try:
        p.wait(timeout=8)
    except subprocess.TimeoutExpired:
        p.kill()
        print("killed host")

    err = p.stderr.read().decode("utf-8", "replace") if p.stderr else ""
    if err.strip():
        print("STDERR", err[-2000:])

    ok_log = any("uds-diagnostic" in x and "activated" in x for x in logs)
    print("RESULT activated=", activated, "ok_log=", ok_log, "exit=", p.returncode)
    return 0 if (activated and ok_log) else 1


if __name__ == "__main__":
    sys.exit(main())
