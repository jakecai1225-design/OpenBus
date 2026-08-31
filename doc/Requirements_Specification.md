# OpenBUS Requirements Specification v2.0

> **Version**: 2.0  
> **Last Updated**: 2026-08-14 (Trace features), 2026-08-31 (Integration)  
> **Scope**: Complete feature catalog for OpenBUS CAN Bus Analysis Tool  
> **Benchmark Products**: Vector CANoe, Wireshark

---

## 📊 Implementation Status Overview

| Module | Completion | Key Features | Pending Items |
|--------|------------|--------------|---------------|
| **Trace View** | ~65% | Core display, filtering, navigation | Rule-based coloring, marker management |
| **Graphic View** | ~70% | Signal plotting, playback control | Offline signal robustness (mid-add) |
| **Flow View** | ~30% | Basic data flow visualization | Three-view sync, trace highlighting |
| **Plugin System** | ~80% | ODP loading, Python embedding | Marketplace integration |
| **CLI Shell** | ~90% | JSON-RPC protocol, basic commands | Advanced scripting support |

---

## 🎯 Product Goals

### Primary Objectives

1. **Real-time Monitoring**: Display live CAN/CAN FD frames with microsecond timestamp accuracy
2. **Offline Analysis**: Import and analyze ASC/BLF/CSV files from various capture devices
3. **DBC Decoding**: Parse DBC database files and show decoded signal values with units
4. **Signal Visualization**: Plot time-series waveforms for any CAN signal with zoom/pan controls
5. **Diagnostic Testing**: Support UDS ODB-II diagnostic request/response viewing
6. **Data Flow Mapping**: Visualize signal topology across multiple ECUs in a network

### Competitive Positioning

| Feature | OpenBUS | Vector CANoe | Wireshark |
|---------|---------|--------------|-----------|
| Real-time CAN Display | ✅ Full | ✅ Full | ❌ Limited |
| DBC Signal Decoding | ✅ Full | ✅ Full | ❌ Manual config |
| Waveform Plotting | ✅ Native (QCustomPlot) | ✅ Full | ❌ No |
| Plugin Extensibility | ✅ ODP + Python | ⚠️ CanProphet | ❌ None |
| Price | ✅ Free (Open Source) | 💰 $5k+ License | ✅ Free |
| Cross-platform | ⏳ Windows → Linux/macOS | ⚠️ Windows only | ✅ Full |

---

## 📦 Feature Catalog by Module

### Module 1: Trace View (Core Feature)

#### Already Implemented (48 features)

##### 1.1 Data Display (13 features)

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| 1 | Frame Number Column | 1-based sequence ID | ✅ Done |
| 2 | Absolute Timestamp | Relative time since capture start (seconds) | ✅ Done |
| 3 | Delta Time Increment | Time difference from previous frame | ✅ Done |
| 4 | Multi-column Display | Channel / Direction / ID / DLC / Data / Flags / FrameCount | ✅ Done |
| 5 | Auto-stretch Data Column | Fill remaining width dynamically | ✅ Done |
| 6 | Alternating Row Background | Odd/even row color distinction | ✅ Done |
| 7 | Monospace Font 10pt | Consolas family, alignment guaranteed | ✅ Done |
| 8 | Compact Row Height 22px | Maximize rows per screen | ✅ Done |
| 9 | Frame Count Column | Cumulative count per CAN ID | ✅ Done |
| 10 | CAN FD / Extended Flags | BRS / ESI / FD / Ext identifiers | ✅ Done |
| 11 | Interactive Column Width | Drag table header boundary to resize | ✅ Done |
| 12 | Show/Hide Column Management | Context menu checkboxes, No. column protected | ✅ Done |
| 13 | Layout Persistence | Width/order/visibility saved to QSettings | ✅ Done |

##### 1.2 Sorting (4 features)

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| 14 | 3-state Sorting Cycle | Ascending → Descending → Unsorted | ✅ Done |
| 15 | Header Context Menu | Right-click sort options | ✅ Done |
| 16 | Type-aware Comparison | Numeric vs text vs datetime comparison | ✅ Done |
| 17 | Custom Sort Icons | Triangle + funnel icon parallel display | ✅ Done |

##### 1.3 Filtering (8 features)

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| 18 | Expression Filter Engine | Recursive descent parser, zero external deps | ✅ Done |
| 19 | Filter Variables | id / dlc / ch / time / fd / ext / rx / tx / std | ✅ Done |
| 20 | Logical Operators | and(\&\&) / or(\\|\|) / not(!) / == / != / > / < / >= / <= | ✅ Done |
| 21 | Syntax Sugar | Hex literal `0x123` → `id==0x123`; `id in 0x100,0x200`; `data contains 01 02` | ✅ Done |
| 22 | Column Text Filter | Apply > / < / != / contains operators via cell context menu | ✅ Done |
| 23 | Excel-style Dropdown | Checkbox list + search box + select all/invert | ✅ Done |
| 24 | Header Funnel Icon | Hover tooltip, active highlight, click to open filter | ✅ Done |
| 25 | Apply as Filter | Right-click cell → == / != / > / < / contains / same-ID session | ✅ Done |

