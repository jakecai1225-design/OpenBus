#!/usr/bin/env python3
"""make_market.py — 生成本地统一插件市场（开发/演示用）

v2（方案 §13.4）：market.json schema 2 ——
  - drivers[] 按驱动聚合：内嵌 devices 简表（型号/通道/CAN FD/规格）+
    icon/image/summary/readme（markdown 内联）/keywords；顶层 devices[] 退役
  - 新增 plugins[]：扫描 plugins/ 全部插件，plugin_tool.py 打包 .opk 到
    opk/，元数据 + 内置中文标题/标签补充
驱动 .odp 组包源 = 源码 driver.json + build dll（同 v1）。正式发布时由 OSS
静态托管替换，结构与字段保持一致。

用法:
    python scripts/make_market.py            # 输出 build/market/
"""

import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRIVER_TOOL = os.path.join(ROOT, "scripts", "driver_tool.py")
PLUGIN_TOOL = os.path.join(ROOT, "scripts", "plugin_tool.py")
OUT_DIR = os.path.join(ROOT, "build", "market")
ASSETS_SRC = os.path.join(ROOT, "drivers", "market", "assets")
PLUGINS_SRC = os.path.join(ROOT, "plugins")
TODAY = "2026-08-18"

# ---- 驱动市场元数据（图文与说明为市场编辑管线的数据源；方案 §13.4） ----
DRIVER_META = {
    "zlg": {
        "name": "ZLG 致远电子驱动",
        "vendor": "ZLG",
        "summary": "ZLG USBCAN / USBCANFD 全系列 USB-CAN 接口卡驱动",
        "icon": "assets/zlg-usbcanfd.svg",
        "image": "assets/zlg-usbcanfd.svg",
        "keywords": "zlg 致远 usbcan usbcanfd 200u 100u 800u mini canfd",
        "readme": (
            "## 概述\n\n"
            "ZLG 致远电子 USBCAN / USBCANFD 系列 USB-CAN 接口卡驱动插件，"
            "覆盖经典 USBCAN-I/II 与 USBCANFD 全系常用型号。\n\n"
            "## 特性\n\n"
            "- USBCANFD 系列：仲裁段 1 Mbps / 数据段 8 Mbps，硬件微秒级时间戳\n"
            "- 经典 USBCAN 系列：1 Mbps\n"
            "- USB 供电，免外接电源\n\n"
            "## 厂商 SDK\n\n"
            "依赖 zlgcan.dll（厂商 SDK，随包不分发）。请先安装 ZLG 官方"
            "驱动包，或将 zlgcan.dll 置于驱动目录 vendor/ 下。\n\n"
            "## 授权\n\n"
            "驱动插件遵循 openbus 插件条款；ZLG SDK 归致远电子所有。"
        ),
    },
    "peak": {
        "name": "PEAK System PCAN 驱动",
        "vendor": "PEAK System",
        "summary": "PEAK PCAN-USB 系列 CAN / CAN FD 接口卡驱动",
        "icon": "assets/peak-pcan.svg",
        "image": "assets/peak-pcan.svg",
        "keywords": "peak pcan pcan-usb pcan light pro fd canfd",
        "readme": (
            "## 概述\n\n"
            "PEAK System PCAN-USB 系列 CAN / CAN FD 接口卡驱动插件，"
            "兼容 PCAN-Basic / PCAN-View 工具链生态。\n\n"
            "## 特性\n\n"
            "- PCAN-USB：1 Mbps 经典 CAN\n"
            "- PCAN-USB FD / Pro FD：数据段最高 8 Mbps，硬件微秒级时间戳\n"
            "- D-Sub 9 标准接口，USB 供电\n\n"
            "## 厂商 SDK\n\n"
            "依赖 PCAN-Basic DLL（厂商 SDK，随包不分发）。请先安装 PEAK "
            "官方驱动包，或将 DLL 置于驱动目录 vendor/ 下。"
        ),
    },
    "kvaser": {
        "name": "Kvaser CANLIB 驱动",
        "vendor": "Kvaser",
        "summary": "Kvaser Leaf / USBcan 系列 CAN / CAN FD 接口卡驱动",
        "icon": "assets/kvaser-leaf.svg",
        "image": "assets/kvaser-leaf.svg",
        "keywords": "kvaser canlib leaf usbcan hybrid canfd lin",
        "readme": (
            "## 概述\n\n"
            "Kvaser Leaf / USBcan 系列 CAN / CAN FD 接口卡驱动插件，"
            "canlib 生态兼容性极佳。\n\n"
            "## 特性\n\n"
            "- Leaf 系列：单通道经典 CAN，1 Mbps\n"
            "- USBcan Hybrid：CAN FD + LIN 双协议\n"
            "- canlib 完整 API 生态\n\n"
            "## 厂商 SDK\n\n"
            "依赖 canlib32.dll（厂商 SDK，随包不分发）。请先安装 Kvaser "
            "官方驱动包，或将 DLL 置于驱动目录 vendor/ 下。"
        ),
    },
}

