#!/usr/bin/env python3
"""Smoke-check PEAK PCAN-Basic (same API as PCAN-View).

Loads drivers/peak/vendor/PCANBasic.dll (or third_party x64) and lists
PCAN_ATTACHED_CHANNELS. Close PCAN-View first if open fails with HWINUSE.

  python scripts/smoke_peak_pcan.py
"""
from __future__ import annotations

import ctypes
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CANDIDATES = [
    ROOT / "drivers" / "peak" / "vendor" / "PCANBasic.dll",
    ROOT / "build" / "bin" / "drivers" / "peak" / "vendor" / "PCANBasic.dll",
    ROOT / "third_party" / "PCAN-Basic" / "x64" / "PCANBasic.dll",
]

PCAN_NONEBUS = 0x00
PCAN_ATTACHED_CHANNELS_COUNT = 0x2A
PCAN_ATTACHED_CHANNELS = 0x2B
PCAN_ERROR_OK = 0
FEATURE_FD = 0x01
CHANNEL_AVAILABLE = 0x01
CHANNEL_OCCUPIED = 0x02


class TPCANChannelInformation(ctypes.Structure):
    _fields_ = [
        ("channel_handle", ctypes.c_uint16),
        ("device_type", ctypes.c_uint8),
        ("controller_number", ctypes.c_uint8),
        ("device_features", ctypes.c_uint32),
        ("device_name", ctypes.c_char * 33),
        ("device_id", ctypes.c_uint32),
        ("channel_condition", ctypes.c_uint32),
    ]


def find_dll() -> pathlib.Path | None:
    for p in CANDIDATES:
        if p.is_file():
            return p
    return None


def main() -> int:
    dll_path = find_dll()
    if not dll_path:
        print("FAIL PCANBasic.dll not found — copy third_party/PCAN-Basic/x64/PCANBasic.dll")
        print("     to drivers/peak/vendor/")
        return 1

    lib = ctypes.WinDLL(str(dll_path))
    lib.CAN_GetValue.argtypes = [
        ctypes.c_uint16, ctypes.c_uint8, ctypes.c_void_p, ctypes.c_uint32
    ]
    lib.CAN_GetValue.restype = ctypes.c_uint32

    print(f"loaded {dll_path}")

    count = ctypes.c_uint32(0)
    st = lib.CAN_GetValue(
        PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS_COUNT,
        ctypes.byref(count), ctypes.sizeof(count)
    )
    if st != PCAN_ERROR_OK:
        print(f"FAIL PCAN_ATTACHED_CHANNELS_COUNT status=0x{st:X}")
        print("  Is the PEAK KMDF driver installed? Device Manager should show PCAN-USB.")
        return 1

    print(f"attached channel count = {count.value}")
    if count.value == 0:
        print("No PCAN hardware attached (plug PCAN-USB and retry)")
        return 2

    infos = (TPCANChannelInformation * count.value)()
    st = lib.CAN_GetValue(
        PCAN_NONEBUS, PCAN_ATTACHED_CHANNELS,
        infos, ctypes.sizeof(infos)
    )
    if st != PCAN_ERROR_OK:
        print(f"FAIL PCAN_ATTACHED_CHANNELS status=0x{st:X}")
        return 1

    for i, ch in enumerate(infos):
        name = ch.device_name.split(b"\x00", 1)[0].decode("latin1", "replace")
        fd = "FD" if (ch.device_features & FEATURE_FD) else "Classic"
        cond = []
        if ch.channel_condition & CHANNEL_AVAILABLE:
            cond.append("available")
        if ch.channel_condition & CHANNEL_OCCUPIED:
            cond.append("occupied")
        print(
            f"  [{i}] handle=0x{ch.channel_handle:X} type={ch.device_type} "
            f"id={ch.device_id} {fd} name='{name}' ({','.join(cond) or 'n/a'})"
        )

    print("RESULT PASS — openbus should list these under PEAK PCAN after rebuild")
    print("Tip: close PCAN-View before connecting in openbus (or leave BITRATE_ADAPTING on)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
