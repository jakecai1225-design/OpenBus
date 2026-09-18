# IXXAT VCI4 CAN / CAN FD Driver

## Research summary

IXXAT USB-to-CAN V2 / compact / FD (HMS) share the **VCI4** userland API. Classic-only stacks use `vcinpl.dll`; CAN FD uses **`vcinpl2.dll`**. openbus loads `vcinpl2.dll` only — it also covers classic CAN on FD-capable runtimes.

### SDK mapping

| Step | VCI4 | openbus |
|------|------|---------|
| Init | `vciInitialize` | Once when DLL loads |
| Enumerate | `vciEnumDeviceOpen` / `Next` / `Close` | One `DeviceInfo` per box; `deviceType` = scan index |
| Channels / FD | `vciDeviceOpen` + `canControlOpen` / `canControlGetCaps` | Channel count and `(FD)` tag |
| Open | `canChannelOpen` + `canControlInitialize` + `canControlStart` | Shared channel, 1024 RX / 128 TX |
| Classic bitrate | `CANBTP` arbitration preset (80% sample) | 250k / 500k / 1M |
| FD bitrate | second `CANBTP` + `CAN_EXMODE_EXTDATALEN` / `FASTDATA` | data 500k…8M presets |
| TX / RX | `canChannelPostMessage` / `canChannelReadMessage` (`CANMSG2`) | `send` / `recv` |
| Close | stop control, close channel, close device | `close` |

### Licensing / deploy

- Do **not** ship `vcinpl2.dll` with openbus.
- Install [IXXAT VCI V4](https://www.ixxat.com/), or copy the **x64** `vcinpl2.dll` to `drivers/ixxat/vendor/`.
- Load order: `drivers/ixxat/vendor/` → app dir → PATH.

### Implementation status

| Piece | Path | Status |
|-------|------|--------|
| Backend | `src/core/candevice_ixxat.{h,cpp}` | Complete (classic + ISO FD via vcinpl2) |
| Plugin | `drivers/ixxat/` | `driver_ixxat.dll` + `driver.json` |
| Builtin registry | `DriverRegistry` | Registered as `ixxat` |
| Vendor DLL | bundled | No |

### Manual test checklist

1. Install IXXAT VCI V4; confirm the adapter in the VCI tool.
2. Launch openbus → Device panel lists the device (FD tagged when capable).
3. Classic: USB-to-CAN V2 @ 500 kbit/s — Trace RX/TX.
4. FD: arb 500k, data 2M, BRS frames.
5. Close/reopen. Two-channel boxes: channel 0 then channel 1.
