# DBC Studio vs Vector CANdb++

> Research date: 2026-09-22  
> Goal: make **DBC Studio** a product that matches daily CANdb++ workflows and **surpasses** them on experience, closed-loop tooling, and openbus integration.  
> Sources: [CANdb++ Manual](https://cdn.vector.com/cms/content/products/candb/Docs/CANdb_Manual_EN.pdf), [Admin Fact Sheet](https://cdn.vector.com/cms/content/products/candb/Docs/CANdb_Admin_FactSheet_EN.pdf), [Extended Multiplexing AN](https://cdn.vector.com/cms/content/know-how/_application-notes/AN-ION-1-0521_Extended_Signal_Multiplexing.pdf).  
> Related: [Domain_Suite_Competitive_Requirements.md](Domain_Suite_Competitive_Requirements.md) §1, [Plugin_Domain_Suites.md](Plugin_Domain_Suites.md) §7.

---

## 1. Product boundary

| In scope | Out of scope |
|----------|--------------|
| `*.dbc` create / edit / save | Vector `*.mdc` proprietary DB |
| Attributes, value tables, matrix, consistency | Vehicles object type |
| Diff / merge / export / C codegen | Full ARXML authoring |
| J1939 attribute templates (later) | J1708 / ARINC / CANoe project bind |

Benchmark: **CANdb++** for day-to-day DBC; **Admin** features only where they apply to DBC files (compare, import lite, timing lite).

---

## 2. CANdb++ feature inventory

| Capability | CANdb++ | Admin | DBC Studio | Phase |
|------------|---------|-------|------------|-------|
| Create / modify DBC | yes | yes | yes | 0 done |
| Object tree (network / node / msg / signal) | yes | yes | yes | 0 done |
| Bit / message layout | yes | yes | yes | 0 done |
| Link Tx/Rx relations | yes | yes | **transmitter combo + Rx checklist** | 1 done |
| Communications matrix view | yes | yes | **interactive Matrix page** | 1 done |
| Value tables window + assign | yes | yes | **Value Tables page + Editor picker** | 1 done |
| User attribute defs + values | yes | yes | **Attributes page + BA_* round-trip** | 1 done |
| Consistency check + live window | yes | yes | Validate + save gate | 0–1 |
| Compare objects / DBs + CSV | partial / Admin | yes | Compare page | 0+ polish |
| Import objects / DBs | — | yes | **CSV → Editor import** | 2 done |
| Export DBC / CSV / XML | yes | yes | DBC + matrix + C | 0–3 |
| Extended multiplexing | yes | yes | **mux filter in layout** | 2 done |
| Timing / bus-load estimate | — | yes | **Timing page** | 3 done |
| Multi-network MDC | — | yes | no | boundary |

Tutorial path to replicate: create → link → **matrix** → value tables → attributes → consistency check.

---

## 3. Surpass strategy (experience)

1. **One suite**: Edit → Lint → Diff → Merge → Export without leaving the window.  
2. **VS Code chrome**: keyboard-first, single OUTPUT, flat chrome.  
3. **Bus-native**: save reloads host DBC; Trace / suites use it immediately.  
4. **AI Attach**: feed selected message / signal into AI Agent.  
5. **CI**: SARIF / JSON lint without a Vector license.

---

## 4. Phase checklist

### Phase 0 — Baseline (usability of what exists)

- [x] Editor tree + bit layout + save/load  
- [x] Validate lint + finding fields (`can_id`, `signal`)  
- [x] Double-click finding → Editor (`goto_editor_target`)  
- [x] Save gate on lint errors  
- [x] Save dialog **Review** opens Validate and re-runs lint  
- [x] Optional “Lint before save” toggle persisted  

### Phase 1 — Core parity

- [x] Interactive **Communications Matrix** (signal × node; Tx/Rx; jump to Editor)  
- [x] **Value Tables** manager + assign to signals (page + Editor VAL_TABLE_ picker)  
- [x] **Attributes** BA_DEF_ / BA_ / BA_DEF_DEF_ round-trip UI  
- [x] Tx/Rx linking UX polish (editable transmitter combo + receiver All/None)  

### Phase 2 — Edit depth

- [x] Undo / Redo (document history, Ctrl+Z / Ctrl+Y)  
- [x] Tree search / multi-select batch delete  
- [x] Extended multiplex layout preview (mux group filter + legend)  
- [x] Gen* attribute presets (Attributes → Presets)  
- [x] J1939 attribute pack (Attributes → Presets → J1939)  
- [x] CSV → signal import (Editor → Import CSV)  

### Phase 3 — Product win

- [x] Host DBC reload on save (best-effort `dbc.reload`)  
- [x] AI Attach from selection (Editor → AI)  
- [x] Export-and-open; timing lite (Timing page + open-after-export)  
- [x] Library depth (Favorites / Reveal / Clear recent)  

---

## 5. Decision log

- 2026-09-22: Product goal set to surpass CANdb++ on DBC workflows; phased plan locked; Phase 1 starts with Matrix page.  
- 2026-09-22: Out of scope confirmed: MDC, Vehicles, full ARXML, CANoe bind.  
- 2026-09-22: Phase 0 complete (Review save gate + lint toggle). Matrix page shipped (nav: Editor | Matrix | …).  
- 2026-09-22: Phase 1 Value Tables + Attributes shipped — SuiteToolbar chrome, VAL_TABLE_/BA_* round-trip tests, Editor assign picker; Gen* defs protected.  
- 2026-09-22: Phase 1 complete for core parity — Tx/Rx linking polished; next is Phase 2 (undo, multiplex depth, Gen*/J1939 presets).  
- 2026-09-22: Phase 2 started — Gen* / network attribute presets menu on Attributes (one-click past CANdb++ default dialogs).  
- 2026-09-22: UX pass — DBC Studio on suite_chrome workbench (activity bar, one chrome row with document actions, OUTPUT tool row, Ctrl+B/J); all pages SuiteToolbar + tuned forms / empty state.  
- 2026-09-22: Phase 2 complete — Undo/Redo, CSV import, J1939 presets, mux layout filter, multi-select delete; Phase 3 started (host reload + AI Attach).  
- 2026-09-22: Phase 3 complete — Timing lite bus-load page, export-and-open, Library favorites/reveal/clear. DBC Studio plan checklist fully done (MDC multi-network remains out of scope).  
- 2026-09-22: UX polish — OUTPUT collapsed by default (Ctrl+J); Editor tree auto-fit Name column + splitter mins; chrome path prefers filename; toolbar icon-only secondary actions.  
