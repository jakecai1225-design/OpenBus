# Trace UX Redesign (CANoe + Wireshark)

> **Status**: Active — U1–U5 + chrome densify landed (2026-09-17)  
> **Last Updated**: 2026-09-17  
> **Parent**: `doc/Trace_Virtual_List_Redesign.md` (paint/SoT) · `doc/Trace_Graphic_Performance_Plan.md`  
> **Goal**: Trace that feels like CANoe Trace for measurement + Wireshark for filter/navigate/inspect — practical, friendly, stable, precise.

Performance (T0–T5) is **done**. This document redesigns **layout and discoverable features** without reopening O(history) model work.

---

## 1. Where we are (2026-09-17)

| Layer | Status |
|-------|--------|
| T0–T5 virtual Trace / CaptureLog camera | done |
| Graphic Stage 2 + B6 silence | done |
| Features already in code | rich; U1–U5 make them discoverable |
| Layout / chrome | **U1–U5 + densify done** |

**Verdict**: Capability is ahead of discoverability. Redesign = reshape chrome and defaults, not rewrite the data path.

---

## 2. What to take from each product

### From Wireshark

| Pattern | Openbus mapping |
|---------|-----------------|
| Display filter as primary control | Keep FilterBar expression; Apply on Enter; presets |
| Apply as Filter from cell | Context menu + **U2 filter chips** |
| Find / Go to packet | Shortcuts + **FilterBar actions** |
| Packet list + detail + decode | List + bottom Detail/Stats/Diff; **U3 right Signals pane** |
| Status: captured / displayed | **Shell status bar** (`traceStatus`) |

### From CANoe Trace

| Pattern | Openbus mapping |
|---------|-----------------|
| Measurement camera over bus history | CaptureLog SoT (done) |
| Follow / scroll lock while live | **Follow** toggle in FilterBar actions |
| Overview strip beside list | Keep `ViewportOverview` |
| Overwrite / fixed-ID row | Settings → overwrite mode (keep) |
| Bus columns: Ch, Dir, Name, Data | **U4 sticky No/Ch/Id/Data** |
| Color / mark for analysis | Context menu + color rules (keep) |

### Product principles

1. **One job per chrome band** — filter+actions one row; chips only when filtering; status in shell bar.  
2. **Live stays precise** — time axis = measurement timestamp; counts match CaptureLog / filter index.  
3. **Power under the hood, calm on top** — advanced time format / refresh rate stay in ⚙.  
4. **Do not break T3 paint budget** — no new O(history) widgets; overview stays meta-sampled.

---

## 3. Target layout (shipped)

```text
┌─────────────────────────────────────────────────────────────┐
│ FilterBar: [✓] [ filter edit ……stretch…… ] [✓][×]            │
│            [Find][GoTo] | [Follow] | [ID↑↓] | [Mark↑↓]       │
│            | [Graphic][Signals]  …  [presets][?][⚙][clear]  │
├─────────────────────────────────────────────────────────────┤
│ FilterChipBar (only when filter on): [Filter: … ×] …        │
├──────┬──────────────────────────────────┬───────────────────┤
│ Over │ TraceView (virtual / cacheRows)  │ Signals (U3)      │
│ view │                                  │                   │
├──────┴──────────────────────────────────┴───────────────────┤
│ Explorer tabs: Detail | Statistics | Diff | Signals         │
└─────────────────────────────────────────────────────────────┘
Shell status: [Ready] [Conn] | tab | N frames | Sel: … | time
```

No in-tab action strip row. No in-tab status strip. Duplicate row/filter labels removed from shell bar.
Action / filter chrome is **icon-only** with hover tooltips; filter edit expands.

---

## 4. Phases (UX only)

| # | Slice | Exit | Status |
|---|-------|------|--------|
| **U1** | Action strip + status strip + FilterBar declutter; Follow toggle | Discoverable Follow / Find / GoTo | **done** → densified |
| **U2** | Active-filter chips (main + column); one-click clear each | “What am I filtering?” always visible | **done** |
| **U3** | Optional right-side Signals pane; remember splitter | Decode while scrolling list | **done** |
| **U4** | Default columns / density; sticky No+Ch+Id+Data | First-run Trace readable | **done** |
| **U5** | Selection → Graphic; status precision (filter/sel) | Analysis loop feels tight | **done** |
| **U6** | Chrome densify: actions in FilterBar; status → shell bar | More list rows; no duplicate chrome | **done** |

Do **not** mix U-slices with C1b custom painter unless measurement says list paint is still the bottleneck.

---

## 5. Hard contracts (unchanged)

| Contract | Rule |
|----------|------|
| Time axis | `CanFrame::timestamp` from file/measurement — never PC-now |
| Live SoT | CaptureLog only; tabs are cameras |
| Per paint | O(visible); no decode in `paintEvent` |
| Hidden Trace | Cursor only |

---

## 6. Decision log

- **2026-09-17**: After T0–T5, start Trace **UX** redesign (this doc). First code: **U1** action + status strip.
- **2026-09-17**: **U1 landed** — FilterBar no longer hosts packet counts; TraceTab action strip; status strip; overview drag clears Follow.
- **2026-09-17**: **U2–U5 landed** — FilterChipBar; right Signals pane + splitter persistence + Signals toggle; sticky No/Ch/Id/Data (layout_version 5); To Graphic + status Filter/Sel precision.
- **2026-09-17**: **U6 chrome densify** — Filter edit maxWidth 420; Follow/Find/… in `FilterBar::actionsLayout`; removed TraceTab status strip; `statusInfoChanged` → shell `traceStatus`; shell bar drops duplicate row/filter labels (keep tab | frames | Sel | time).
- **2026-09-17**: **U6.1 icon toolbar** — All Trace filter-row actions icon-only + tooltips; filter edit expanding (min 360); groups: filter ops → find/goto → follow → ID → marks → graphic/signals.
