# Trace / Graphic Performance Plan (A / B / C)

> **Status**: Active — Phase A done; Phase B: CaptureLog + Trace pull (B1/B2/B5 partial); B3/B4/B6/B7 next  
> **Last Updated**: 2026-09-14  
> **Goal**: Reach or exceed CANoe-class fluidity on Trace and Graphic under high bus load, many signals, and long history.

This document is the durable product decision for Trace/Graphic performance work.
Do not replace it with chat notes; update checkboxes and status here as work lands.

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
| B3 | Shared sample store keyed by `(canId, startBit, length)` — Graphic tabs subscribe | pending |
| B4 | Decode / MinMax bucket update off GUI (worker or timer on non-GUI thread) | pending |
| B5 | Trace tabs share CaptureLog + filter cursor (stop per-tab 1e6 rings where possible) | partial |
| B6 | Hidden Graphic: append samples optional; never composite | partial |
| B7 | Prefer overlay / fewer AxisRects when signal count is high | pending |

**B5 notes (2026-09-14)**: Trace display still uses a per-tab ring (`trace.maxFrames`), but live ingest no longer runs on the shell fan-out stack. Trace tabs pull via `CaptureLog::copyAfterSeq` on a timer; `capture.maxFrames` (default 500k) holds burst history. Background tabs use a slower pull/flush budget. Full “tabs as cameras only” (no per-tab ring) remains Phase C-adjacent.

**B6 notes**: Hidden Graphic already skips `replot`; Watcher/IOGraph skip ingest when not visible.

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
| `src/core/player.cpp` | Batch `framesPlayed` |

---

## Decision log

- **2026-09-14**: Persist A/B/C plan; implement A then B (not C yet). Analysis concluded GUI per-frame fan-out + Trace sort/scroll + Graphic paint/downsample are the primary bottlenecks for many-signals + long-history loads.
- **2026-09-14**: Phase A (A1–A11) implemented. Phase B: CaptureLog append on the measurement path (B1–B2). Remaining: shared sample store, off-GUI decode, Trace reading CaptureLog, overlay-for-many-signals (B3–B7). Phase C still deferred.
- **2026-09-14**: Trace Phase B5 partial — live Trace ingest pulls from CaptureLog (decoupled from shell); `enqueueFrames` + pending soft-cap; background tab budget; `capture.maxFrames`; status-bar / Watcher / IOGraph throttling. Next: B3 shared sample store for Graphic.
