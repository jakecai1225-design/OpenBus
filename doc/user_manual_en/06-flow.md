# 6. Flow (measurement setup)

## 6.1 Purpose

**Flow** (activity-bar icon / Measurement Setup) configures the measurement topology: which analysis views participate and how global measurement starts and stops. It follows the common Measurement Setup idea used in the industry.

## 6.2 Open Flow

1. Click **Flow** in the activity bar.
2. The side bar can manage open Flow editors and create Protocol Flow instances.
3. The center canvas shows nodes and links (node types grow with versions).

![Flow canvas](images/06-flow-canvas.png)

## 6.3 Start and stop measurement

1. Ensure the device is **Connected** when using a real bus.
2. Click **Start** in the Flow toolbar (or equivalent) to begin measurement.
3. Trace / Graphic views attached to the measurement start updating.
4. Click **Stop** to end measurement.

> The status bar reflects measurement frame counts; details go to **Output**.

## 6.4 Trace / Graphic instances

- Create multiple instances with **New Trace** / **New Graphic** from Flow or the Trace / Graphic side bars.
- Saving a project may remember layout and instance links (depends on the project format).

## 6.5 Tips

- Connect the device before Start to avoid an empty run.
- Under heavy load, open only the Trace windows you need first, then add Graphic.
- After changing baud: Stop → Disconnect → edit → Connect → Start.

---

← [Device](05-device.md) · [Manual home](README.md) · Next: [Trace](07-trace.md) →
