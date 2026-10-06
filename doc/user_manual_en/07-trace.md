# 7. Trace (frame list)

## 7.1 Purpose

**Trace** shows bus frames in a table (time, channel, ID, direction, DLC, data, and more). It supports display filters, coloring, export, and linking with Graphic.

## 7.2 Open Trace

1. Click **Trace** in the activity bar.
2. Manage instances with **Open Editors** / **New Trace** in the side bar.
3. A Trace tab opens in the editor.

![Trace main view](images/07-trace-main.png)

## 7.3 Display filter bar

The top filter bar accepts expression filters (see the **?** help on the bar).

Examples:

```text
id == 0x123
id == 0x123 and fd
```

| Action | Notes |
|--------|--------|
| Enter / Apply | Apply the filter |
| Clear | Clear the expression |
| Presets | Filter presets |
| Clear list | Clear frames in the list (`Ctrl+L` and related actions — see the shortcuts page) |

A valid expression tends to tint the field green; invalid input tints red.

![Filter bar](images/07-filter-bar.png)

## 7.4 Common list actions

- **Scroll / auto-scroll**: follow newest frames (auto-scroll toggle available).
- **Select a row**: status bar shows selection; Inspector (right bar) can show details.
- **Context menu**: copy, apply as filter, color marks, navigation (see the live menu).
- **Export**: export visible or selected frames to a supported log format.

## 7.5 Import logs

- **File > Import Log File…** (`Ctrl+I`)
- Or **File > Open File…** then continue analysis

For batch offline files, use **Offline Analysis** under Transceive (see [Transceive](10-transceive.md)).

## 7.6 Performance tips

- Trace is a hot path: keep filters precise; avoid huge unfiltered sessions with many color rules.
- When Graphic is also heavy, keep Start / Stop measurement cadence clear.

---

← [Flow](06-flow.md) · [Manual home](README.md) · Next: [Graphic](08-graphic.md) →
