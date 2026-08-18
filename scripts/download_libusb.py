#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
下载 libusb-1.0.dll (Windows x64) → driver/libusb-1.0.dll

candle/GS_USB 驱动（方案 §14.5 P0-B）运行时动态加载 libusb-1.0.dll，
搜索顺序：drivers/candle/vendor → 应用目录 → 系统路径。
本脚本把 DLL 放入项目根 driver/ 目录（CMake POST_BUILD 会整目录拷贝到
exe 同级，见 src/CMakeLists.txt "驱动 DLL" 段）。

来源：PyPI 官方镜像的 libusb1 / libusb-package wheel（wheel 即 zip，
内含 libusb 官方构建的 libusb-1.0.dll），仅用标准库，无需第三方依赖。

手动替代：https://libusb.info 下载 7z 发布包，
取 VS2022~x64/dll/libusb-1.0.dll 放入 driver/ 即可。
"""

import io
import json
import sys
import zipfile
from pathlib import Path
from urllib.request import urlopen

DEST = Path(__file__).resolve().parent.parent / "driver" / "libusb-1.0.dll"
PYPI_PROJECTS = ("libusb1", "libusb-package")
TIMEOUT = 30


def _wheel_urls(project):
    """返回 PyPI 项目全部 wheel 下载 URL（最新版本）"""
    with urlopen(f"https://pypi.org/pypi/{project}/json", timeout=TIMEOUT) as r:
        meta = json.load(r)
    version = meta["info"]["version"]
    return [u["url"] for u in meta["releases"].get(version, [])
            if u["filename"].endswith(".whl")]


def _score_x64(name):
    """x64 目录/文件名打分：越大越可能是 64 位（避免取到 Win32 目录里的 DLL）"""
    low = name.lower()
    score = 0
    if "x64" in low or "amd64" in low or "win64" in low:
        score += 2
    if "x86" in low or "win32" in low or "i386" in low:
        score -= 2
    return score


def _pe_machine(data):
    """解析 PE 头 Machine 字段（0x8664=x64, 0x14C=x86）；解析失败返回 None"""
    try:
        if data[:2] != b"MZ":
            return None
        pe_off = int.from_bytes(data[0x3C:0x40], "little")
        if data[pe_off:pe_off + 4] != b"PE\x00\x00":
            return None
        return int.from_bytes(data[pe_off + 4:pe_off + 6], "little")
    except (IndexError, ValueError):
        return None


def _extract_dll(whl_bytes):
    """从 wheel (zip) 中定位 64 位 libusb-1.0.dll（校验 PE Machine=0x8664）；
    返回 (bytes, 内部路径) 或 (None, None)"""
    with zipfile.ZipFile(io.BytesIO(whl_bytes)) as z:
        candidates = [(n, z.read(n)) for n in z.namelist()
                      if n.lower().endswith("libusb-1.0.dll")]
        # 只接受 x64（win32 wheel 内的单个 usb1/libusb-1.0.dll 是 32 位，
        # 文件名无从分辨，必须查 PE 头）
        candidates = [c for c in candidates if _pe_machine(c[1]) == 0x8664]
        if not candidates:
            return None, None
        candidates.sort(key=lambda c: _score_x64(c[0]), reverse=True)
        return candidates[0][1], candidates[0][0]


def main():
    for project in PYPI_PROJECTS:
        try:
            urls = _wheel_urls(project)
        except Exception as e:  # noqa: BLE001 - 网络失败换下一个源
            print(f"[skip] {project}: {e}")
            continue
        for url in urls:
            try:
                print(f"[get ] {url}")
                with urlopen(url, timeout=TIMEOUT) as r:
                    data = r.read()
                dll, inner = _extract_dll(data)
                if dll is None:
                    print(f"[skip] {url}: 包内未找到 libusb-1.0.dll")
                    continue
                DEST.parent.mkdir(parents=True, exist_ok=True)
                DEST.write_bytes(dll)
                print(f"[ok  ] {inner} -> {DEST} ({len(dll)} bytes)")
                print("       编译时将随 driver/ 目录自动拷贝到 exe 同级目录")
                return 0
            except Exception as e:  # noqa: BLE001
                print(f"[skip] {url}: {e}")
    print("自动下载失败。请手动获取：https://libusb.info → 7z 包内 "
          "VS2022~x64/dll/libusb-1.0.dll → 放入 driver/ 目录")
    return 1


if __name__ == "__main__":
    sys.exit(main())
