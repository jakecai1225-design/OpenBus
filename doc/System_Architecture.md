# OpenBUS System Architecture Specification

> **Version**: v2.0  
> **Last Updated**: 2026-08-31  
> **Format**: Structured Text + Draw.io Diagram Reference

---

## 🎯 Overview

This document provides a comprehensive overview of the OpenBUS CAN Bus Analysis Tool system architecture, including component relationships, data flow, and deployment structure.

See also: [`architecture.drawio`](architecture.drawio) for visual diagram.

---

## 🏗️ Architectural Principles

### Core Design Philosophy

1. **Separation of Concerns**: Clear boundaries between UI layer, business logic, and hardware abstraction
2. **Plugin-based Extensibility**: Dynamic module loading via ODP (OpenBUS Driver Package) format
3. **Cross-platform Compatibility**: All source code in English to ensure build stability across Windows/Linux/macOS
4. **Incremental Refactoring**: Gradual migration from Qt Widgets to modern UI frameworks without disrupting existing functionality

---

## 📦 High-Level Architecture

### Component Layers

```
┌─────────────────────────────────────────────────────────────┐
│                    Presentation Layer                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │
│  │ MainWindow   │  │ Tab Views    │  │ Side Panels      │   │
│  │ Controls     │  │ (Trace/      │  │ (Activity Bar/   │   │
│  │              │  │  Graphic/     │  │  Sidebar)        │   │
│  │              │  │  UDS/Flow)    │  │                  │   │
│  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘   │
└─────────┼─────────────────┼──────────────────┼──────────────┘
          │                 │                  │
┌─────────┴─────────────────┼──────────────────┼──────────────┐
│              CORE BUSINESS LOGIC LAYER        │              │
│  ┌───────────────────────────────────────────┴─────────────┐ │
│  │              openbus_ui Static Library                  │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌────────────────┐  │ │
│  │  │ ModuleReg   │  │ ShellCtx    │  │ SignalBroker   │  │ │
│  │  └─────────────┘  └─────────────┘  └────────────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────┬───────────────────────────────────┘
                          │
┌─────────────────────────┴───────────────────────────────────┐
│             ABSTRACTION LAYER                                │
│  ┌─────────────────┐       ┌─────────────────────────────┐ │
│  │ Driver API      │       │ Protocol Stack              │ │
│  │ (QPlugin-based) │       │ (ASC/BLF/DBC/UDS parsers)   │ │
│  └────────┬────────┘       └─────────────┬───────────────┘ │
│           │                               │                 │
└───────────┼───────────────────────────────┼─────────────────┘
            │                               │
┌───────────┴───────────────────────────────┴─────────────────┐
│               HARDWARE & DATA SOURCES                        │
│  ┌───────────────┐  ┌───────────────┐  ┌─────────────────┐ │
│  │ ZLG Driver    │  │ Kvaser Driver │  │ File Import     │ │
│  │ (odp_zlg.dll) │  │ (odp_kvaser.d│  │ (ASC/BLF/CSV)   │ │
│  └───────────────┘  └───────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## 🔧 Core Components

### 1. Main Application (`openbus.exe`)

**Entry Point**: `src/main.cpp`

**Responsibilities**:
- Initialize Qt application framework
- Load dynamic plugin modules (ODP files)
- Configure build environment (Qt/MinGW/CMake paths)
- Launch main window with tabbed interface

**Dependencies**:
- `openbus_ui` static library (UI components)
- Plugin registry system
- Hardware driver interface

### 2. UI Layer (`openbus_ui` Static Library)

**Location**: `src/ui/`

**Key Classes**:

| Class | Purpose | Dependencies |
|-------|---------|--------------|
| `MainWindow` | Main window controls, tab management, signal routing | None |
| `SidebarPanels` | Project tree view, tool palette display | `ModuleRegistry` |
| `ActivityBar` | Navigation toolbar with icon-based actions | None |
| `MeasurementSetupView` | Measurement configuration canvas | DBC parser |
| `TraceView` | CAN frame list display, filtering, navigation | `CanFrame`, `DBCMessage` |
| `GraphicView` | Signal waveform plotting (QCustomPlot) | `CanFrame` stream |
| `UDSView` | UDS diagnostic request/response viewer | UDS protocol stack |
| `FlowView` | Data flow visualization interface | Signal topology |

**Layout Pattern**: VS Code-inspired layout with fixed sidebar and resizable central area

### 3. Plugin System

**Architecture**: QPlugin-based dynamic loading mechanism

**Plugin Interface**: `IBusinessModule`

```cpp
class IBusinessModule : public QObject {
    Q_PLUGIN_METADATA(IID "com.openbus.module" INTERFACE IBusinessModule)
public:
    virtual QString name() const = 0;
    virtual QWidget* createPage() const = 0;
};
```

**Plugin Types**:
- **Hardware Drivers** (`.odp`): ZLG, Kvaser, PEAK, SLCAN support
- **Business Modules**: Trace, Graphic, UDS, Flow views
- **Python Tools**: CAN/DBC/UDS analysis scripts via PyQt6 embedding

### 4. Module Registry

**Purpose**: Centralized plugin discovery and lifecycle management

**Methods**:
- Scan `plugins/` directory for valid ODP files
- Load plugins on demand (lazy initialization)
- Provide dependency injection to loaded modules

---

## 🔄 Data Flow Patterns

### Real-time CAN Frame Processing

```
CAN Device → Driver Plugin → CanFrame Object → Signal Broker → Multiple Views
                                    ↓
                            Filter Engine
                                    ↓
                            Trace View (List Display)
                            Graphic View (Waveform)
                            Flow View (Topology)
