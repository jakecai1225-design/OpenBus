# Intrepid ValueCAN / neoVI Driver

## Research summary

Intrepid ValueCAN 4 and neoVI FIRE/RED families are supported through the open-source **libicsneo** C API (`icsneoc`). This is preferred over the legacy Vehicle Spy `icsneo40.dll` / SpyMessage ABI (different layout, Windows-centric).

### SDK mapping

| Step | libicsneo C | openbus |
|------|-------------|---------|
| Enumerate | `icsneo_findAllDevices` | One `DeviceInfo` per box; `deviceType` = scan index |
| Channels | `icsneo_getNetworkByNumber(…, CAN, n)` | Channel 0 = 1st CAN net, etc. |
| Open | `icsneo_openDevice` | Keep `neodevice_t` for session |
| Bitrate | `icsneo_setBaudrate` + optional `setFDBaudrate` | Then `settingsApplyTemporary` |
| Online / poll | `enableMessagePolling` + `goOnline` | Required before RX/TX |
| TX / RX | `icsneo_transmit` / `icsneo_getMessages` (`neomessage_can_t`) | `send` / `recv` |
| Close | `goOffline` + `closeDevice` | Invalidates `neodevice_t` |

### Licensing / deploy

- Do **not** ship `icsneoc.dll` / `libicsneoc.so` with openbus by default (libicsneo is GPLv3 unless you have a commercial license).
- Build [libicsneo](https://github.com/intrepidcs/libicsneo) with `LIBICSNEO_BUILD_ICSNEOC=ON`, or place the shared library under `drivers/intrepid/vendor/`.
- Load order: `drivers/intrepid/vendor/` → app dir → PATH / ld cache.
- Legacy `icsneo40.dll` is **not** supported by this backend.

### Implementation status

| Piece | Path | Status |
|-------|------|--------|
| Backend | `src/core/candevice_intrepid.{h,cpp}` | Complete (classic + ISO FD) |
| Plugin | `drivers/intrepid/` | `driver_intrepid` + `driver.json` |
| Builtin registry | `DriverRegistry` | Registered as `intrepid` |
| Vendor DLL | bundled | No |

### Manual test checklist

1. Build/install libicsneo; confirm `icsneoc` loads (log: `Intrepid loaded …`).
2. Launch openbus → Device panel lists ValueCAN / neoVI (FD tagged when type known).
3. Classic: ValueCAN @ 500 kbit/s — Trace RX/TX.
4. FD: arb 500k, data 2M, BRS frames.
5. Close/reopen. Multi-channel boxes: channel 0 then channel 1.