# ---- 型号规格补充（有图文数据的 6 款；其余走 DEFAULT_SPEC） ----
DEVICE_META = {
    "USBCANFD-200U": {"summary": "2 通道 CAN FD 板卡，ZLG 旗舰入门款",
                      "maxBaud": "8 Mbps (数据段)", "timestamp": "硬件时间戳"},
    "USBCANFD-100U": {"summary": "单通道 CAN FD 板卡，便携调试首选",
                      "maxBaud": "8 Mbps (数据段)", "timestamp": "硬件时间戳"},
    "PCAN-USB FD": {"summary": "单通道 CAN FD，PCAN-Basic 生态标准卡",
                    "maxBaud": "8 Mbps (数据段)", "timestamp": "硬件时间戳"},
    "PCAN-USB Pro FD": {"summary": "双通道 CAN FD 专业卡，高精度时间戳",
                        "maxBaud": "8 Mbps (数据段)", "timestamp": "硬件时间戳"},
    "Leaf": {"summary": "单通道经典 CAN，Kvaser 最畅销款",
             "maxBaud": "1 Mbps", "timestamp": "硬件时间戳"},
    "USBcan Hybrid": {"summary": "双通道 CAN FD / LIN 混合接口卡",
                      "maxBaud": "8 Mbps (数据段)", "timestamp": "硬件时间戳"},
}
DEFAULT_SPEC = {"summary": "", "maxBaud": "1 Mbps", "timestamp": "软件时间戳"}

