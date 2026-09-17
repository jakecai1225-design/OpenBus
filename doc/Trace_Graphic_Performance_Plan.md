# Trace / Graphic Performance Plan (A / B / C)

> **Status**: Active — Trace T1–T5 + Graphic Stage 2 + B6 done; UX U1–U5 done; next = measure / P2 if needed  
> **Last Updated**: 2026-09-17  
> **Goal**: Reach or exceed CANoe-class fluidity on Trace and Graphic under high bus load, many signals, and long history.

This document is the durable product decision for Trace/Graphic performance work.
Do not replace it with chat notes; update checkboxes and status here as work lands.
Agent shortcut: `.cursor/rules/trace-graphic-performance.mdc` (alwaysApply).

**Trace redesign (primary)**: `doc/Trace_Virtual_List_Redesign.md` — Wireshark/CANoe-style virtual list; do not treat batch/timer caps as the main fix.

---

## Optimization backlog (P0 / P1 / P2)

Prioritized for Trace + Graphic smoothness. Work top-down; land one vertical slice at a time.

### Trace redesign T0–T5 (do these first)

| # | Item | Status |
|---|------|--------|
| T0 | Freeze Trace budgets / acceptance checklist | done (doc) |
| T1 | CaptureLog SoA + cheap `frameMetaAt` (P0-3 part) | done |
| T2 | Filter index beside CaptureLog (P0-2) | done |
| T3 | Virtual viewport Trace model (C1 brought forward) | done |
| T4 | DBC / ColSignal / color off paint (P0-3 + P1-2) | done |
| T5 | Shell / background silence (P0-4 / B6) | done |

### P0 — mapped / residual

| # | Item | Status |
|---|------|--------|
| P0-1 | Graphic MinMax / LOD in SampleStore worker | done |
| P0-2 | Trace filter index | → **T2** |
| P0-3 | Trace cheap `frameAt` / format | → **T1 + T4** |
| P0-4 | Finish B6 shell silence | → **T5** |

### P1 — Graphic Stage 2 (after Trace T3)

| # | Item | Status |
|---|------|--------|
| P1-1 | Graphic: remove per-view rawData twin of SampleStore | done |
| P1-2 | Trace: DBC / ColSignal / color off paint | → **T4** (done) |
| P1-3 | Graphic: tighter display budget | done |
| P1-4 | Multi-tab CPU budget: full rate only for visible page | → **T5** (done) |

### P2 — Phase C (later)

| # | Item | Status |
|---|------|--------|
| P2-1 / C1 | Custom virtual Trace list painter | → **T3** (thin model first; custom paint if needed) |
| P2-2 / C2 | GPU / custom Graphic strip compositor | deferred |
| P2-3 / C3 | Full subscription engine + LOD pyramid | deferred |

---

## Problem Summary

Stutter is **architectural**, not a single bug:

- Device/sim already bulk-dequeues (up to 512 frames / 2 ms), but the shell still
  fans out **one GUI-thread callback per frame**.
- Trace and Graphic share that same hot slot with Flow / stats / Watcher / status bar.
- Long history + many signals amplify Qt model notify and QCustomPlot paint costs.

**Product rule**: Trace and Graphic are the two make-or-break pages. Fix them together;
optimizing only one side still lets the other starve the GUI thread.

---

## Target Architecture (end state)

```text
Device / sim threads
  → lock-free frame queue (exists today)
  → CaptureLog (SoA / fixed-size ring — single source of truth)
       → Decode / subscription worker: canId → per-signal sample rings
       → Trace filter index (incremental bitset / skip list)
       → Graphic LOD / MinMax buckets (incremental)
  → GUI 16–33 ms compositor: visible Trace rows + visible Graphic pixels only
```

Hard rules:

1. Measurement ≠ UI (decode / filter / ingest never in `paintEvent` / `replot`).
2. One history, many windows (tabs are cameras, not full ring copies).
3. Subscribe, do not broadcast (unsubscribed IDs cost ~0 in Graphic).
4. Display budget = pixels / visible rows (independent of bus fps).
5. Hidden pages do not composite (optional low-rate background record).
6. Trace must not use `QAbstractItemModel` as a million-row database long-term.

