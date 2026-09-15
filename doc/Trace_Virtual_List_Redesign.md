# Trace Virtual List Redesign (CANoe / Wireshark class)

> **Status**: Design approved for implementation — Trace first, Graphic second  
> **Last Updated**: 2026-09-15  
> **Parent**: `doc/Trace_Graphic_Performance_Plan.md`  
> **Goal**: Smooth Trace under high bus load and long history; display cost ≈ visible rows only.

This document is the durable redesign for Trace. Incremental A/B patches reduced waste but **cannot** deliver CANoe/Wireshark-class fluidity while `QAbstractItemModel` + proxy chain still treat hundreds of thousands of rows as a live database.

---

## 1. Why current Trace still stutters

### What we have today

```text
CaptureLog (shared ring)
  → TraceTab timer (16 ms) syncFromCaptureLog (≤256 rows)
  → CanTraceModel (camera: rowCount = CaptureLog size)
  → CanTraceProxyModel (filter + sort map over ALL accepted rows)
  → ViewportProxyModel (window of ~2000 rows)
  → QTableView
```

### Why it still fails under load

| Layer | Cost that does not scale |
|-------|--------------------------|
| `CanTraceModel` | `rowCount` grows with CaptureLog (100k–500k). Qt model assumes "all rows exist". |
| `CanTraceProxyModel` | Keeps `m_proxyRows` / `m_sourceToProxy` for **every** accepted row. Insert of N frames → evaluate N filters + resize maps. |
| `frameAt` / `data()` | CaptureLog camera: mutex + `CanFrame` (QByteArray) copy **per cell**. ~10 columns × ~40 visible rows × paint = heavy. |
| `ColSignal` / DBC / color | Format and decode on the paint / `data()` path. |
| Auto-scroll | Moves viewport + may notify large ranges. |
| Shell co-tenants | Flow / Watcher / Graphic / status still share the GUI thread. |

**Bottleneck verdict**: Streaming batch caps help, but the architecture still pays O(history) in the proxy and O(cells × deep copy) in paint. That is the opposite of Wireshark/CANoe.

---

## 2. How Wireshark and CANoe do it (lessons)

### Wireshark (packet list)

- **Storage ≠ view**: packets live in a capture file / epan structures, not in a Qt million-row model.
- **Display filter**: builds / updates an **accepted index** (or redissects) off the hot paint path; the list paints from the index.
- **Virtual paint**: only rows intersecting the viewport are formatted; string cache for visible rows.
- **Progressive UI**: capture continues while UI updates on a budget; user always sees recent packets without freezing.

### CANoe (Trace window)

- **Sliding / ring history** with a **viewport camera** over shared measurement data.
- **Filter applied as an index**, not by cloning frames into a filtered table model.
- **Paint budget ≈ screen**: ~30–60 visible lines, not 2000 model rows forced through `QTableView`.
- Measurement thread and UI thread are separated; Trace is a consumer camera.

### Shared principles (adopt these)

1. **One history, many cameras** — CaptureLog is SoT; tabs never own full copies for live path.  
2. **Filter index, not filtered clone** — incremental bitset / row-id list beside CaptureLog.  
3. **Virtual list** — paint only visible rows; model rowCount for Qt = viewport size (or thin facade).  
4. **Format off paint** — visible-row string / color cache; DBC decode lazy or worker.  
5. **Display budget fixed** — independent of bus fps and CaptureLog size.  
6. **Background tabs silent** — advance cursor only; no full proxy notify.

---

## 3. Target Trace architecture

```text
Device / Player / Sim
  → CaptureLog (SoA preferred: time, id, dlc, flags, data blob pool)
       │
       ├─ FilterIndexWorker (optional thread)
       │     incremental accept[] / rowIds for active filter expr
       │
       └─ TraceCamera (per visible Trace tab)
             cursor / auto-scroll / selection by seq
             VisibleRowCache (strings + colors for ~2× viewport)
             TraceListView (custom paint OR thin QAbstractTableModel of N≈screen rows)
```

