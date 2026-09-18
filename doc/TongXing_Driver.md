# TOSUN / TongXing CAN / CAN FD Driver

## Research summary

TOSUN TC series (TC1011 / TC1013 / TC1014 / TC1016 / TC1017 and related TSMaster boxes) share one userland API: **libTSCAN** (`libTSCAN.dll`). `libTSH.dll` is a runtime dependency and must sit next to it (or on PATH).

### SDK mapping

| Step | libTSCAN | openbus |
|------|----------|---------|
| Init | `initialize_lib_tscan(FIFO, no error frames, HW time)` | Once when DLL loads |
| Enumerate | `tscan_scan_devices` + `tscan_get_device_info` | One `DeviceInfo` per box; `deviceType` = scan index |
| Channels / FD | brief `tscan_connect` + `tscan_get_can_channel_count` | Name tagged `(FD)` when capable |
| Open classic | `tscan_config_can_by_baudrate` (kbps, 120 ohm on) | `CanDeviceTongXing::open` |
| Open FD | `tscan_config_canfd_by_baudrate` ISO + Normal | arb/data in kbps |
| TX | `tscan_transmit_can_async` / `tscan_transmit_canfd_async` | `send` |
| RX | `tsfifo_receive_can_msgs` / `tsfifo_receive_canfd_msgs` | `recv` |
| Close | `tscan_disconnect_by_handle` | `close` |

Frame layouts (`#pragma pack(1)`): `TLIBCAN` 24 bytes, `TLIBCANFD` 80 bytes. Timestamps are microseconds.

### Licensing / deploy

- Do **not** ship `libTSCAN.dll` / `libTSH.dll` with openbus.
- Install TOSUN TSMaster runtime, or copy both DLLs to `drivers/tongxing/vendor/`.
- Load order: `drivers/tongxing/vendor/` → app dir → PATH.

### Implementation status

| Piece | Path | Status |
|-------|------|--------|
| Backend | `src/core/candevice_tongxing.{h,cpp}` | Complete (classic + ISO FD) |
| Plugin | `drivers/tongxing/` | `driver_tongxing.dll` + `driver.json` |
| Builtin registry | `DriverRegistry` | Registered as `tongxing` |
| Vendor DLL | bundled | No |

### Manual test checklist

1. Install TOSUN runtime; confirm the box in TSMaster.
2. Launch openbus → Device panel lists the device (FD tagged when capable).
3. Classic: TC1011 @ 500 kbit/s — Trace RX/TX.
4. FD: arb 500k, data 2M, BRS frames.
5. Close/reopen. Multi-channel boxes: pick channel 0 then channel 1.