---

## Phase A — Incremental (must land first)

**Intent**: Keep existing Qt models / QCustomPlot; remove per-frame UI waste.  
**Expected**: ~5–20× headroom; usable under medium–high load on the visible tab.  
**Not enough alone** for stable CANoe-class behavior at extreme load.

| # | Item | Area | Status |
|---|------|------|--------|
| A1 | Emit `framesGenerated(QVector<CanFrame>)` from simulator / device drain; keep single-frame signal for compat | Ingress | done |
| A2 | `MainWindow::onFramesReceived` batch path; throttle status-bar updates | Shell | done |
| A3 | TraceModule / GraphicModule / FlowModule `onFrames` batch invoke | Modules | done |
| A4 | Remove per-frame `scrollToBottom` in TraceModule; scroll only on `framesCommitted` | Trace | done |
| A5 | Default Trace sort: append-only (no ColNo merge / `layoutChanged`) | Trace | done |
| A6 | Wire `trace.maxFrames` from AppConfig on TraceTab create | Trace | done |
| A7 | Ring wrap: notify viewport / shifted range only (not full million-row `dataChanged`) | Trace | done |
| A8 | Graphic: default scatter off; limit AA; honor `graphic.fps` / `graphic.antialiasing` | Graphic | done |
| A9 | Graphic: `canId → signal indices` hash; remove hot-path `spdlog::debug` arg work | Graphic | done |
| A10 | Graphic: skip `replot` when not visible; grow ring capacity (no eager 1e6 alloc) | Graphic | done |
| A11 | Player: batch `framesPlayed` when tick advances many frames | Playback | done |

---

## Phase B — Hybrid architecture (next mainline)

**Intent**: Shared capture + off-GUI decode/downsample; QCustomPlot only paints ≤ ~2×width points.  
**Expected**: Visible page 30–60 fps with tens of signals and multi-minute windows.

| # | Item | Status |
|---|------|--------|
| B1 | `CaptureLog` ring (process-unique in `openbus_data`) — append batch, seq, capacity | done |
| B2 | Shell appends every measurement batch into CaptureLog once | done |
| B3 | Shared sample store keyed by `(canId, startBit, length)` — Graphic tabs subscribe | done |
| B4 | Decode / MinMax bucket update off GUI (worker or timer on non-GUI thread) | done (decode + LOD) |
| B5 | Trace tabs share CaptureLog + filter cursor (stop per-tab 1e6 rings where possible) | done |
| B6 | Hidden Graphic: append samples optional; never composite | done |
| B7 | Prefer overlay / fewer AxisRects when signal count is high | done |

**B5 notes (2026-09-15)**: Live Trace is a CaptureLog camera — `syncFromCaptureLog` updates rowCount/seq only; `frameAt` reads the shared ring. No per-tab live frame copy. Offline import and overwrite mode still use a local ring (`leaveCaptureCameraForLocal`). `trace.maxFrames` sizes the offline fallback ring; `capture.maxFrames` is the live history limit.

**B5 notes (2026-09-14)**: Trace display still uses a per-tab ring (`trace.maxFrames`), but live ingest no longer runs on the shell fan-out stack. Trace tabs pull via `CaptureLog::copyAfterSeq` on a timer; `capture.maxFrames` (default 500k) holds burst history. Background tabs use a slower pull/flush budget. Full “tabs as cameras only” (no per-tab ring) remains Phase C-adjacent.

**B3 notes (2026-09-15)**: `SampleStore` in `openbus_data` — subscribe/refcount by SampleKey; shell `ingestFrames` once; GraphicView pulls on a timer into local display rings. Unsubscribed IDs cost ~0. Multi-tab same signal shares one decoded ring.

**B4 notes (2026-09-15)**: `SampleStore::ingestFrames` queues batches to a low-priority worker thread; decode + ring push run off GUI. `seriesUpdated` crosses threads via queued metatype. History `appendSamples` stays synchronous.

