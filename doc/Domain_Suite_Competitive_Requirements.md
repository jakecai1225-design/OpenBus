# Domain Suite Competitive Requirements

> Research date: 2026-09-20  
> Purpose: PRD input for refactoring every domain suite to match [Suite_UI_UX_Design_System.md](Suite_UI_UX_Design_System.md) and beat mid-tier standalone tools.  
> Market leaders overall: **Vector** (CANoe / CANalyzer / CANdb++ / Indigo / vSignalyzer), **TOSUN TSMaster**, **Intrepid Vehicle Spy**, **PEAK**, plus domain specialists.

---

## Rollout map

| Suite | Top-3 references | UI baseline | Status |
|-------|------------------|-------------|--------|
| uds-suite | Softing DTS / CANoe Diag / TSMaster UDS | Design system reference | Done (chrome) |
| dbc-studio | Vector CANdb++ / TSMaster DB / Kvaser DB Editor | P0 shell + bit layout | P1 in progress |
| eds-studio | Vector CANeds / emotas DeviceExplorer / CANopenEditor | DBC Studio chrome | Phase 0–3 shipped |
| canopen-suite | CANoe.CANopen / emotas DeviceExplorer / port CCM | P0 shell + PDO page | P2 in progress |
| j1939-suite | CANalyzer.J1939 / Vehicle Spy / PEAK PCAN-Diag | Live / Transport / DM / Network | P2 in progress |
| obd-suite | Vector Indigo / RA DiagRA / Noregon JPRO | Setup + Scanner + Readiness | P2 in progress |
| tx-lab | CANoe IG+RBS / TSMaster TX / PCAN-Explorer | P0 shell + Replay page | P1 in progress |
| bus-security | CANoe+vTESTstudio fuzz / PlaxidityX / C2A | P0 shell + Findings | P3 in progress |
| protocol-hub | CANoe options / Vehicle Spy / TSMaster | P0 shell + Setup | P3 in progress |
| log-analysis | vSignalyzer / CANalyzer offline / CANtrace | Open / Trace / Report | P3 in progress |
| bus-utilities | CANoe+VH6501 / IVNT-03 / PEAK Bit Rate Tool | Scanner + Quality | P3 in progress |
| autosar-suite | DaVinci Configurator / EB tresos / ISOLAR + CANoe COM | DBC/EDS chrome | **AUTOSAR Studio** (BSW + live COM) |
| ethercat-suite | TwinCAT / EC-Engineer / SOEM | UDS chrome | ESI editor P0 |
| ai-agent | (platform agent — see aiagent.md) | P0 theme | Done (P0) |

Legacy thin plugins are **removed** from `plugins/` and `market.json` plugins[]. Do not reintroduce them.

Master plan: [Domain_Suite_Rollout_Plan.md](Domain_Suite_Rollout_Plan.md)

---

## 1. DBC Studio (`dbc-studio`)

> **Product goal:** surpass Vector CANdb++ on daily DBC workflows (features + UX + bus/AI integration).  
> Full inventory, gap matrix, and phase checklist: [`DBC_Studio_vs_CANdb.md`](DBC_Studio_vs_CANdb.md).

### Competitors
1. **Vector CANdb++ Admin** — matrix + tree, merge/diff, J1939 attributes, Vector toolchain handoff  
2. **TSMaster Database Editor + File Converter** — DBC/ARXML/LDF/FIBEX, codegen, encrypted DB  
3. **Kvaser Database Editor** — lightweight tree edit + append merge  

### Must-have (P0) — done
- Editor: nodes / messages / signals tree; signal bit layout preview  
- Validate / lint with navigable findings  
- Compare + merge two DBC files  
- Export subset / CSV / C structs (codegen)  
- Library / recent files; Setup-free (file-centric)  

### Phase 1 (parity with CANdb++ daily path) — done
- Interactive communications matrix (signal × node)  
- Value tables manager + attribute definitions/values UI  
- Save-gate lint with Review → Validate  
- Tx/Rx linking polish  

