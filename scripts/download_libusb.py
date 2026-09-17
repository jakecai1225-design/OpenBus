#!/usr/bin/env python3
"""Fetch libusb-1.0.dll for the Candle / GS_USB driver plugin.

Places the DLL at:
  drivers/candle/vendor/libusb-1.0.dll

Resolution order:
  1. Copy from MSYS2 UCRT64/CLANG64/MINGW64 (preferred for this project)
  2. Download official libusb Windows binaries (VS2022 x64 DLL from .7z)

Usage:
  python scripts/download_libusb.py
  python scripts/download_libusb.py --force
"""
from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import sys
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / "drivers" / "candle" / "vendor" / "libusb-1.0.dll"

# Official release binaries (x64 DLL path inside the archive)
LIBUSB_VERSION = "1.0.29"
LIBUSB_7Z_URL = (
    f"https://github.com/libusb/libusb/releases/download/"
    f"v{LIBUSB_VERSION}/libusb-{LIBUSB_VERSION}.7z"
)
# Fallback: GitHub also ships a source zip — we prefer MSYS2 / 7z DLL.
# Alternate direct path used when 7z is unavailable: PyPI wheel is avoided
# (known NULL-context crash on some Windows builds).

MSYS_CANDIDATES = [
    pathlib.Path(os.environ.get("SIN_MSYS2", "")),
    pathlib.Path(r"C:\msys64"),
    pathlib.Path(r"D:\msys64"),
    pathlib.Path(os.environ.get("MSYSTEM_PREFIX", "")).parent.parent
    if os.environ.get("MSYSTEM_PREFIX")
    else pathlib.Path(),
]


def find_msys_dll() -> pathlib.Path | None:
    prefixes = ("ucrt64", "clang64", "mingw64")
    for root in MSYS_CANDIDATES:
        if not root or not root.is_dir():
            continue
        for pref in prefixes:
            dll = root / pref / "bin" / "libusb-1.0.dll"
            if dll.is_file():
                return dll
    return None


def copy_dll(src: pathlib.Path) -> None:
    DEST.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, DEST)
    print(f"OK copied {src} -> {DEST}")


def download_via_7z() -> bool:
    """Download official .7z and extract VS2022\\MS64\\dll\\libusb-1.0.dll."""
    with tempfile.TemporaryDirectory(prefix="libusb_") as td:
        td_path = pathlib.Path(td)
        archive = td_path / f"libusb-{LIBUSB_VERSION}.7z"
        print(f"Downloading {LIBUSB_7Z_URL} ...")
        try:
            urllib.request.urlretrieve(LIBUSB_7Z_URL, archive)
        except Exception as e:
            print(f"WARN download failed: {e}")
            return False

        # Prefer system 7z; fall back to py7zr
        seven = shutil.which("7z") or shutil.which("7za")
        out_dir = td_path / "out"
        out_dir.mkdir()
        if seven:
            os.system(
                f'"{seven}" x -y "-o{out_dir}" "{archive}" '
                f'"VS2022/MS64/dll/libusb-1.0.dll" '
                f'"MinGW64/dll/libusb-1.0.dll" >nul 2>&1'
            )
        else:
            try:
                import py7zr  # type: ignore
            except ImportError:
                print("WARN neither 7z nor py7zr available — pip install py7zr")
                return False
            with py7zr.SevenZipFile(archive, mode="r") as z:
                targets = [
                    "VS2022/MS64/dll/libusb-1.0.dll",
                    "MinGW64/dll/libusb-1.0.dll",
                ]
                names = [n for n in z.getnames() if n.replace("\\", "/") in targets
                         or n.replace("\\", "/").endswith("MS64/dll/libusb-1.0.dll")
                         or n.replace("\\", "/").endswith("MinGW64/dll/libusb-1.0.dll")]
                if not names:
                    print("WARN DLL path not found in archive")
                    return False
                z.extract(targets=names, path=out_dir)

        for rel in (
            pathlib.Path("VS2022") / "MS64" / "dll" / "libusb-1.0.dll",
            pathlib.Path("MinGW64") / "dll" / "libusb-1.0.dll",
        ):
            cand = out_dir / rel
            if cand.is_file():
                copy_dll(cand)
                return True
        # py7zr may preserve nested paths differently — search
        for cand in out_dir.rglob("libusb-1.0.dll"):
            copy_dll(cand)
            return True
        print("WARN extract succeeded but libusb-1.0.dll not found")
        return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--force", action="store_true", help="Overwrite existing DLL")
    args = ap.parse_args()

    if DEST.is_file() and not args.force:
        print(f"OK already present: {DEST}")
        return 0

    msys = find_msys_dll()
    if msys:
        copy_dll(msys)
        return 0

    if download_via_7z():
        return 0

    print(
        "FAIL could not obtain libusb-1.0.dll.\n"
        "  Install: pacman -S mingw-w64-ucrt-x86_64-libusb\n"
        "  Or download from https://github.com/libusb/libusb/releases\n"
        "  and place VS2022/MS64/dll/libusb-1.0.dll at:\n"
        f"    {DEST}"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
