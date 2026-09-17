#!/usr/bin/env python3
"""Smoke-check Candle/GS_USB hardware visibility (no Qt app required).

Loads drivers/candle/vendor/libusb-1.0.dll and lists VID/PID whitelist hits.
Use this to verify WinUSB binding before building driver_candle.dll.

  python scripts/smoke_candle_usb.py
"""
from __future__ import annotations

import ctypes
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DLL = ROOT / "drivers" / "candle" / "vendor" / "libusb-1.0.dll"

WHITELIST = {
    (0x1D50, 0x606F): "GS_USB / candleLight",
    (0x1209, 0x8C00): "CANable (candle)",
    (0x1209, 0x2323): "candleLight",
    (0x1209, 0xCA01): "CANnectivity",
    (0x1CD2, 0x606F): "CES CANext FD",
    (0x16D0, 0x10B8): "ABE CANDebugger FD",
    (0x16D0, 0x0F30): "Xylanta Saint3",
}


class DeviceDescriptor(ctypes.Structure):
    _fields_ = [
        ("bLength", ctypes.c_uint8),
        ("bDescriptorType", ctypes.c_uint8),
        ("bcdUSB", ctypes.c_uint16),
        ("bDeviceClass", ctypes.c_uint8),
        ("bDeviceSubClass", ctypes.c_uint8),
        ("bDeviceProtocol", ctypes.c_uint8),
        ("bMaxPacketSize0", ctypes.c_uint8),
        ("idVendor", ctypes.c_uint16),
        ("idProduct", ctypes.c_uint16),
        ("bcdDevice", ctypes.c_uint16),
        ("iManufacturer", ctypes.c_uint8),
        ("iProduct", ctypes.c_uint8),
        ("iSerialNumber", ctypes.c_uint8),
        ("bNumConfigurations", ctypes.c_uint8),
    ]


def main() -> int:
    if not DLL.is_file():
        print(f"FAIL missing {DLL} — run: python scripts/download_libusb.py")
        return 1

    lib = ctypes.CDLL(str(DLL))
    lib.libusb_init.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_init.restype = ctypes.c_int
    lib.libusb_exit.argtypes = [ctypes.c_void_p]
    lib.libusb_get_device_list.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))]
    lib.libusb_get_device_list.restype = ctypes.c_ssize_t
    lib.libusb_free_device_list.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_int]
    lib.libusb_get_device_descriptor.argtypes = [ctypes.c_void_p, ctypes.POINTER(DeviceDescriptor)]
    lib.libusb_get_device_descriptor.restype = ctypes.c_int
    lib.libusb_get_bus_number.argtypes = [ctypes.c_void_p]
    lib.libusb_get_bus_number.restype = ctypes.c_uint8
    lib.libusb_get_device_address.argtypes = [ctypes.c_void_p]
    lib.libusb_get_device_address.restype = ctypes.c_uint8
    lib.libusb_open.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_open.restype = ctypes.c_int
    lib.libusb_close.argtypes = [ctypes.c_void_p]

    ctx = ctypes.c_void_p()
    # Use default context (NULL) after init(NULL) — match CanDeviceCandle
    if lib.libusb_init(None) != 0:
        print("FAIL libusb_init")
        return 1

    lst = ctypes.POINTER(ctypes.c_void_p)()
    n = lib.libusb_get_device_list(None, ctypes.byref(lst))
    if n < 0:
        print(f"FAIL get_device_list rc={n}")
        return 1

    hits = 0
    print(f"libusb OK — scanning {n} USB device(s)...")
    print("--- all USB VID:PID ---")
    for i in range(n):
        dev = lst[i]
        desc = DeviceDescriptor()
        if lib.libusb_get_device_descriptor(dev, ctypes.byref(desc)) != 0:
            continue
        key = (desc.idVendor, desc.idProduct)
        bus = lib.libusb_get_bus_number(dev)
        addr = lib.libusb_get_device_address(dev)
        name = WHITELIST.get(key)
        tag = f"  {key[0]:04x}:{key[1]:04x} class={desc.bDeviceClass} @ {bus}-{addr}"
        if name:
            handle = ctypes.c_void_p()
            open_rc = lib.libusb_open(dev, ctypes.byref(handle))
            if open_rc == 0:
                lib.libusb_close(handle)
                status = "OPEN OK (WinUSB/libusb bound)"
            else:
                status = f"OPEN FAIL rc={open_rc} (bind WinUSB with Zadig?)"
            print(f"{tag}  << {name} — {status}")
            hits += 1
        else:
            print(tag)

    lib.libusb_free_device_list(lst, 1)
    if hits == 0:
        print(
            "No GS_USB / CANable (candle) devices found.\n"
            "  - Candle path needs candleLight_fw (VID 1209:8c00 or 1d50:606f)\n"
            "  - Stock CANable with SLCAN firmware appears as a COM port → use slcan driver"
        )
        return 2
    print(f"RESULT PASS hits={hits}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