**P0-1 / B4 LOD (2026-09-15)**: Worker `pushOne` maintains running Y range + incremental MinMax LOD buckets (`samplesPerBucket ≈ capacity/4096`). Graphic `refreshDisplayData` uses `copyDownsampled` (O(buckets)); falls back to local raw downsample if cold. Cursors still use per-view `rawData` (P1-1). Next P0: Trace filter index.

**Trace stream (2026-09-15)**: Player 16 ms tick + 256-frame cap (hold clock if more due); Trace camera syncs at most 256 rows / 16 ms instead of jumping to CaptureLog tip every 50 ms.

**B7 notes (2026-09-15)**: `graphic.overlayAutoThreshold` (default 2) auto-switches Y-axis to OverlaySelected when adding signals; user combo changes set `m_yAxisModeUserLocked` so manual Separate is respected. P1-3 also defaults new views to OverlaySelected.

**B6 notes (2026-09-15)**: Hidden Graphic stops pull / value / replot timers after tip-cursor advance; Watcher and DataWindow stop refresh timers when hidden; shell skips DataWindow ingest when not visible. SampleStore still ingests once for all subscribers.

**B6 notes (earlier)**: Hidden Graphic already skipped `replot` and slowed SampleStore pull; Watcher/IOGraph skip ingest when not visible.

---

## Phase C — Exceed CANoe (later)

| # | Item | Status |
|---|------|--------|
| C1 | Custom virtual Trace list painter (optional keep thin Qt model for selection) | deferred |
| C2 | GPU / custom strip compositor for Graphic | deferred |
| C3 | Full subscription engine + LOD pyramid | deferred |

---

## Acceptance (manual)

1. Live hardware or simulator at high fps with Trace auto-scroll on — UI remains interactive.
2. Graphic with ≥20 signals, ≥30 s window, long run — no multi-second freezes; visible tab stays smooth.
3. Two Trace + two Graphic tabs open — background tabs do not dominate CPU.
4. Playback of a large log — no per-frame GUI storm; progress still correct.
5. Switch projects / stop measurement — no use-after-free or leftover fans.

---

## Related code (entry points)

| Path | Role |
|------|------|
| `src/core/cansimulator.cpp` / `candevicemanager.cpp` | Drain → emit |
| `src/ui/mainwindow_frameflow.cpp` | Fan-out |
| `src/ui/tracemodule.cpp` / `traceview.cpp` | Trace ingest + scroll |
| `src/models/cantracemodel.cpp` / `cantraceproxymodel.cpp` | Flush / sort / wrap |
| `src/ui/graphicmodule.cpp` / `graphicview.cpp` | Decode + replot |
| `src/ui/graphic/downsample.cpp` | Viewport MinMax |
| `src/core/appconfig.cpp` | `trace.maxFrames`, `capture.maxFrames`, `graphic.fps`, `graphic.maxSamples` |
| `src/core/capturelog.cpp` | Shared measurement ring; Trace pull cursor |
| `src/core/samplestore.cpp` | Shared decoded samples; Graphic subscribe/pull |
| `src/core/player.cpp` | Batch `framesPlayed` |

---

## Decision log

