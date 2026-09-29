#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build a local market.json that only lists domain suites (+ optional AI Agent).

Keeps driver entries from the remote catalog (absolute package URLs) so hardware
install still works. Replaces plugins[] with suites packed by pack-suites.

Usage:
    python scripts/make_market.py
    python scripts/make_market.py --remote http://sin.org.cn/market/market.json
    python scripts/make_market.py --out build/bin/market
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import urllib.request
from datetime import date

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

# Marketplace ships domain suites only (S5). AI Agent is Phase 1 MVP.
SUITE_CATALOG = [
    {
        "id": "uds-suite",
        "name": "UDS Suite",
        "description": "Diagnose, Scan, Batch, Security, Profiles and shared log (ISO 14229).",
        "tags": ["uds", "diagnostic", "isotp"],
        "keywords": "uds diagnose scan batch security flash",
    },
    {
        "id": "dbc-studio",
        "name": "DBC Studio",
        "description": "Edit, validate, compare, merge and export DBC databases.",
        "tags": ["dbc", "database"],
        "keywords": "dbc lint diff merge export codegen",
    },
    {
        "id": "eds-studio",
        "name": "EDS Studio",
        "description": "Edit, validate, compare and export CANopen EDS/DCF files.",
        "tags": ["canopen", "eds", "dcf"],
        "keywords": "eds dcf canopen validate export pdo",
    },
    {
        "id": "autosar-suite",
        "name": "AUTOSAR Studio",
        "description": "Unified AUTOSAR CP workbench: BSW module config (Os/Com/PduR/Dcm/…), live COM/NM/E2E/SecOC, project ARXML (no stack codegen).",
        "tags": ["autosar", "arxml", "bsw", "com"],
        "keywords": "autosar arxml bsw dbc validate com nm e2e secoc extract ecuc studio",
    },
    {
        "id": "canopen-suite",
        "name": "CANopen Suite",
        "description": "Template/profile EDS workbench, live OD vs EDS, Codegen, Network/NMT/PDO, CiA profiles.",
        "tags": ["canopen"],
        "keywords": "canopen eds od sdo nmt",
    },
    {
        "id": "j1939-suite",
        "name": "J1939 Suite",
        "description": "PGN/SPN analysis, DM1/DM2, Address Claim, TP and RQST.",
        "tags": ["j1939"],
        "keywords": "j1939 pgn spn dm1 tp",
    },
    {
        "id": "obd-suite",
        "name": "OBD Suite",
        "description": "OBD-II Mode 01-09 scanner, freeze frame, DTC and PID history.",
        "tags": ["obd", "diagnostic"],
        "keywords": "obd obd2 pid dtc",
    },
    {
        "id": "tx-lab",
        "name": "TX Lab",
        "description": "Periodic TX, Restbus simulation and live dashboard.",
        "tags": ["tx", "restbus"],
        "keywords": "frame generator simulator dashboard restbus",
    },
    {
        "id": "bus-security",
        "name": "Bus Security",
        "description": "Fuzzer, IDS, stress and E2E checksum in one suite.",
        "tags": ["security"],
        "keywords": "fuzzer ids stress e2e",
    },
    {
        "id": "protocol-hub",
        "name": "Protocol Hub",
        "description": "NM, ISO-TP, ISOBUS, NMEA2000, GBT27930 and XCP monitors.",
        "tags": ["protocol"],
        "keywords": "nm isotp isobus nmea2000 gbt27930 xcp",
    },
    {
        "id": "log-analysis",
        "name": "Log & Compare",
        "description": "Log toolkit, compare, trigger, quality, reverse and ID scan.",
        "tags": ["log", "analysis"],
        "keywords": "log compare trigger quality reverse id-scan",
    },
    {
        "id": "bus-utilities",
        "name": "Bus Utilities",
        "description": "Bit timing calculator and CAN gateway rules.",
        "tags": ["tools"],
        "keywords": "bit timing gateway",
    },
    {
        "id": "ethercat-suite",
        "name": "EtherCAT Suite",
        "description": "ESI, topology, PDO process image, CoE SDO, DC timing and datagram decode.",
        "tags": ["ethercat", "coe", "pdo"],
        "keywords": "ethercat esi coe pdo dc mailbox datagram",
    },
    {
        "id": "ai-agent",
        "name": "AI Agent",
        "description": "Read-only Trace/DBC tool loop with OpenAI-compatible providers.",
        "tags": ["ai", "agent"],
        "keywords": "ai agent llm ollama frames stats",
    },
]


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_remote(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=30) as resp:
        raw = resp.read()
    return json.loads(raw.decode("utf-8"))


