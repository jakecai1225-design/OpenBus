#!/usr/bin/env python3
"""make_market.py — 生成本地设备市场（开发/演示用）

从 build/bin/drivers/<id>/ 打包三品牌 .odp 到 build/market/，计算 sha256，
结合内置设备图文数据生成 market.json（双索引：drivers + devices，方案 §8.1），
并复制 drivers/market/assets/ 占位图。正式发布时由 OSS 静态托管替换，
market.json 结构与字段保持一致。

用法:
    python scripts/make_market.py            # 输出 build/market/
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "scripts", "driver_tool.py")
OUT_DIR = os.path.join(ROOT, "build", "market")
ASSETS_SRC = os.path.join(ROOT, "drivers", "market", "assets")

# ---- 驱动包定义（id → 源目录部件名） ----
DRIVERS = ["zlg", "peak", "kvaser"]

# ---- 设备图文数据（市场编辑管线的数据源；方案 §8.4 详情内容规范） ----
DEVICES = [
    {
        "driverId": "zlg", "model": "USBCANFD-200U", "vendor": "ZLG", "type": 41,
        "summary": "2 通道 CAN FD 板卡，ZLG 旗舰入门款",
        "tags": ["canfd", "2ch", "usb"],
        "images": ["assets/zlg-usbcanfd.svg"],
        "intro": "## 产品定位\n\n"
                 "致远电子 USBCANFD-200U 是面向汽车电子开发与测试的双通道\n"
                 "CAN FD 采集卡，兼容 ZCANPRO 生态。\n\n"
                 "## 特性\n\n"
                 "- 2 × CAN FD 通道，仲裁段 1 Mbps / 数据段 8 Mbps\n"
                 "- 硬件时间戳，微秒级精度\n"
                 "- USB 2.0 供电，无需外接电源\n\n"
                 "## 接线\n\n"
                 "使用 DB9 端子接入总线，终端电阻 120Ω 按需短接。",
        "specs": {
            "channels": 2, "canFd": True, "maxBaud": "8 Mbps (数据段)",
            "timestamp": "硬件时间戳", "interface": "USB 2.0", "power": "USB 供电"
        },
    },
    {
        "driverId": "zlg", "model": "USBCANFD-100U", "vendor": "ZLG", "type": 42,
        "summary": "单通道 CAN FD 板卡，便携调试首选",
        "tags": ["canfd", "1ch", "usb"],
        "images": ["assets/zlg-usbcanfd.svg"],
        "intro": "## 产品定位\n\nUSBCANFD-100U 单通道版本，适合单总线便携调试。",
        "specs": {
            "channels": 1, "canFd": True, "maxBaud": "8 Mbps (数据段)",
            "timestamp": "硬件时间戳", "interface": "USB 2.0", "power": "USB 供电"
        },
    },
    {
        "driverId": "peak", "model": "PCAN-USB FD", "vendor": "PEAK System", "type": 84,
        "summary": "单通道 CAN FD，PCAN-Basic 生态标准卡",
        "tags": ["canfd", "1ch", "usb"],
        "images": ["assets/peak-pcan.svg"],
        "intro": "## 产品定位\n\nPEAK PCAN-USB FD 是广泛使用的单通道 CAN FD\n"
                 "分析卡，兼容 PCAN-Basic / PCAN-View 工具链。\n\n"
                 "## 特性\n\n"
                 "- CAN FD 仲裁段 1 Mbps / 数据段 8 Mbps\n"
                 "- 硬件时间戳（微秒）\n"
                 "- D-Sub 9 接口",
        "specs": {
            "channels": 1, "canFd": True, "maxBaud": "8 Mbps (数据段)",
            "timestamp": "硬件时间戳", "interface": "USB 2.0", "power": "USB 供电"
        },
    },
    {
        "driverId": "peak", "model": "PCAN-USB Pro FD", "vendor": "PEAK System", "type": 86,
        "summary": "双通道 CAN FD 专业卡，高精度时间戳",
        "tags": ["canfd", "2ch", "usb"],
        "images": ["assets/peak-pcan.svg"],
        "intro": "## 产品定位\n\nPCAN-USB Pro FD 双通道专业级 CAN FD 卡。",
        "specs": {
            "channels": 2, "canFd": True, "maxBaud": "8 Mbps (数据段)",
            "timestamp": "硬件时间戳", "interface": "USB 2.0", "power": "USB 供电"
        },
    },
    {
        "driverId": "kvaser", "model": "Leaf Light", "vendor": "Kvaser", "type": 2,
        "summary": "单通道经典 CAN，Kvaser 最畅销款",
        "tags": ["can", "1ch", "usb"],
        "images": ["assets/kvaser-leaf.svg"],
        "intro": "## 产品定位\n\nKvaser Leaf Light 是经典单通道 CAN 分析仪，\n"
                 "canlib 生态兼容性极佳。",
        "specs": {
            "channels": 1, "canFd": False, "maxBaud": "1 Mbps",
            "timestamp": "硬件时间戳", "interface": "USB 2.0", "power": "USB 供电"
        },
    },
    {
        "driverId": "kvaser", "model": "USBcan Hybrid", "vendor": "Kvaser", "type": 3,
        "summary": "双通道 CAN FD / LIN 混合接口卡",
        "tags": ["canfd", "2ch", "lin"],
        "images": ["assets/kvaser-leaf.svg"],
        "intro": "## 产品定位\n\nKvaser USBcan Hybrid 支持 CAN FD 与 LIN 双协议。",
        "specs": {
            "channels": 2, "canFd": True, "maxBaud": "8 Mbps (数据段)",
            "timestamp": "硬件时间戳", "interface": "USB 2.0", "power": "USB 供电"
        },
    },
]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run_tool(*args):
    r = subprocess.run([sys.executable, TOOL, *args],
                       capture_output=True, text=True, encoding="utf-8")
    try:
        return json.loads(r.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        return {"ok": False, "error": r.stdout + r.stderr}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    drivers_meta = []
    for did in DRIVERS:
        # 组包源 = driver.json（源码）+ dll（构建输出），避免目录重叠
        pkg = os.path.join(OUT_DIR, f".pkg_{did}")
        shutil.rmtree(pkg, ignore_errors=True)
        os.makedirs(pkg)
        shutil.copy(os.path.join(ROOT, "drivers", did, "driver.json"), pkg)
        dll = os.path.join(ROOT, "build", "bin", "drivers", did, f"driver_{did}.dll")
        if not os.path.isfile(dll):
            print(f"[FAIL] 缺少 {dll}（先构建）")
            return 1
        shutil.copy(dll, pkg)

        odp_name = f"{did}-driver_1.0.0_x64.odp"
        odp = os.path.join(OUT_DIR, odp_name)
        res = run_tool("pack", pkg, "-o", odp)
        shutil.rmtree(pkg, ignore_errors=True)
        if not res.get("ok"):
            print(f"[FAIL] pack {did}: {res.get('error')}")
            return 1

        drivers_meta.append({
            "id": did,
            "name": {"zlg": "ZLG 致远电子驱动",
                     "peak": "PEAK System PCAN 驱动",
                     "kvaser": "Kvaser CANLIB 驱动"}[did],
            "vendor": {"zlg": "ZLG", "peak": "PEAK System", "kvaser": "Kvaser"}[did],
            "version": "1.0.0",
            "package": odp_name,
            "sha256": sha256_file(odp),
            "size": os.path.getsize(odp),
            "minAppVersion": "0.1.0",
            "abiVersion": "1.0",
            "license": "Proprietary - 厂商授权条款",
            "updatedAt": "2026-08-18",
        })
        print(f"[OK] {odp_name} ({os.path.getsize(odp) // 1024} KB)")

    market = {
        "version": "2026.8.18",
        "updated": "2026-08-18",
        "base": ".",
        "drivers": drivers_meta,
        "devices": DEVICES,
    }
    out_json = os.path.join(OUT_DIR, "market.json")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(market, f, ensure_ascii=False, indent=2)
    print(f"[OK] {out_json}（{len(drivers_meta)} 驱动 / {len(DEVICES)} 设备）")

    # 复制占位图
    if os.path.isdir(ASSETS_SRC):
        dst = os.path.join(OUT_DIR, "assets")
        os.makedirs(dst, exist_ok=True)
        for fn in os.listdir(ASSETS_SRC):
            shutil.copy(os.path.join(ASSETS_SRC, fn), dst)
        print(f"[OK] assets/ ({len(os.listdir(ASSETS_SRC))} 个文件)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
