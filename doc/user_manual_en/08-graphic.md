# 8. Graphic (waveforms)

## 8.1 Purpose

**Graphic** plots DBC signals (or selected variables) against time for value trends, multi-signal comparison, and cursor measurement.

## 8.2 Open Graphic

1. Click **Graphic** in the activity bar.
2. Use **New Graphic** or open an existing editor in the side bar.
3. Add signals from Database / Trace / the signal picker.

![Graphic view](images/08-graphic-main.png)

## 8.3 Basic operations (concepts)

| Action | Notes |
|--------|--------|
| Add signals | Pick signals from the DBC tree or dialog |
| Show / hide | Toggle visibility in the signal list |
| Zoom / pan | Wheel, drag, or toolbar (see the UI) |
| Cursors | Place measurement cursors for time and delta |
| Pause | Pause view refresh (data may still accumulate) |

## 8.4 Relation to Trace / DBC

- Without a DBC you usually only see frame-level info; **physical waveforms need a loaded DBC**.
- Trace selection / signals can be sent to Graphic when the context menu offers that action.

## 8.5 Tips

- Keep the number of simultaneous curves reasonable for readability and performance.
- Overlay vs split tracks depend on toolbar options; prefer clear comparison by default.

---

← [Trace](07-trace.md) · [Manual home](README.md) · Next: [Database](09-database.md) →
