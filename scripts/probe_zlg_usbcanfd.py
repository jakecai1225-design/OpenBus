#!/usr/bin/env python3
"""Probe ZCAN_OpenDevice for USBCANFD-200U (type=41) next to openbus.exe."""
from __future__ import annotations

import ctypes
import os
import sys


def main() -> int:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build", "bin"))
    os.chdir(root)
    dll_path = os.path.join(root, "zlgcan.dll")
    if not os.path.isfile(dll_path):
        print("MISSING", dll_path)
        return 2

    dll = ctypes.WinDLL(dll_path)
    open_dev = dll.ZCAN_OpenDevice
    open_dev.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
    open_dev.restype = ctypes.c_void_p
    close_dev = dll.ZCAN_CloseDevice
    close_dev.argtypes = [ctypes.c_void_p]
    close_dev.restype = ctypes.c_uint

    type_200u = 41
    found = False
    for idx in range(4):
        h = open_dev(type_200u, idx, 0)
        print(f"OpenDevice(41,{idx}) -> {h}")
        if h:
            close_dev(h)
            print("FOUND USBCANFD-200U index", idx)
            found = True
            break
    if not found:
        print("NOT FOUND via ZCAN_OpenDevice type=41")
        print("Check: device plugged, ZLG USB driver, kerneldlls/USBCANFD.dll")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