### UI / UX
- Workbench: activity bar + one chrome row (page title + document actions + layout toggles) + OUTPUT  
- Nav pages: Editor | Matrix | Value Tables | Attributes | Validate | Timing | Compare | Merge | Export | Library  
- Ctrl+B sidebar · Ctrl+J OUTPUT · Ctrl+O/S/N/Z/Y document  
- Flat VS Code chrome; elided path in chrome; lint-on-save gate  
- Diff view like CANdb++ (side-by-side or unified)  

### Out of scope (P2+)
- Full ARXML authoring; Vector MDC proprietary store  

---

## 1b. EDS Studio (`eds-studio`)

> **Product goal:** match Vector CANeds + emotas DeviceExplorer at the EDS/DCF file layer, and surpass them on one-window Edit→Lint→Diff→Export, CI SARIF, and openbus host/AI loops.  
> Full inventory: [`EDS_Studio_vs_CANeds.md`](EDS_Studio_vs_CANeds.md).

### Competitors
1. **Vector CANeds** — free EDS editor; tree OD; CiA-CODB; consistency check; scan-to-EDS  
2. **emotas CANopen DeviceExplorer** — DCF ParameterValue / DeviceCommissioning; PDO config  
3. **CANopenEditor** (open source) — EDS/DCF/XDD depth reference  

### Must-have (shipped)
- Editor: OD tree + definition form + FileInfo / DeviceInfo / Commissioning; undo/redo  
- Validate (deep) + lint-on-save + double-click → Editor; CSV/JSON/SARIF  
- PDO Map (file-layer); Library 301/402 + RPDO templates  
- Compare + merge-added; Export EDS/DCF/HTML/CSV/XDD lite  
- Analysis coverage / PDO bit estimate; AI Attach; host notify; scan import  

### Boundary
- Live SDO / NMT / LSS / Trace stay in **canopen-suite**  
- Shared parser: `plugins/_shared/edsparse.py`  

### UI / UX
- Same workbench as DBC Studio; OUTPUT collapsed by default (Ctrl+J)  
- Nav: Editor | PDO Map | Validate | Compare | Export | Library | Analysis  

---

## 1c. AUTOSAR Studio (`autosar-suite`)

> **Product goal:** match DaVinci Configurator / EB tresos / ISOLAR at the ARXML *configuration & validation* layer for Classic Platform COM extracts — without BSW/RTE codegen. ARXML is the handoff artifact to third-party stacks.  
> Full inventory: [`ARXML_Studio_vs_DaVinci.md`](ARXML_Studio_vs_DaVinci.md).

### Competitors
1. **Vector DaVinci Configurator** — BSW param + ARXML round-trip + generate  
2. **EB tresos Studio** — module config + ARXML  
3. **ETAS ISOLAR-A** — system description / extract  

### Must-have (shipped Phase 0–5)
- Editor: I-PDU / I-Signal tree + property form; undo/redo; lint-before-save  
- Spec glossary tips under fields; Validate deep + suggested fixes → Editor  
- Compare + Merge lite; Library starters; Export DBC / HTML / Extract  
- Analysis coverage; AI Attach; host notify  
- autosar-suite System → “AUTOSAR Studio” handoff  
- **Project** workspace: `project.json` + role-tagged ARXML (System / Extract / COM / ECUC)  
- ECUC-lite intermediates (Com / CanIf / PduR / CanNm) derived from COM; write under `work/` + `out/`  
- BSWMD-lite tips + import; cross-artifact Validate + recipe packs + SARIF  
- SWC / port mapping lite (no RTE codegen)  
- Full menubar: File / Edit / Import / Project / BSW / Validate / View / Help  
- **Import DBC** → COM I-PDUs/signals; sync Com / CanIf / PduR / CanNm / EcuC; retain `input/network.dbc`  
- Deep BSW ECUC starters (~40 modules) + `validate_bsw_set` cross-module checks  
- **Schema-driven BSW configurator**: curated EcucDefs JSON packs; Add/Remove/Duplicate containers; typed params; export `work/bsw/<Module>.arxml`  
- **Phase 8–12:** EcucDefs depth (Com≥200 / CanIf≥120); BSWMD-as-structure; preserve-unknown ARXML; Document↔Live sync; BSW undo; findings ack + release gate; wizards + DaVinci handoff report; multi-PDU Live; golden fixture CI  