# Driver market tile icons (same 64×64 rounded-rect language as suite plugins).
DRIVER_ICON_FILES = {
    "zlg": "zlg-usbcanfd.svg",
    "peak": "peak-pcan.svg",
    "kvaser": "kvaser-leaf.svg",
    "slcan": "slcan-serial.svg",
    "candle": "candle-usb.svg",
}


def localize_drivers(drivers: list, remote_base: str, assets_dir: str) -> list:
    """Keep remote package/readme URLs; ship local tile icons like plugins."""
    base = remote_base.rstrip("/") + "/"
    src_dir = os.path.join(ROOT, "drivers", "market", "assets")
    out = []
    for d in drivers:
        item = dict(d)
        sid = (item.get("id") or "").lower()
        icon_name = DRIVER_ICON_FILES.get(sid)
        if icon_name:
            src = os.path.join(src_dir, icon_name)
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(assets_dir, icon_name))
                # Same tile for card + detail — no 16:9 product banner.
                item["icon"] = "assets/%s" % icon_name
                item.pop("image", None)
                print("driver icon", sid, icon_name)

        for key in ("package", "readme"):
            val = item.get(key)
            if not val or not isinstance(val, str):
                continue
            if val.startswith(("http://", "https://", "file:")):
                continue
            item[key] = base + val.lstrip("./")
        # Leave any leftover absolute icon/image alone only if we had no local tile.
        if not icon_name or not os.path.isfile(
                os.path.join(src_dir, icon_name or "")):
            for key in ("icon", "image"):
                val = item.get(key)
                if not val or not isinstance(val, str):
                    continue
                if val.startswith(("http://", "https://", "file:")):
                    continue
                item[key] = base + val.lstrip("./")
        out.append(item)
    return out


def pack_one(plugin_dir: str, out_opk: str) -> None:
    sys.path.insert(0, os.path.dirname(__file__))
    import plugin_tool

    path, _name, _ver, err = plugin_tool._write_opk(plugin_dir, out_opk)
    if err:
        raise RuntimeError("%s: %s" % (plugin_dir, err))
    if path != os.path.abspath(out_opk):
        shutil.copy2(path, out_opk)


