# EDS Studio vs Vector CANeds / emotas DeviceExplorer

> Research date: 2026-09-22  
> Goal: make **EDS Studio** a file-centric product that matches daily CANeds / emotas EDS–DCF workflows and **surpasses** them on experience, CI lint, and openbus integration.  
> Sources: [CANeds](https://www.vector.com/se/en/support-downloads/downloads/add-ons-and-freeware/caneds/), [emotas CANopen DeviceExplorer](https://www.emotas.de/en/software-products-by-emotas/canopen-software-products/canopen-tools/canopen-deviceexplorer), [CiA 306](https://can-cia.org/can-knowledge/cia-306-series-electronic-device-description-edd), [CANopenEditor](https://github.com/CANopenNode/CANopenEditor) (open-source depth reference).  
> Related: [DBC_Studio_vs_CANdb.md](DBC_Studio_vs_CANdb.md), [Domain_Suite_Competitive_Requirements.md](Domain_Suite_Competitive_Requirements.md), [Plugin_Domain_Suites.md](Plugin_Domain_Suites.md).

---

## 1. Product boundary

| In scope | Out of scope |
|----------|--------------|
| `*.eds` / `*.dcf` create / edit / save | Full live SDO / NMT / LSS (→ `canopen-suite`) |
| OD tree, FileInfo / DeviceInfo, PDO map (file) | Network PDO linker (port CCM) |
| Validate / Compare / Export / Analysis | Full CiA 402 motion teacher |
| Profile library insert; XDD lite export | HIL restbus; SRDO packs |

Benchmark: **CANeds** for day-to-day EDS; **emotas DeviceExplorer** for DCF / ParameterValue depth.

---

## 2. Industry Top2

| # | Product | Role |
|---|---------|------|
| 1 | **Vector CANeds** | Free EDS editor: create / modify / test; hierarchical OD; symbolic types; CiA-CODB; scan-to-EDS |
| 2 | **emotas CANopen DeviceExplorer** | Device tool with DCF editor, PDO config, object browser–first UX |

Open-source reference: **CANopenEditor** (EDS/DCF/XDD, PDO UI, docs/codegen).

---

## 3. Capability matrix

| Capability | CANeds | emotas | EDS Studio |
|------------|--------|--------|------------|
| Create / modify EDS | yes | yes | yes |
| OD tree + attribute form | yes | yes | yes |
| FileInfo / DeviceInfo | yes | yes | yes |
| Consistency check | strong | partial | Validate + save gate |
| Profile / standard insert | CiA-CODB | profiles | Shared library: 301/302 + 20 device profiles + PDO/HB/SDO packs; Missing-only / Overwrite import |
| DCF ParameterValue / Commissioning | weak | **strong** | first-class |
| PDO mapping (file) | limited | strong | PDO Map page |
| Diff / multi-file | weak | project | Compare |
| Export HTML / CSV / DCF | weak | docs | Export |
| XDD (CiA 311) | limited | DeviceDesigner | lite |
| Scan-to-EDS | yes | scan OD | via canopen-suite hook |
| AI / host reload | no | no | yes |

---

## 4. Surpass strategy

1. One suite: Edit → Lint → Diff → Export without leaving the window.  
2. VS Code chrome (same as DBC Studio): Ctrl+B / Ctrl+J; OUTPUT collapsed by default.  
3. Bus-native: save notifies host; Apply path into canopen-suite.  
4. AI Attach for selected OD entry / whole EDS.  
5. CI: JSON / SARIF lint without a Vector license.

---

## 5. Phase checklist

### Phase 0 — Baseline

- [x] Scaffold `eds-studio` + market entry  
- [x] `EdsDocument` (path / dirty / undo)  
- [x] `_shared/edsparse` + round-trip tests  
- [x] Editor tree + Device/FileInfo + Save  
- [x] Validate minimal + lint-before-save  

### Phase 1 — CANeds daily path

- [x] Deep Validate (mandatory objects, types, PDO map refs, SubNumber) + profile coverage  
- [x] Library → shared CiA catalog (301/302 + device profiles + packs); Missing-only / Overwrite  
- [x] PDO Map page (file-layer)  
- [x] Double-click finding → Editor  

### Phase 2 — emotas DCF depth

- [x] DCF ParameterValue / DeviceCommissioning  
- [x] Compare  
- [x] Export HTML / CSV / DCF  
- [x] XDD lite export  

### Phase 3 — Product win

- [x] Host notify on save  
- [x] AI Attach  
- [x] Scan-to-EDS hook (delegate / grey if no bus)  
- [x] Analysis page  

---

## 6. Decision log

- 2026-09-22: Top2 locked — CANeds + emotas DeviceExplorer; file-centric `eds-studio`; bus stays in `canopen-suite`.  
- 2026-09-22: Parser lives in `_shared/edsparse.py` for suite reuse.  
- 2026-09-22: Out of scope — SRDO packs, full 402 teacher, network PDO linker, HIL.  
- 2026-09-22: Shipped Phase 0–3 in `plugins/eds-studio/` + market `eds-studio` entry.  
- 2026-09-22: Library expanded — CiA 301/401/402/404/406/418 + PDO packs; Starter templates (generic / DIO / AIO / servo / encoder / measure / battery / custom) for beginners.  
- 2026-09-27: Profile catalog moved to `_shared/canopen_profiles` (CiA 301/302 + 401–422/434/437 + packs). Library UX: Missing-only / Overwrite / Insert missing; Editor: Dup, → Param (DCF), SubNumber sync, Type column. Both `eds-studio` and `canopen-suite` consume the same catalog.  
- 2026-09-27: Validate adds profile coverage (auto from 0x1000 Device type, or force CiA xxx).  