### Boundary
- Live COM / NM / E2E / SecOC live inside **AUTOSAR Studio** (former autosar-suite merged)  
- No BSW/RTE/stack codegen — configure & validate only; intermediates are ARXML  
- Shared: `arxmlparse` + `arxml_project` + `arxml_bsw` + `arxml_dbc` + `arxml_ecuc_schema` + `ecuc_schemas/`  

### UI / UX
- Same workbench as DBC/EDS Studio; OUTPUT collapsed by default (Ctrl+J)  
- Activity workspaces + Side Bar sections (Config / Project / COM / Bus / Validate / Setup); Ctrl+B  
- Full menu bar: File / Edit / Import / Project / BSW / Validate / View / Help (+ Wizards)  
- BSW: schema instance tree per module; one ARXML each under `work/bsw/`  
- Validate + OUTPUT Findings: ack/filter/badges; Live multi-PDU monitor  

---

## 2. CANopen Suite (`canopen-suite`)

### Competitors
1. **Vector CANoe.CANopen + CANeds** — EDS→model→PDO link→trace  
2. **emotas CANopen DeviceExplorer** — SDO/PDO/NMT/LSS, CiA 402, DCF  
3. **port CCM** — network-wide PDO linking planner  

### Must-have (P0)
- Setup: node-ID, baud, EDS path  
- Object dictionary browser (typed values)  
- SDO upload/download; NMT commands; Heartbeat view  
- PDO mapping table (read/edit where safe)  
- Scan / LSS lite; shared log  

### UI / UX
- Nav: Setup | Network | OD | PDO | Drive(402) | Log  
- Object browser dominant (emotas pattern)  

### Out of scope (P2+)
- Full HIL restbus; CiA profile pack explosion  

---

## 3. J1939 Suite (`j1939-suite`)

### Competitors
1. **Vector CANalyzer/CANoe.J1939** — TP reassembly, DM1 monitor, address claim  
2. **Intrepid Vehicle Spy** — Address Manager, DM1 multi-ECU, no option packs  
3. **PEAK PCAN-Diag FD + J1939** — field/service form factor  

### Must-have (P0)
- PGN/SPN decode from DBC/J1939 DB  
- Address claim view + preferred address  
- Transport BAM/CMDT reassembly into logical PGNs  
- DM1 / DM2 list with SPN/FMI/OC  
- Request PGN helper; Log  

### UI / UX
- Nav: Setup | Live | Transport | Diagnostics(DM) | Network | Log  
- DM tree like Vehicle Spy  

---

## 4. OBD Suite (`obd-suite`)

### Competitors
1. **Vector Indigo** — J1979 / OBDonUDS / WWH-OBD, IUMPR, freeze frame  
2. **RA Consulting DiagRA / Silver Scan-Tool** — compliance PDF, automation  
3. **Noregon JPRO** — HD-OBD workshop (reference for workflow clarity)  

### Must-have (P0)
- Modes 01–0A (subset OK if 01/02/03/04/09 solid)  
- PID live + history chart  
- DTC read/clear; freeze frame  
- Readiness / monitors summary  
- Setup (ISO-TP IDs) + Log / export CSV  

### UI / UX
- Nav: Setup | Scanner | Freeze | DTC | Readiness | Log  
- Homologation density is **not** the goal — clarity is  

---

## 5. TX / Restbus Lab (`tx-lab`)

### Competitors
1. **Vector CANoe** — Generator Block, CAPL, RBS from DBC  
2. **TSMaster** — cyclic TX, signal generators, RBS node select  
3. **PEAK PCAN-Explorer 6** — transmit lists / node emulator  

