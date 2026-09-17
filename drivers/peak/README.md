# PEAK PCAN driver (PCAN-Basic)

## Hardware you have

Device Manager → **CAN-Hardware → PCAN-USB** means the PEAK **KMDF** driver
(`PCAN_USB` 4.x) is installed. That is required but not sufficient.

Apps (PCAN-View, Cangaroo, openbus) talk to userland **`PCANBasic.dll`**,
not to `PCAN_USB.sys` directly. BusMaster’s old **CanApi2** path is obsolete;
we reuse the same PCAN-Basic API as PCAN-View.

## Layout

```
<exe>/drivers/peak/
  driver_peak.dll          # optional external plugin
  driver.json
  vendor/PCANBasic.dll     # from third_party/PCAN-Basic/x64/
```

Built-in backend (`CanDevicePEAK` in openbus_core) also loads the same DLL
from `drivers/peak/vendor` → app dir → PATH.

## Setup (MSYS2 / MinGW)

```bash
# Stage vendor DLL (also done by CMake POST_BUILD for driver_peak)
mkdir -p drivers/peak/vendor
cp third_party/PCAN-Basic/x64/PCANBasic.dll drivers/peak/vendor/
cp third_party/PCAN-Basic/x64/PCANBasic.dll build/bin/drivers/peak/vendor/

# Verify hardware is visible (close PCAN-View first if busy)
python scripts/smoke_peak_pcan.py

# Rebuild openbus (UCRT64 / same toolchain as the app)
cmake --build build --target openbus
# optional: cmake --build build --target driver_peak
```

## In the app

1. Device panel → **PEAK PCAN** → your PCAN-USB channel
2. Connect (Classic CAN, e.g. 500 kbit/s — stock PCAN-USB is not FD)
3. Flow → **Start measurement**
4. Trace should show bus traffic

If connect fails with “in use”, close **PCAN-View** (or any other app holding the channel).