def build_market(out_dir: str, remote_url: str, opk_src: str) -> str:
    remote = load_remote(remote_url)
    remote_dir = remote_url.rsplit("/", 1)[0]

    market_dir = os.path.abspath(out_dir)
    opk_dir = os.path.join(market_dir, "opk")
    assets_dir = os.path.join(market_dir, "assets")
    os.makedirs(opk_dir, exist_ok=True)
    os.makedirs(assets_dir, exist_ok=True)

    plugins = []
    today = date.today().isoformat()
    for meta in SUITE_CATALOG:
        sid = meta["id"]
        plugin_dir = os.path.join(ROOT, "plugins", sid)
        manifest_path = os.path.join(plugin_dir, "plugin.json")
        if not os.path.isfile(manifest_path):
            print("skip missing", sid)
            continue
        with open(manifest_path, encoding="utf-8") as f:
            manifest = json.load(f)
        version = manifest.get("version", "0.0.0")
        opk_name = "%s_%s.opk" % (sid, version)
        out_opk = os.path.join(opk_dir, opk_name)

        # Prefer already packed file from dist/opk, else pack now.
        src_opk = os.path.join(opk_src, opk_name)
        if os.path.isfile(src_opk):
            shutil.copy2(src_opk, out_opk)
        else:
            pack_one(plugin_dir, out_opk)

        icon_src = os.path.join(plugin_dir, "icon.svg")
        icon_rel = ""
        if os.path.isfile(icon_src):
            # Canonical market path matches appstore build-market.mjs
            icon_name = "plugin-%s.svg" % sid
            shutil.copy2(icon_src, os.path.join(assets_dir, icon_name))
            # Keep bare alias for older clients (same bytes)
            shutil.copy2(icon_src, os.path.join(assets_dir, "%s.svg" % sid))
            icon_rel = "assets/%s" % icon_name

        plugins.append({
            "id": sid,
            "name": meta["name"],
            "publisher": "sin",
            "version": version,
            "description": meta["description"],
            "icon": icon_rel,
            "package": "opk/%s" % opk_name,
            "sha256": sha256_file(out_opk),
            "size": os.path.getsize(out_opk),
            "readme": "## %s\n\n%s\n" % (meta["name"], meta["description"]),
            "tags": meta["tags"],
            "keywords": meta["keywords"],
            "minAppVersion": "0.1.0",
            "updatedAt": today,
        })
        print("plugin", sid, version, opk_name)

    # Drop stale / thin .opk leftovers (old versions, non-suite packages).
    keep_names = {os.path.basename(p["package"]) for p in plugins}
    for name in os.listdir(opk_dir):
        if not name.endswith(".opk"):
            continue
        if name not in keep_names:
            stale = os.path.join(opk_dir, name)
            os.remove(stale)
            print("removed stale", name)

    market = {
        "schema": 2,
        "version": today.replace("-", "."),
        "updated": today,
        "base": ".",
        "drivers": localize_drivers(
            remote.get("drivers") or [], remote_dir, assets_dir),
        "plugins": plugins,
    }
    out_json = os.path.join(market_dir, "market.json")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(market, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("wrote", out_json, "drivers=%d plugins=%d" % (
        len(market["drivers"]), len(market["plugins"])))
    return out_json


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--remote",
        default="http://sin.org.cn/market/market.json",
        help="Remote market.json used for drivers[] only",
    )
    ap.add_argument(
        "--out",
        default=os.path.join(ROOT, "build", "bin", "market"),
        help="Output market directory (market.json + opk/ + assets/)",
    )
    ap.add_argument(
        "--opk-src",
        default=os.path.join(ROOT, "dist", "opk"),
        help="Directory with already packed suite .opk files",
    )
    ap.add_argument(
        "--also-repo",
        action="store_true",
        default=True,
        help="Also sync <repo>/market (default: on)",
    )
    ap.add_argument(
        "--no-also-repo",
        action="store_true",
        help="Do not sync <repo>/market",
    )
    args = ap.parse_args()

    # Ensure suite packages exist.
    sys.path.insert(0, os.path.dirname(__file__))
    import plugin_tool
    os.makedirs(args.opk_src, exist_ok=True)
    plugin_tool.cmd_pack_suites(["-o", args.opk_src])
    # Pack AI Agent if present (not in SUITE_IDS).
    ai_dir = os.path.join(ROOT, "plugins", "ai-agent")
    if os.path.isdir(ai_dir):
        with open(os.path.join(ai_dir, "plugin.json"), encoding="utf-8") as f:
            ver = json.load(f).get("version", "0.1.0")
        out_ai = os.path.join(args.opk_src, "ai-agent_%s.opk" % ver)
        pack_one(ai_dir, out_ai)
        # Drop older AI Agent packages in opk-src.
        for name in os.listdir(args.opk_src):
            if name.startswith("ai-agent_") and name.endswith(".opk") and name != os.path.basename(out_ai):
                os.remove(os.path.join(args.opk_src, name))
                print("removed stale", name)

    path = build_market(args.out, args.remote, args.opk_src)
    if args.also_repo and not args.no_also_repo:
        repo_market = os.path.join(ROOT, "market")
        if os.path.isdir(repo_market):
            shutil.rmtree(repo_market)
        shutil.copytree(args.out, repo_market)
        print("copied", repo_market)
    # Dev layout: exe often resolves ../market first (build/market from build/bin).
    build_market_dir = os.path.join(ROOT, "build", "market")
    if os.path.isdir(os.path.dirname(build_market_dir)):
        if os.path.isdir(build_market_dir):
            shutil.rmtree(build_market_dir)
        shutil.copytree(args.out, build_market_dir)
        print("copied", build_market_dir)
    print("OK", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
