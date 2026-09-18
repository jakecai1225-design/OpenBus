# Kvaser CAN / CAN FD Driver

## Research summary (mainstream devices)

Kvaser uses **one SDK for all hardware**: [CANlib](https://kvaser.com/canlib-sdk/) (`canlib32.dll` on Windows). Software written against CANlib runs on USB, PCIe, Mini PCIe, and virtual channels without per-product APIs.

### Mainstream product lines (CAN + CAN FD)

| Family | Form factor | Typical channels | CAN FD | Notes |
|--------|-------------|------------------|--------|--------|
| **Leaf Pro HS v2** / Leaf Light HS v2 | USB | 1 | Yes (Pro) | Most common lab USB stick |
| **USBcan Pro 2xHS v2** / 4xHS / **5xCAN** | USB | 2–5 | Yes | Multi-channel bench / HIL |
| **U100** / U100P / U100X | USB | 1 | Yes | Rugged IP67-oriented |
| **PCIEcan 1x/2xCAN v3** | PCIe | 1–2 | Yes | Desktop / IPC |
| **Mini PCIe 1x/2xCAN v3** | Mini PCIe | 1–2 | Yes | Embedded; older Mini PCIe 2xHS is classic-only |
| **Memorator Pro 2xHS / 5xHS** | USB + logger | 2–5 | Yes | Capture + interface |
| **Hybrid Pro CAN/LIN** | USB | 2 | Yes (CAN) | CAN + LIN |
| **Virtual CAN** | Software | 2+ | Yes | Ships with CANlib; good for CI / no-hardware |

Older classic-only Leaf / USBcan II still enumerate; open falls back to classic CAN when `canCHANNEL_CAP_CAN_FD` is absent.

### SDK usage (openbus mapping)

| Step | CANlib API | openbus |
|------|------------|---------|
| Init | `canInitializeLibrary` | Once when DLL loads |
| Enumerate | `canGetNumberOfChannels` + `canGetChannelData` | One `DeviceInfo` per channel; `deviceType` = CANlib channel |
| Open | `canOpenChannel(ch, EXCLUSIVE\|ACCEPT_VIRTUAL[\|CAN_FD])` | `CanDeviceKvaser::open` |
| Classic bitrate | `canSetBusParams(h, canBITRATE_*)` | Presets 1M…50k |
| FD bitrate | `canSetBusParams(h, canFD_BITRATE_*_80P)` + `canSetBusParamsFd` | Arb 500k/1M; data 1M…8M presets |
| Bus on/off | `canBusOn` / `canBusOff` | open / close |
| TX / RX | `canWrite` / `canReadWait` | `send` / `recv` |

**Do not** use `canBITRATE_*` when the channel was opened with `canOPEN_CAN_FD` — use `canFD_BITRATE_*` for arbitration and `canSetBusParamsFd` for data phase.

### Licensing / deploy

- `canlib32.dll` is **not** redistributed with openbus (Kvaser proprietary).
- Install [Kvaser Drivers + CANlib SDK](https://www.kvaser.com/download/), or copy `canlib32.dll` to `build/bin/drivers/kvaser/vendor/`.
- Load order: `drivers/kvaser/vendor/` → app dir → system PATH.

### Implementation status

| Piece | Path | Status |
|-------|------|--------|
| Backend | `src/core/candevice_kvaser.{h,cpp}` | Complete (classic + ISO FD) |
| Plugin | `drivers/kvaser/` | `driver_kvaser.dll` + `driver.json` |
| Builtin registry | `DriverRegistry` | Registered as `kvaser` |
| Vendor DLL | bundled | No |

### Manual test checklist

1. Install Kvaser drivers; confirm device in **Kvaser Device Guide**.
2. Launch openbus → Device panel should list each CANlib channel (FD tagged).
3. Classic: Leaf / Virtual @ 500 kbit/s — Trace RX/TX.
4. FD: Leaf Pro / USBcan Pro / Virtual FD — arb 500k, data 2M, BRS frames.
5. Optional: two virtual channels loopback without hardware.