### Hard contracts

| Contract | Rule |
|----------|------|
| Time axis | `CanFrame::timestamp` from file/measurement; never PC-now at plot/playback emit |
| Live SoT | CaptureLog only; offline import may use local ring until migrated |
| GUI work per frame | O(1) amortized: bump counters; never O(history) |
| GUI work per paint | O(visible_rows × cheap_cell); no deep decode in paint |
| Filter change | One rebuild of index (may be async); then incremental again |
| Hidden Trace tab | Cursor advance only; no list notify / no cache rebuild |

### What we stop doing

- Growing `CanTraceProxyModel::m_proxyRows` to hundreds of thousands for live capture.  
- Exposing CaptureLog size as `QAbstractItemModel::rowCount` for the table.  
- Calling `CaptureLog::frameAt` + full format for every column of every proxy row on every paint.  
- Treating `ViewportProxyModel(2000)` as “virtualization” — it still sits on a huge proxy map.

---

## 4. Implementation phases (Trace only)

Ship vertical slices. Each slice must leave Trace usable.

### T0 — Freeze product rules (docs / config) — 0.5 day

- Document: Trace first, Graphic second.  
- Config: `trace.visibleRows` (default ~40–80), `trace.cacheRows` (~2× visible), `capture.maxFrames` remains history.  
- Acceptance checklist for smoothness (below).

### T1 — CaptureLog SoA + cheap `frameAt` — done (2026-09-15)

**Intent**: Make reading one Trace row cheap without changing the view yet.

- Split hot fields from payload: `CaptureFrameMeta` contiguous ring; payload in 64-byte side slabs.
- API: `frameMetaAt`, `copyDataAt`; `frameAt` rebuilds full `CanFrame` for compat.
- Trace `data()` uses meta first; Data / Signal / color-filter / FrameRole load payload only.

**Exit**: Paint path no longer deep-copies `CanFrame` (incl. `QByteArray`) for every cell.

### T2 — Filter index beside CaptureLog (P0-2) — done (2026-09-15)

**Intent**: Filter without a million-row proxy map.

- `TraceFilterIndex`: accepted source-row list; reverse lookup via binary search.
- `CanTraceProxyModel` MapMode:
  - **Passthrough** — no filters, capture order: identity map (zero history vectors)
  - **AcceptIndex** — filters on, capture order: accepted list only
  - **DenseMaps** — user sort: accepted list + dense reverse map
- Incremental append evaluates new frames only; expression change rebuilds once.

**Exit**: Live unfiltered capture no longer allocates O(history) proxy maps; filtered live path grows with accepted rows only.

### T3 — Virtual Trace list (C1 / P2-1 brought forward) — done (2026-09-15)

**Intent**: Display cost = screen, not history.

- Viewport window sized by `trace.cacheRows` (default **128**, was 2000).
- Overview / external scroll moves `viewportStart` across filtered source.
- Prefill Trace format cache on viewport move; overview samples via `frameMetaAt`.
- Fixed CaptureLog ring-wrap path that could `dataChanged` the entire history.
- Chain kept: `CanTraceModel → CanTraceProxyModel → ViewportProxyModel → QTableView`
  (T2 lean maps + T3 small viewport). Custom painter deferred to T3b if still heavy.

**Exit**: QTableView rowCount ≤ cacheRows; paint/notify bounded by window, not CaptureLog size.

### T4 — Format / DBC / color off paint (P0-3 + P1-2) — done (2026-09-15)

- `setVisibleRange` prefills all columns (incl. ColSignal DBC decode) + fg/bg color rules.
- `data()` for the window is cache-hit only; no rule eval / decode during paint.
- Outside the window: meta-only colors (no payload rules).
- Color/DBC changes refresh only the cached window (not full history `dataChanged`).
- `invalidateRowCache` keeps the window range and refills after sync/wrap.

