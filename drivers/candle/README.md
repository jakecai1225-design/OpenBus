# Candle / GS_USB driver (CANable candle firmware)

## What this driver supports

USB devices speaking the **GS_USB** protocol (Linux `gs_usb`):

| Device | VID:PID |
|--------|---------|
| CANable (candleLight_fw) | `1209:8c00` |
| GS_USB / candleLight | `1d50:606f` |
| candleLight (legacy) | `1209:2323` |
| CANnectivity / CES / ABE / Xylanta | see `driver.json` |

**Not this driver:** CANable flashed with **SLCAN** (appears as a COM/CDC port) → use `drivers/slcan`.

## Layout (must match .odp install)

```
<exe>/drivers/candle/
  driver_candle.dll
  driver.json
  vendor/libusb-1.0.dll
```

## Setup

```bash
# 1) Fetch libusb (or: pacman -S mingw-w64-ucrt-x86_64-libusb && re-run)
python scripts/download_libusb.py

# 2) Verify hardware + WinUSB binding (plug CANable first)
python scripts/smoke_candle_usb.py

# 3) Build plugin (MSYS2 UCRT64 toolchain, same as main app)
cmake --build build --target driver_candle

# Output: build/bin/drivers/candle/driver_candle.dll (+ vendor copy if present)
```

If `smoke_candle_usb.py` shows the device but `OPEN FAIL`, use [Zadig](https://zadig.akeo.ie/) to bind **WinUSB** to the interface.

## Runtime

1. Start openbus → Device panel lists **Candle / GS_USB** when the plugin is loaded.
2. Connect → set bitrate → Flow **Start measurement**.
3. Trace should show Rx; Tx via Transceive / plugins uses the same hub.
