# 5. Device

## 5.1 Purpose

**Device** selects a CAN adapter, configures baud / timing, and connects or disconnects. After a successful connect, send / receive and measurement can use real bus data (or simulator data).

## 5.2 Select a device

1. Click **Device** in the activity bar.
2. The **Devices** side-bar section lists discovered / configured items.
3. Click a device to open its configuration page in the editor.

![Device side bar and page](images/05-device-page.png)

## 5.3 Basic settings

| Field | Description |
|-------|-------------|
| **CAN mode** | `CAN 2.0A (Classic)` or `CAN FD (ISO 11898-1)` |
| **Channels** | Enable Channel 1 / Channel 2, etc. |
| **Baud rate** | Arbitration-phase baud (manual entry allowed) |
| **Timing preset** | Timing preset (for example CiA recommended) |
| **Data baud rate** | CAN FD only: data-phase baud |
| **Data phase timing** | CAN FD only: data-phase timing preset |

Some drivers (SLCAN / certain USB stacks) manage timing themselves; presets may be disabled with a “Driver auto timing” hint.

## 5.4 Connect and disconnect

1. Enable at least one channel.
2. Click **Connect**.
3. On success the status shows **Connected: …** and **Disconnect** becomes available.
4. Click **Disconnect** to release the device.

### Common messages

| Message | What to do |
|---------|------------|
| Select at least one channel | Enable a channel |
| Invalid arbitration / data-phase baud rate | Fix baud numbers |
| Connect (not implemented) | Kind not implemented — change device or install a driver |
| Connect failed (see output / log) | Check **Output**, drivers, permissions, exclusive use |

## 5.5 Relation to Flow / Trace

- **Connect** only opens the device session.
- For Trace / Graphic to **keep receiving**, you usually also **Start** measurement in **Flow** (see [Flow](06-flow.md)).

---

← [Project](04-project.md) · [Manual home](README.md) · Next: [Flow](06-flow.md) →