### T5 — Shell silence for Trace path (P0-4 / B6) — done (2026-09-15)

- Background Trace: advance CaptureLog cursor only; no `syncFromCaptureLog` / proxy notify.
- On show: `adoptCaptureLogSnapshot` one-shot tip align + viewport cache fill.
- Hidden Graphic: tip-cursor only (no twin push / replot); pull interval 500 ms.
- Watcher: stop refresh timer when hidden; shell already skips `onFrame` when not visible.
- Status bar throttle ~5 Hz.

### Graphic (Stage 2 — after Trace T3 lands)

Do **not** start until Trace T3 acceptance passes:

- Finish P1-1 (drop per-view raw twin).  
- Tighter replot budget / overlay.  
- GPU / strip compositor only if still needed.

---

## 5. Mapping to existing backlog

| Old ID | Disposition |
|--------|-------------|
| P0-2 Filter index | **T2** (core of redesign) |
| P0-3 frameAt / format | **T1 + T4** |
| P0-4 Shell silence | **T5** |
| P2-1 / C1 Virtual list | **T3** (brought forward; no longer “later”) |
| B5 camera | Keep CaptureLog SoT; extend with SoA + index |
| CanTraceProxyModel | Shrink to offline / transition; not live SoT |

---

## 6. Acceptance (manual + light metrics)

1. Live or Player at high fps, Trace auto-scroll on — mouse/UI remains interactive; no multi-second freezes.  
2. CaptureLog ≥ 200k frames, filter `id == 0x123` — apply filter &lt; 1s feel; scrolling smooth.  
3. Two Trace tabs — background tab CPU ≈ idle aside from cursor.  
4. Same frame: Trace Time column == Graphic X (file timestamp contract unchanged).  
5. Stop measurement / clear — no leftover fans or UAF.

Optional: log `trace.paint_ms` / `trace.sync_ms` p95 in debug builds.

---

## 7. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Large rewrite breaks selection / mark / export | Keep seq-based identity; migrate features behind TraceCamera API |
| Async filter rebuild races append | Generation token; discard stale rebuilds |
| SoA migration touches many call sites | Compat `frameAt` wrapper; migrate paint path first |
| QTableView still slow after T3 | Proceed to custom painter (T3b) without changing storage |

---

## 8. Decision

- **Approve Trace T0→T5 as the next mainline**; Graphic Stage 2 after T3.  
- Stop investing in further “batch size / timer interval” tuning as the primary fix — those are bandaids on an O(history) UI model.  
- First coding milestone: **T1 SoA + T2 filter index**, then **T3 virtual viewport model**.

---

## Decision log

- **2026-09-15**: User requested CANoe/Wireshark-class redesign; Trace first, Graphic second. Documented as this file; parent plan backlog remapped to T1–T5.
- **2026-09-15**: T1 landed — CaptureLog SoA + Trace meta-first `data()`. Next: T2 filter index.
- **2026-09-15**: T2 landed — TraceFilterIndex + lean proxy MapModes. Next: T3 virtual viewport model.
- **2026-09-15**: T3 landed — `trace.cacheRows` viewport (default 128), cache prefill, overview meta sample, no full-history dataChanged on wrap. Next: T4 format/DBC off paint.
- **2026-09-15**: T4 landed — VisibleRowCache prefills ColSignal + color rules on window fill; paint is cache-hit. Next: T5 shell silence.
- **2026-09-15**: T5 landed — background Trace cursor-only; Graphic/Watcher silent when hidden; status ~5 Hz. Trace redesign T1–T5 complete for mainline.
- **2026-09-15**: Graphic Stage 2 — P1-1 (no raw twin) + P1-3 (overlay default, dirty/hidden budget).
- **2026-09-15**: B6 complete — hidden Graphic/DataWindow timers stopped; Phase B closed.
