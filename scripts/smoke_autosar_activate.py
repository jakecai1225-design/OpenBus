# -*- coding: utf-8 -*-
"""Activate autosar-suite through sin_host (offscreen) and print errors."""
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
    host_script = bin_dir / "scripts" / "sin_host.py"
    if not host_script.is_file():
        host_script = root / "scripts" / "sin_host.py"

    for name in ("sin_host.py", "plugin_tool.py"):
        src = root / "scripts" / name
        dst = bin_dir / "scripts" / name
        if src.is_file() and dst.parent.is_dir():
            dst.write_bytes(src.read_bytes())

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
        "SIN_PLUGINS_DIR": os.environ.get(
            "SIN_PLUGINS_DIR", str(root / "plugins")),
        "QT_QPA_PLATFORM": "offscreen",
    })
    plugin_dir = pathlib.Path(env["SIN_PLUGINS_DIR"]) / "autosar-suite"
    if not plugin_dir.joinpath("main.py").is_file():
        print("missing", plugin_dir)
        return 1
    p = subprocess.Popen(
        [sys.executable, str(host_script)],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    router.setsockopt(zmq.RCVTIMEO, 30000)
    identity = None
    activated = False
    deadline = time.time() + 35

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
                    "plugin": "autosar-suite",
                    "directory": str(plugin_dir),
                    "main": "main.py",
                },
            }).encode()])
        elif method == "log":
            text = (msg.get("params") or {}).get("message", "")
            print("LOG", text[:500].replace("\n", " | "))
        elif method == "output.append":
            print("OUT", (msg.get("params") or {}).get("text", "")[:300])
        elif method and method.startswith("workspace."):
            # Reply to host queries so activate can proceed.
            rid = msg.get("id")
            if rid is not None:
                router.send_multipart([identity, json.dumps({
                    "jsonrpc": "2.0", "id": rid,
                    "result": str(root),
                }).encode()])
        elif "result" in msg and msg.get("id") == 1:
            activated = bool((msg.get("result") or {}).get("success"))
            print("ACTIVATE", msg.get("result"), "error", msg.get("error"))
            router.send_multipart([identity, json.dumps({
                "jsonrpc": "2.0", "method": "shutdown", "params": {},
            }).encode()])
            break
        elif "error" in msg and msg.get("id") == 1:
            print("ACTIVATE ERROR", msg.get("error"))
            break

    try:
        p.wait(timeout=8)
    except subprocess.TimeoutExpired:
        p.kill()
    err = p.stderr.read().decode("utf-8", "replace") if p.stderr else ""
    if err.strip():
        print("STDERR", err[-4000:])
    print("activated=", activated)
    return 0 if activated else 1


if __name__ == "__main__":
    sys.exit(main())