### Must-have (P0)
- Manual + cyclic TX lists (save/load)  
- DBC signal-level edit → raw frame  
- Simple RBS: pick nodes from DBC, schedule TX  
- Replay BLF/ASC slice (basic)  
- Dashboard / panel lite; Log  

### UI / UX
- Nav: Setup | Transmit | Restbus | Replay | Panels | Log  
- Per-row cyclic toggle (TSMaster pattern)  

---

## 6. Bus Security (`bus-security`)

### Competitors
1. **Vector CANoe + vTESTstudio fuzz** — DBC-aware white-box fuzz  
2. **PlaxidityX (Argus)** — AutoTester + IDS policies  
3. **C2A EVSec** — risk-driven DevSecOps fuzz  

### Must-have (P0)
- Observational IDS / anomaly counters (no unsafe key brute by default)  
- Mutation fuzz from DBC fields (bounded, abortable)  
- Stress / flood with rate limits and kill switch  
- E2E / checksum helpers  
- Findings export (HTML/CSV); link out to UDS Suite for 27 audit  

### UI / UX
- Nav: Setup | Fuzz | IDS | Stress | Integrity | Findings | Log  
- Hard safety banners; never hide Abort  

---

## 7. Protocol Hub (`protocol-hub`)

### Competitors
1. **Vector CANoe/CANalyzer options** — widest protocol catalog  
2. **Intrepid Vehicle Spy** — multi-protocol one license  
3. **TSMaster** — tabbed monitors + RBS  

### Must-have (P0)
- One shell, many protocol pages: ISO-TP, NM, XCP, NMEA2000, ISOBUS, GB/T 27930, …  
- Shared timebase / shared log  
- Per-protocol enable without leaving the hub  
- Decode tables + filters  

### UI / UX
- Nav = protocol list (or grouped); Setup for channel map  
- Trace-first where possible  

---

## 8. Log Analysis (`log-analysis`)

### Competitors
1. **Vector vSignalyzer** — multi-format offline, sync cursors, reports  
2. **CANalyzer Offline** — same UI as online  
3. **CANtrace** — BLF/ASC + Python filter  

### Must-have (P0)
- Import BLF / ASC / CSV  
- Filter by ID / time / DBC signal  
- Trace + simple plot  
- Frame compare; quality / bus-load summary  
- Export filtered CSV / report snippet  

### UI / UX
- Nav: Open | Trace | Signals | Compare | Quality | Report  
- Offline-first (no Bus strip)  

---

## 9. Bus Utilities (`bus-utilities`)

### Competitors
1. **Vector CANoe + VH6501** — disturbance / conformance (aspirational)  
2. **Intrepid IVNT-03** — physical-layer bench  
3. **PEAK Bit Rate Calculation Tool** — bit timing planner  

### Must-have (P0)
- Classic + FD bit-timing calculator (PEAK-class)  
- Gateway / remap rules with persist + rate limit  
- Optional: ID scanner, quality snapshot (from old thin tools)  
- Log of utility actions  

### UI / UX
- Nav: Bit Timing | Gateway | Scanner | Quality | Log  
- Calculator-first; no fake HIL claims  

---

## 10. Shared non-functional requirements

1. Match [Suite_UI_UX_Design_System.md](Suite_UI_UX_Design_System.md)  
2. Ship only via `plugin_tool pack-suites` + `market/plugins`  
3. English UI; Chinese docs OK  
4. Prefer measurement over drive-by refactors (Trace/Graphic perf rule still applies to host)  
5. Each suite README section in this file’s checklist must pass before bumping market version  

---

## Implementation phases

| Phase | Work | Exit criteria |
|-------|------|---------------|
| **P0** | Design system + chrome helper; apply to all suite shells | Every suite: theme + icons + flat nav — **done 2026-09-20** |
| **P1** | dbc-studio + tx-lab deep UX (highest daily use after UDS) | Checklist P0 features usable |
| **P2** | canopen / j1939 / obd feature gaps from tables above | Competitive P0 closed |
| **P3** | protocol-hub / log-analysis / bus-security / bus-utilities | Hub + offline + utilities polished |
| **P4** | Market repack + README cleanup; optional standalone packaging | Market lists suites only |