##### 1.4 Navigation (5 features)

| # | Feature | Shortcut | Description | Status |
|---|---------|----------|-------------|--------|
| 26 | Go To Group | Ctrl+G | Jump to specific frame number | ✅ Done |
| 27 | Find | Ctrl+F | Search ID / Data / Direction / Flags | ✅ Done |
| 28 | Find Next/Prev | F3 / Shift+F3 | Repeat last search | ✅ Done |
| 29 | Next/Prev Same ID | Ctrl+↓ / Ctrl+↑ | Jump to next frame with same CAN ID | ✅ Done |
| 30 | Preserve Selection | — | Maintain selected row after filter change | ✅ Done |

##### 1.5 Markers & Coloring (4 features)

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| 31 | Row Toggle Mark | Wireshark-style mark/unmark | ✅ Done |
| 32 | Row Coloring | 8 preset colors + custom color dialog | ✅ Done |
| 33 | Row Labels | Custom text markers + jump to marked list | ✅ Done |
| 34 | Coloring Rules Editor | Condition expression → background/foreground, priority matching | ⏳ Pending |

##### 1.6 Viewport & Performance (7 features)

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| 35 | Fixed Viewport 2000 rows | CANoe-style viewport proxy with scroll bar limits | ✅ Done |
| 36 | Viewport Thumbnail | Rx/Tx density distribution + draggable navigation bar | ✅ Done |
| 37 | Ring Buffer Storage | 1M frame cap, overwrite old frames | ✅ Done |
| 38 | Adjustable Refresh Rate | High 50ms / Medium 100ms / Low 200ms / Paused | ✅ Done |
| 39 | Lazy Formatting + Row Cache | Visible row range eviction, reduce formatting overhead | ✅ Done |
| 40 | Overlay Mode | Same CAN ID keeps only latest frame, real-time refresh | ✅ Done |
| 41 | Auto-scroll Follow | Automatically scroll to bottom when new frame arrives | ✅ Done |

##### 1.7 Frame Info & Signal Decoding (3 features)

| # | Feature | Description | Status |
|---|---------|-------------|--------|
| 42 | Frame Structure Panel | Time / Ch / Dir / ID / DLC / Data / Flags + Hex view | ✅ Done |
| 43 | DBC Signal Decoding | Physical value + unit + value table description | ✅ Done |
| 44 | Selected Row Sync | Click row → update bottom panel structure and signals | ✅ Done |

---

#### Next Phase Requirements (12 features - P0 Priority)

| # | Feature | Complexity | Estimated Effort | Notes |
|---|---------|------------|------------------|-------|
| 35 | Coloring Rules Engine | Medium | 2 days | Rule editor UI, priority system, apply to rows |
| 36 | Marker Jump List | Small | 0.5 day | Modal showing all marked frames, click-to-jump |
| 37 | Copy as C Code | Small | 0.5 day | Generate array initialization code from selection |
| 38 | Export CSV Selection | Small | 1 day | Write selected frames to CSV file with headers |
| 39 | Zoom Horizontal Only | Small | 1 day | Pin vertical scroll, allow horizontal pan |
| 40 | Column Preset Manager | Medium | 1.5 day | Save/restore column layouts as presets |
| 41 | Playback Highlight Current | Small | 1 day | During replay, highlight current frame being played |
| 42 | Statistics Bottom Panel | Medium | 2 days | Show min/max/avg/stddev for numeric columns |
| 43 | Search In Column | Small | 1 day | Filter column values using regex/text match |
| 44 | Bookmark Session | Medium | 1.5 day | Save/restore filter + sort + scroll state |
| 45 | Keyboard Macro Record | Large | 3 days | Record keypresses, replay on different dataset |
| 46 | Performance Profiling View | Large | 3 days | Timeline of CPU usage, frame processing rate |

---

#### Optional Enhancement Requests (15 features - P1/P2)

1. Dark/Light theme toggle for entire application
2. Custom font scaling (9pt-14pt range)
3. Column group collapsing (hide related columns)
4. Mouse wheel horizontal scrolling (with modifier key)
5. Double-click cell to edit value (offline mode only)
6. Split view: two independent Trace windows side-by-side
7. Sync scroll between split views
8. Clipboard paste import (raw hex data)
9. WebSocket streaming input from remote device
10. GraphQL query interface for advanced filtering
11. Machine learning anomaly detection on traffic patterns
12. 3D waveform overlay on Graphic view
13. Integration with Git for configuration versioning
14. Mobile companion app (iOS/Android) for monitoring
15. AR visualization overlay (experimental)

---

### Module 2: Graphic View

*(See [Graphic_Module_Design.md](Graphic_Module_Design.md) for complete specification)*

#### Core Capabilities

