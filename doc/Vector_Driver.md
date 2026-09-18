# Vector XL driver (vxlapi)

## Hardware

Install **Vector Driver Setup** so Device Manager / Vector Hardware Config
lists VN16xx, VN56xx, or VirtualCAN. Apps talk to userland **`vxlapi64.dll`**
(XL Driver Library), not to the kernel driver directly.

## Layout

```
<exe>/drivers/vector/
  driver_vector.dll        # optional external plugin
  driver.json
  vendor/vxlapi64.dll      # optional copy; normally from Vector install PATH
```

Built-in backend (`CanDeviceVector` in openbus_data) also loads the same DLL
from `drivers/vector/vendor` → app dir → PATH.

## SDK mapping

| Step | XL API | openbus |
|------|--------|---------|
| Init | `xlOpenDriver` | Once when DLL loads |
| Enumerate | `xlGetDriverConfig` | One `DeviceInfo` per CAN-capable channel; `deviceType` = `channelIndex` |
| Open classic | `xlOpenPort` V3 + `xlCanSetChannelBitrate` + `xlActivateChannel` | `CanDeviceVector::open` |
| Open FD | `xlOpenPort` V4 + `xlCanFdSetConfiguration` | arb/data bitrate + default segments |
| TX / RX classic | `xlCanTransmit` / `xlReceive` | `send` / `recv` |
| TX / RX FD | `xlCanTransmitEx` / `xlCanReceive` | `send` / `recv` |

**Do not** ship `vxlapi64.dll` with openbus (Vector proprietary).

## Manual test checklist

1. Install Vector drivers; confirm channel in **Vector Hardware Config**.
2. Launch openbus → Device panel lists each CAN XL channel (FD tagged).
3. Classic: VirtualCAN or VN1610 @ 500 kbit/s — Trace RX/TX.
4. FD: VN16xx / Virtual FD — arb 500k, data 2M, BRS frames.
5. Close/reopen; if another app holds init permission, bitrate set may warn.