UDS remains the quality bar for chrome. Feature depth per domain follows the tables above.

### Decision log

| Date | Note |
|------|------|
| 2026-09-20 | P0 chrome landed on all domain `app_shell.py` + AI Agent; GroupBox session/log frames replaced with SuiteToolbar / SuiteLogHost. |
| 2026-09-20 | P1 started: dbc-studio Editor/Validate/Compare/Export use `vscode_theme.block`; market repacked (10 suites + ai-agent). |
| 2026-09-20 | DBC Editor bit-layout preview (codec-aligned). TX Lab nav: Transmit / Restbus / Replay / Panels / Log; ASC replay page. |
| 2026-09-20 | P2: CANopen PDO mapping page; J1939 Live/Transport/Diagnostics/Network; OBD Setup + Mode 01 readiness. |
| 2026-09-20 | P3: Log Analysis Trace/Report on shared corpus; Protocol Hub Setup; Bus Utilities Scanner/Quality; Bus Security Findings. |
| 2026-09-20 | Protocol description editors: ARXML (System Tree/Validate/Export) + ESI (Tree/Validate/Save) + EDS FileInfo round-trip/Validate/Save. DBC Studio already full. LDF/FIBEX/ODX deferred. |
| 2026-09-20 | CANopen Suite workbench rewrite: activity bar + one chrome tab row (Network Scan|NMT, EDS Dictionary|Device|Check, Library CiA301|402). Session strip removed; Setup owns Node/EDS. Log is OUTPUT only. |
| 2026-09-22 | ARXML Studio Phase 0–2 shipped (`plugins/arxml-studio`): Spec/Validate/Compare/Merge/Export/Library/Analysis; shared `arxmlparse`; autosar System handoff. |
| 2026-09-22 | ARXML Studio Phase 3: project engineering (`project.json` roles), ECUC-lite intermediates, BSWMD-lite tips, cross-artifact validate + `out/` SARIF. |
| 2026-09-22 | ARXML Studio Phase 4: BSWMD import, fix recipe packs, SWC/port mapping lite (no codegen). |
| 2026-09-22 | ARXML Studio Phase 5: full menubar + DBC→COM/BSW sync; deep ECUC schemas; `validate_bsw_set`; still no stack codegen. |
| 2026-09-22 | AUTOSAR Studio schema-driven BSW configurator: `ecuc_schemas/*.json`, multiplicity UI, typed ECUC ARXML export. |
| 2026-09-23 | AUTOSAR Studio Phase 8–12: EcucDefs depth + BSWMD structure; preserve-unknown ARXML; Live sync; findings ack/release; wizards/handoff; multi-PDU Live; golden fixtures. Still no codegen. |

---

## Protocol description file editors

| Format | Suite | Top-3 references | Status |
|--------|-------|------------------|--------|
| **DBC** | `dbc-studio` | CANdb++ / TSMaster DB / Kvaser | Full create/edit/save/validate/compare/merge/export |
| **EDS/DCF** | `eds-studio` (+ canopen-suite live) | CANeds / emotas / CANopenEditor | Full file studio; live OD stays in canopen-suite |
| **ARXML / BSW config + live COM** | `autosar-suite` (AUTOSAR Studio) | DaVinci / tresos / ISOLAR | Project roles + BSW ECUC + live COM; no stack codegen |
| **ESI** | `ethercat-suite` ESI | TwinCAT / EC-Engineer / SOEM tools | Tree edit PDO/Objects + Validate + Save XML (no ENI) |
| **LDF / FIBEX / A2L / ODX** | — | — | Out of scope until dedicated LIN / measurement / UDS-ODX track |

### UX bar (all description editors)

- Same workbench as UDS: activity bar, one chrome row (editor tabs), OUTPUT
- Primary path: Open → edit tree → Validate → Save
- Hints on tooltips; no second toolbar row
- Explicit handoff where useful (ARXML → DBC for DBC Studio)