```

### Offline File Import Flow

```
User Selects File → Format Detection (ASC/BLF/CSV) → Parser Selection
                                           ↓
                                    DBC Decoding
                                           ↓
                              Populate CanFrame Objects
                                           ↓
                              Signal Broker Distribution
```

---

## 💾 Persistence Strategy

### Configuration Storage

| Type | Location | Format |
|------|----------|--------|
| User Settings | `QSettings` (Windows Registry) | Binary |
| Workspace State | `.obw` JSON file | JSON |
| Filter Presets | `*.sfilter` | JSON |
| DBC Database | External `.dbc` file | Vector DBC format |

### Cache Management

- **Frame Buffer**: Ring buffer with 1M frame limit (overwrites old frames)
- **Row Caching**: Visible rows only, lazy formatting per viewport refresh
- **Refresh Rates**: Configurable (50ms high / 100ms medium / 200ms low / paused)

---

## 🛠️ Build System

### Tools and Versions

| Tool | Version Path | Role |
|------|-------------|------|
| Qt | `C:/Qt/6.11.2/mingw_64` | GUI framework + build tools |
| MinGW GCC | `C:/Qt/Tools/mingw1310_64` | C++ compiler (g++ 13.1.0) |
| CMake | `C:/Program Files/CMake` | Build system generator |
| Ninja | `C:/Qt/Tools/ninja` | Fast incremental builder |

### CMake Structure

```cmake
# Root CMakeLists.txt
project(openbus VERSION 1.0 LANGUAGES CXX)
add_subdirectory(src/core)       # Business logic
add_subdirectory(src/ui)         # Qt widgets
add_subdirectory(drivers/)       # Plugin ODPs
add_subdirectory(tests/)         # Unit tests
add_subdirectory(resources/)     # Icons, QSS styles
```

### Compilation Flags

- **Debug**: `-O0 -g3 -DDEBUG` (full symbols, slow)
- **Dev**: `-O1 -g1` (balanced for daily development) ⭐ Recommended
- **Release**: `-O3 -DNDEBUG` (max optimization, no debug info)
- **RelWithDebInfo**: `-O2 -g` (optimized with symbols for release builds)

---

## 🚀 Deployment Strategy

### Runtime Dependencies

After building, execute `windeployqt.exe` to copy required DLLs:

```bash
windeployqt.exe build/bin/openbus.exe
```

**Copied Automatically**:
- Qt Core/Gui/Widgets DLLs
- Platform plugin (`platforms/qwindows.dll`)
- Styles plugin (`styles/qmodernwindowsstyle.dll`)
- PrintSupport DLL (for qcustomplot static linkage)
- Font directory (`lib/fonts/`)

**Manual Copy Required**:
- `Qt6PrintSupport.dll` (qcustomplot static library dependency)
- Custom fonts (optional: DejaVu, Noto Sans)

---

## 🔍 Key Design Decisions

### Decision 1: Static Library for UI

**Choice**: Bundle all Qt widgets into `openbus_ui.a` static library instead of separate DLL

**Rationale**:
✅ Simpler deployment (no DLL version conflicts)  
✅ Faster startup (no runtime linking overhead)  
✅ Easier symbol export management

**Trade-offs**:
❌ Larger executable size (~15MB vs ~8MB shared)  
❌ Slower incremental rebuilds (full relink on each change)

### Decision 2: VS Code Layout Pattern

**Choice**: Fixed left sidebar + resizable central edit area

**Rationale**:
✅ Familiar mental model for developers  
✅ Maximizes workspace utilization  
✅ Consistent behavior across platforms

**Implementation**: Use `QSplitter` with stored sizes persisted in QSettings

### Decision 3: Plugin-Based Architecture

**Choice**: QPlugin-based dynamic loading instead of hardcoded modules

**Rationale**:
✅ Zero-touch extensibility (new ODPs auto-discovered)  
✅ Isolated crashes (plugin failure doesn't crash host)  
✅ Language flexibility (Python plugins via embedding)

**Pattern**: `IBusinessModule` interface with standard metadata methods

---

## 📊 Performance Considerations

### Optimization Strategies

1. **Lazy Initialization**: Only load plugins when user accesses corresponding tab
2. **Viewport Clipping**: Render only visible rows in TraceView (2000 row viewport proxy)
3. **Signal Debouncing**: 100ms timer batch updates for group counters
4. **Ring Buffer**: 1M frame circular storage prevents memory unbounded growth
5. **Async Parsing**: File import runs in worker thread, progress shown in status bar

### Known Bottlenecks

- **DBC Signal Decoding**: O(n²) lookup in large DBC files (>10,000 signals) → TODO: Hash map optimization
- **UTF-8 Encoding**: Console output fallback encoding issues → SOLVED by English-only policy
- **Ninja Detection**: Automatic detection improved in v2.0 (now checks Qt Tools path)

---

## 🔮 Future Roadmap

### Phase I (Completed)
- [x] Reduce docs from ~50 to 15 core documents
- [x] Migrate all paths and filenames to English
- [x] Integrate Ninja build system auto-detection

### Phase II (Q3 2026)
- [ ] WebEngine Progressive Migration (Vue3 frontend)
- [ ] Add Flow module signal topology visualization
- [ ] Optimize DBC lookup with hash indexing

### Phase III (Q4 2026+)
- [ ] Cross-platform testing (Linux/macOS)
- [ ] CI/CD pipeline integration (GitHub Actions)
- [ ] AI-assisted bug prediction engine

---

## 📚 Related Documentation

- **[Requirements Specification]**(Requirements_Specification.md) - Feature breakdown
- **[Trace Module Design]**(Trace_Module_Design.md) - Detailed trace view specs
- **[Plugin System Design]**(Plugin_System_Design.md) - Plugin architecture deep-dive
- **[Build Environment Setup]**(Build_Environment.md) - Toolchain configuration

---

**Document Status**: ✅ Active (Architectural baseline approved)  
**Diagram Reference**: See [`architecture.drawio`](architecture.drawio) for visual representation