- **2026-09-14**: Persist A/B/C plan; implement A then B (not C yet). Analysis concluded GUI per-frame fan-out + Trace sort/scroll + Graphic paint/downsample are the primary bottlenecks for many-signals + long-history loads.
- **2026-09-14**: Phase A (A1–A11) implemented. Phase B: CaptureLog append on the measurement path (B1–B2). Remaining: shared sample store, off-GUI decode, Trace reading CaptureLog, overlay-for-many-signals (B3–B7). Phase C still deferred.
- **2026-09-14**: Trace Phase B5 partial — live Trace ingest pulls from CaptureLog (decoupled from shell); `enqueueFrames` + pending soft-cap; background tab budget; `capture.maxFrames`; status-bar / Watcher / IOGraph throttling. Next: B3 shared sample store for Graphic.
- **2026-09-15**: Phase B3 — `SampleStore` subscribe/ingest/pull; Graphic decode once per key; views timer-pull. Next: B4 off-GUI decode, B7 overlay for many signals.
- **2026-09-15**: Phase B4 — SampleStore worker-thread ingest; B7 — auto overlay above `graphic.overlayAutoThreshold`.
- **2026-09-15**: Phase B5 — Trace CaptureLog camera (no per-tab live ring); offline/overwrite keep local ring.
- **2026-09-15**: Persist P0/P1/P2 backlog in this doc + `.cursor/rules/trace-graphic-performance.mdc`.
- **2026-09-15**: P0-1 — SampleStore worker MinMax LOD + `copyDownsampled` / `valueRange`; Graphic display path prefers store LOD.
- **2026-09-15**: Graphic crash/lag hotfix — capped pull, LOD-only display lock path, ingest queue soft-cap.
- **2026-09-15**: Trace/Graphic time axis — use file/`CanFrame::timestamp` only (no PC-now on playback/plot); align Graphic ticker with Trace; import rebase; multi-file single rebase; CaptureLog history backfill.
- **2026-09-15**: User requested CANoe/Wireshark-class redesign; Trace first. Added `doc/Trace_Virtual_List_Redesign.md` (T0–T5). Next code: T1 SoA, then T2 filter index, then T3 virtual viewport. Graphic P1 after T3. Stop prioritizing batch/timer caps as primary fix.
- **2026-09-15**: T1 — CaptureLog SoA (`CaptureFrameMeta` + 64 B payload slabs); `frameMetaAt` / `copyDataAt`; Trace `data()` meta-first (payload only for Data/Signal / color filters / FrameRole). Next: T2 filter index.
- **2026-09-15**: T2 — `TraceFilterIndex` + proxy MapMode (Passthrough / AcceptIndex / DenseMaps). Live no-filter path is identity (no O(history) maps); filtered capture-order keeps accepted list only (binary-search reverse). Next: T3 virtual viewport.
- **2026-09-15**: T3 — Viewport `trace.cacheRows` (default 128); format-cache prefill on window move; overview uses `frameMetaAt`; removed full-history `dataChanged` on CaptureLog wrap. Next: T4.
- **2026-09-15**: T4 — VisibleRowCache prefills DBC ColSignal + color rules; `data()` paint path cache-hit; rule/DBC changes notify window only. Next: T5.
- **2026-09-15**: T5 — background Trace cursor-only + adopt-on-show; Graphic tip-only when hidden; Watcher timer stopped when hidden; status bar ~5 Hz. Next: Graphic Stage 2 (P1).
- **2026-09-15**: P1-1 — Graphic store-backed signals no longer twin `rawData`; pull advances cursor only; display/cursors/`fit*` use SampleStore (`copyDownsampled` / `valueAtTime` / `timeRange`). P1-3 partial: skip `userHidden` in `refreshDisplayData`. Next: finish P1-3 overlay/dirty budget.
- **2026-09-15**: P1-3 — OverlaySelected default; `overlayAutoThreshold` default 2; per-signal `displayDirty` (no global twin rebuild); skip hidden in refresh; drop duplicate cursor update on replot. Graphic Stage 2 complete. Next: P2 / measure.
- **2026-09-15**: B6 complete — hidden Graphic stops pull/value/replot timers (tip once on hide); DataWindow timer + shell fan-out gated on visibility. Phase B closed. Next: manual acceptance; P2 (C1b/C2/C3) only if still heavy.
- **2026-09-17**: Trace UX track opened (`doc/Trace_UX_Redesign.md`). Performance T0–T5 unchanged. **U1–U5 landed** (action/status strip, filter chips, right Signals pane, sticky columns, To Graphic + status precision). Next: manual acceptance; C1b only if still heavy.
- **2026-09-17**: **U6 Trace chrome densify** — actions after narrow filter edit; in-tab status strip removed; shell status bar slimmed (`tab | frames | Sel | time`). See `doc/Trace_UX_Redesign.md`.