# ---- 插件市场元数据补充（标题/标签/关键词/readme 扩展） ----
PLUGIN_META = {
    "blf-converter": {
        "title": "BLF 日志转换器",
        "tags": ["日志", "转换"],
        "keywords": "blf asc csv pcap trc 日志 转换 vector",
        "readme_extra": (
            "\n\n## 功能\n\n"
            "- BLF / ASC / CSV / PCAP / TRC 五种格式互转\n"
            "- 支持通道映射与时间基准设置"
        ),
    },
    "bus-statistics": {
        "title": "总线统计分析",
        "tags": ["分析", "统计"],
        "keywords": "统计 bus load 负载 帧 频率 字节",
        "readme_extra": "\n\n## 功能\n\n- 按 ID 统计帧数/字节/频率/负载\n- 总线负载率实时计算",
    },
    "canopen-explorer": {
        "title": "CANopen 总线浏览器",
        "tags": ["协议", "CANopen"],
        "keywords": "canopen cia301 nmt sdo pdo heartbeat",
        "readme_extra": "\n\n## 功能\n\n- NMT 节点控制 / SDO 读写\n- Heartbeat 监测 / 对象字典浏览",
    },
    "dbc-tool": {
        "title": "DBC 查看编辑器",
        "tags": ["数据库", "DBC"],
        "keywords": "dbc 数据库 信号 报文 编辑 c 代码生成",
        "readme_extra": "\n\n## 功能\n\n- 报文/信号浏览与信号编辑\n- 属性查看 / 导出 C++ 解析代码",
    },
    "uds-diagnostic": {
        "title": "UDS 诊断客户端",
        "tags": ["诊断", "UDS"],
        "keywords": "uds 14229 isotp 诊断 did dtc 刷写 34 36 37",
        "readme_extra": (
            "\n\n## 功能\n\n"
            "- ISO-TP 传输层（BS/STmin/FC 状态机，P2/P2* 超时与 0x78 延续）\n"
            "- 会话/安全访问、DID 读写/周期查询、DTC 三列表\n"
            "- 刷写算法（34/36/37）对标 ZCANPro / CANoe / TSMaster"
        ),
    },
}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run_tool(tool, *args):
    r = subprocess.run([sys.executable, tool, *args],
                       capture_output=True, text=True, encoding="utf-8")
    try:
        return json.loads(r.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        return {"ok": False, "error": r.stdout + r.stderr}


def load_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def build_driver(did):
    """组包一个驱动 .odp 并返回市场条目（schema 2，按驱动聚合）"""
    src_json = os.path.join(ROOT, "drivers", did, "driver.json")
    drv = load_json(src_json)
    version = drv.get("version", "1.0.0")

    # 组包源 = driver.json（源码）+ dll（构建输出），避免目录重叠
    pkg = os.path.join(OUT_DIR, f".pkg_{did}")
    shutil.rmtree(pkg, ignore_errors=True)
    os.makedirs(pkg)
    shutil.copy(src_json, pkg)
    dll = os.path.join(ROOT, "build", "bin", "drivers", did, f"driver_{did}.dll")
    if not os.path.isfile(dll):
        print(f"[FAIL] 缺少 {dll}（先构建）")
        return None
    shutil.copy(dll, pkg)

    meta = DRIVER_META[did]
    odp_name = f"{did}-driver_{version}_x64.odp"
    odp = os.path.join(OUT_DIR, odp_name)
    res = run_tool(DRIVER_TOOL, "pack", pkg, "-o", odp)
    shutil.rmtree(pkg, ignore_errors=True)
    if not res.get("ok"):
        print(f"[FAIL] pack {did}: {res.get('error')}")
        return None
    print(f"[OK] {odp_name} ({os.path.getsize(odp) // 1024} KB)")

    # devices 简表：driver.json devices[] + 规格/摘要补充
    devices = []
    for d in drv.get("devices", []):
        spec = DEVICE_META.get(d.get("name"), DEFAULT_SPEC)
        entry = {
            "model": d.get("name", ""),
            "channels": d.get("channels", 1),
            "canFd": bool(d.get("canFd", False)),
            "maxBaud": spec["maxBaud"],
            "timestamp": spec["timestamp"],
        }
        if spec.get("summary"):
            entry["summary"] = spec["summary"]
        devices.append(entry)

    return {
        "id": did,
        "name": meta["name"],
        "vendor": meta["vendor"],
        "version": version,
        "package": odp_name,
        "sha256": sha256_file(odp),
        "size": os.path.getsize(odp),
        "icon": meta["icon"],
        "image": meta["image"],
        "summary": meta["summary"],
        "readme": meta["readme"],
        "keywords": meta["keywords"],
        "devices": devices,
        "minAppVersion": "0.9.0",
        "abiVersion": "1.0",
        "license": "Proprietary - 厂商授权条款",
        "updatedAt": TODAY,
    }


def build_plugin(pdir):
    """打包一个插件 .opk 并返回市场条目"""
    manifest = load_json(os.path.join(pdir, "plugin.json"))
    pid = manifest["name"]
    version = manifest.get("version", "0.0.0")
    meta = PLUGIN_META.get(pid, {})

    opk_dir = os.path.join(OUT_DIR, "opk")
    os.makedirs(opk_dir, exist_ok=True)
    opk_name = f"{pid}_{version}.opk"
    opk = os.path.join(opk_dir, opk_name)
    res = run_tool(PLUGIN_TOOL, "pack", pdir, "-o", opk)
    if not res.get("ok"):
        print(f"[FAIL] pack {pid}: {res.get('error')}")
        return None
    print(f"[OK] opk/{opk_name} ({os.path.getsize(opk) // 1024} KB)")

    # 市场图标：复制插件 icon.svg 到 assets/plugin-<id>.svg
    icon_rel = ""
    icon_src = os.path.join(pdir, manifest.get("icon", "icon.svg"))
    if os.path.isfile(icon_src):
        dst_assets = os.path.join(OUT_DIR, "assets")
        os.makedirs(dst_assets, exist_ok=True)
        shutil.copy(icon_src, os.path.join(dst_assets, f"plugin-{pid}.svg"))
        icon_rel = f"assets/plugin-{pid}.svg"

    readme = manifest.get("description", "") + meta.get("readme_extra", "")
    return {
        "id": pid,
        "name": meta.get("title", pid),
        "publisher": manifest.get("author", "openbus"),
        "version": version,
        "description": manifest.get("description", ""),
        "icon": icon_rel,
        "package": f"opk/{opk_name}",
        "sha256": sha256_file(opk),
        "size": os.path.getsize(opk),
        "readme": readme,
        "tags": meta.get("tags", []),
        "keywords": meta.get("keywords", pid),
        "minAppVersion": "0.9.0",
        "updatedAt": TODAY,
    }


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    drivers_meta = []
    for did in DRIVER_META:
        entry = build_driver(did)
        if entry is None:
            return 1
        drivers_meta.append(entry)

    plugins_meta = []
    for pdir in sorted(glob.glob(os.path.join(PLUGINS_SRC, "*"))):
        if not os.path.isfile(os.path.join(pdir, "plugin.json")):
            continue
        entry = build_plugin(pdir)
        if entry is None:
            return 1
        plugins_meta.append(entry)

    market = {
        "schema": 2,
        "version": "2026.8.18",
        "updated": TODAY,
        "base": ".",
        "drivers": drivers_meta,
        "plugins": plugins_meta,
    }
    out_json = os.path.join(OUT_DIR, "market.json")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(market, f, ensure_ascii=False, indent=2)
    n_dev = sum(len(d["devices"]) for d in drivers_meta)
    print(f"[OK] {out_json}"
          f"（{len(drivers_meta)} 驱动/{n_dev} 型号, {len(plugins_meta)} 插件）")

    # 复制驱动占位图
    if os.path.isdir(ASSETS_SRC):
        dst = os.path.join(OUT_DIR, "assets")
        os.makedirs(dst, exist_ok=True)
        for fn in os.listdir(ASSETS_SRC):
            shutil.copy(os.path.join(ASSETS_SRC, fn), dst)
        print(f"[OK] assets/ ({len(os.listdir(dst))} 个文件)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