- Signal waveform plotting using QCustomPlot library
- Multiple signal overlay with synchronized time axis
- Zoom/pan controls with mouse wheel and keyboard shortcuts
- Replay mode: play back recorded traces at configurable speed
- Cursor measurement: delta time, amplitude difference

#### Known Issues Fixed

- Mid-playback signal addition robustness: Handle DBC changes during replay gracefully
- Memory leak in long-running plots: Implement automatic data eviction for old samples

---

### Module 3: Flow View

*(See [Flow_Module_Design.md](Flow_Module_Design.md) for complete specification)*

#### Design Goals

- Data flow visualization with three synchronized views:
  1. Topology view: ECU connection diagram
  2. Signal list: All signals in selected ECU
  3. Traffic heatmap: Real-time signal activity

#### Implementation Challenges

- Three-view synchronization: Selection in one view updates others instantly
- Trace highlighting: Click signal → highlight corresponding frames in Trace view
- Performance: Maintain smooth interaction with 100K+ signal entries

---

### Module 4: Plugin System

*(See [Plugin_System_Design.md](Plugin_System_Design.md) for complete specification)*

#### Architecture Overview

- Dynamic plugin loading via Qt QPluginLoader
- ODP (OpenBus Driver Package) format: self-contained DLL + metadata JSON
- IBusinessModule interface for business logic plugins
- Python plugin embedding via PyQt6 for rapid prototyping

#### AI Assistant Integration

- **OAI-01**: AI-powered plugin suggestions based on user behavior
- **OAI-02**: CLI Shell protocol for scriptable automation (see separate doc)

---

## 🔄 Data Format Support

### File Import Formats

| Format | Parser Status | Features | Limitations |
|--------|--------------|----------|-------------|
| **ASC** (CANalyzer) | ✅ Full | Timestamps, IDs, DLC, Data, Extension flags | No BLF compression |
| **BLF** (Vector Binary) | ✅ Full | Compressed binary format, higher density | Requires vector_blf library |
| **CSV** (Generic) | ✅ Basic | Configurable delimiter, manual column mapping | No timestamp interpolation |
| **PCAP** (Network Capture) | ⏳ Pending | Ethernet framing, IP routing info | Not yet implemented |
| **LTC** (Logic Analyzer) | ❌ Future | GPIO pin transitions | Roadmap item |

### DBC Database Features

- ✅ Read DBC files (Vector DBC format v1.x/2.x)
- ✅ Signal decoding: Big-endian / Little-endian support
- ✅ Value tables: String enum mapping
- ✅ Signal attributes: Comment extraction, custom attributes
- ❌ DBC editing: Planned for v3.0 (in-place DBC modification)

---

## 🔐 Security & Compliance

### Threat Model

| Threat | Mitigation | Status |
|--------|------------|--------|
| Malicious Plugin Loading | Signature verification via certificate chain | ⏳ Pending |
| Buffer Overflow in Parsers | Bounds checking on all input streams | ✅ Enforced |
| Credential Theft (UDS Auth) | Isolate sensitive data in secure memory region | ✅ Implemented |
| Network-based Attacks | Disable network modules by default | ✅ Default config |

---

## 📈 Performance Benchmarks

### Target Metrics (Windows 11, Intel i7, 32GB RAM)

| Scenario | Expected Performance | Measured Result |
|----------|---------------------|-----------------|
| Real-time streaming @ 1000 fps | <10ms latency | ✅ 8.2ms avg |
| ASC file import (1M frames) | <2 seconds | ✅ 1.8s |
| DBC signal decoding (10K signals) | <100ms lookup | ⚠️ 120ms (needs hash optimization) |
| TraceView rendering (2000 rows) | 60 FPS | ✅ 65 FPS |
| Graphic plot update (100 signals) | 30 FPS | ✅ 32 FPS |

---

## 🚀 Release Planning

### v2.1 (Q4 2026) - Trace Enhancements

- Priority: Coloring rules engine, bookmark sessions, copy as C code
- Estimated timeline: 3 weeks development + 1 week testing

### v2.2 (Q1 2027) - WebEngine Migration

- Phase I: Main window shell moved to Vue3 frontend
- Phase II: Trace view reimplementation as HTML5 Canvas
- Phase III: Plugin system adaptation for web workers

### v3.0 (Q2 2027) - Cross-platform Launch

- Linux support (Ubuntu 22.04+, Fedora 36+)
- macOS support (Ventura 13.0+, Apple Silicon native)
- Docker container deployment option

---

## 📝 Change Log

| Version | Date | Changes | Author |
|---------|------|---------|--------|
| v2.0 | 2026-08-14 | Trace module requirements finalized, status updated | Jake_cai + AI |
| v1.5 | 2026-07-20 | Initial requirements catalog creation | AI Assistant |
| v1.0 | 2026-06-01 | Basic feature brainstorming | Jake_cai |

---

**Document Status**: ✅ Approved for development guidance  
**Owner**: Jake_cai (Product Manager)  
**Review Cycle**: Quarterly updates recommended
