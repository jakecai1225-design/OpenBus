# Linux SocketCAN Driver

## Scope

Native Linux `PF_CAN` / `SOCK_RAW` / `CAN_RAW` backend for `can*`, `vcan*`,
and `slcan*` interfaces. **Windows builds ship a stub** (`isAvailable() == false`).

## SDK mapping

| Step | Kernel API | openbus |
|------|------------|---------|
| Enumerate | `/sys/class/net` + `if_nametoindex` | One `DeviceInfo` per iface; `deviceType` = ifindex |
| FD capability | iface MTU ≥ 72 (`CANFD_MTU`) | Name tagged `(FD)` |
| Open | `socket` + optional `CAN_RAW_FD_FRAMES` + `bind` | `CanDeviceSocketCan::open` |
| Bitrate | OS (`ip link set … type can bitrate`) | Not set by openbus |
| TX / RX | `write` / `read` + `poll` | `can_frame` or `canfd_frame` |

## Setup (example)

```bash
# Virtual loopback (CI / no hardware)
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Real CAN FD (adjust rates)
sudo ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
sudo ip link set up can0
```

## Implementation status

| Piece | Path | Status |
|-------|------|--------|
| Backend | `src/core/candevice_socketcan.{h,cpp}` | Complete (Linux); stub elsewhere |
| Plugin | `drivers/socketcan/` | Built on all hosts; useful on Linux |
| Builtin registry | `DriverRegistry` | Registered as `socketcan` |

## Manual test checklist

1. Create `vcan0` (commands above).
2. Launch openbus on Linux → Device panel lists `vcan0 [virtual]`.
3. Connect classic or FD → Trace TX/RX (second process or `cangen` / `candump`).
4. Real hardware: bring `can0` up with `ip link`, then soak RX ≥ 1k frame/s.
