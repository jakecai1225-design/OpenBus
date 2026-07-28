# sin — CAN/CAN FD Bus Analyzer

<p align="center">
  <strong>Professional CAN/CAN FD bus message recording, playback, parsing & analysis tool</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Qt-6.8.3-green" alt="Qt 6.8.3">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue" alt="C++17">
  <img src="https://img.shields.io/badge/CMake-3.21+-orange" alt="CMake">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-yellow" alt="License">
</p>

---

## Overview

**sin** is a CAN/CAN FD bus message analysis tool inspired by [Wireshark](https://www.wireshark.org/), [CANoe](https://www.vector.com/canoe), and [Ozone](https://www.segger.com/products/development-tools/ozone-debugger/). It features a modern VS Code-style UI and provides a complete workflow from message recording to signal-level parsing, suitable for automotive electronics development, bus debugging, and protocol reverse engineering.

## Features

### Recording & Playback
- **Live Recording** — Capture bus messages in real-time from CAN devices; supports CAN 2.0A/B and CAN FD
- **File Playback** — Load `.sin` recording files with timestamp-accurate playback and variable speed (0.1x ~ 10x)
- **Seekable Progress** — Drag the progress bar to jump to any point in the timeline

### Trace View
- **Wireshark-style List** — Multi-column display: Timestamp, Channel, Direction, ID, DLC, Data, Flags
- **Column Sorting** — Click column headers to sort ascending/descending with intelligent type-aware comparison
- **Column Filtering** — Per-column filter conditions; ID column supports `>`, `<`, `!=` operators
- **Quick Filters** — Right-click column header for one-click Rx-only/Tx-only/unique-ID filters
- **Frame Info Panel** — Selected message displays fully decoded fields with DBC signal-level parsing
- **Hex Dump** — Selected message shown as Offset + Hex + ASCII dump

### DBC Signal Parsing
- **DBC Loading** — Load standard `.dbc` files with automatic message and signal definition parsing
- **Signal Tree** — Sidebar DBC panel displays Message → Signal hierarchy in a tree view
- **Signal Decoding** — Selecting a Trace message auto-decodes all signal values (signed/unsigned, big/little endian)
- **Signal Tracking** — Double-click a DBC signal to add it to the Graphic view for real-time plotting

### Graphic View
- **Real-time Waveform** — Plot signal value changes on a time-axis
- **Multi-signal Overlay** — Monitor multiple signals simultaneously with independent channel configuration
- **Interactive Zoom** — Mouse wheel zoom, drag to pan, waveform measurement supported

### Project Context Management
- **Multi-project** — Create multiple analysis projects, each managing its own CAN config, DBC files, and layout
- **Quick Switching** — One-click project context switching without reloading files
- **Persistence** — Project configs saved as `.sinproj` files for session restoration

### VS Code-style UI
- **Frameless Window** — Native title bar removed; menu bar at top with minimize/maximize/close buttons in the top-right corner
- **Global Sidebar** — ActivityBar + collapsible SideBar with functionally grouped panels
- **Splittable Editor** — Main area tabs support right-click "Split Right" / "Split Down" for side-by-side views
- **Fully Dockable** — Left, right, bottom, frame info, and hex dump panels are all closable, draggable, and dockable
- **Aero Snap** — Frameless window still supports native Windows window snapping and edge resizing

### Other Features
- **Command Terminal** — Built-in CLI supporting `help`, `clear`, `sim on/off`, `record`, `play`, `filter`, and more
- **CAN Simulator** — Built-in message simulator for generating test frames
- **Statistics Panel** — Real-time display of total frames, Rx/Tx distribution, CAN FD / Extended statistics
- **Theming** — QSS-driven dark menu bar + light content area, fully customizable

## Architecture

```
sin/
├── CMakeLists.txt              # Top-level CMake config
├── src/
│   ├── main.cpp                # Entry point
│   ├── core/                   # Core layer — data structures & engines
│   │   ├── canframe.h          #   CAN/CAN FD frame structure
│   │   ├── recorder            #   Message recorder
│   │   ├── player              #   Message player
│   │   ├── cansimulator        #   CAN simulator
│   │   ├── dbcdata.h           #   DBC data structures
│   │   └── dbcmanager          #   DBC file manager
│   ├── models/                 # Data model layer
│   │   ├── cantracemodel       #   Trace table model
│   │   └── canfilterproxymodel #   Filter proxy model (sort + filter)
│   ├── ui/                     # UI layer
│   │   ├── mainwindow          #   Main window (frameless + dock layout)
│   │   ├── spliteditorarea     #   Splittable editor area
│   │   ├── traceview           #   Trace list + FrameInfo + HexDump
│   │   ├── graphicview         #   Signal graphic view
│   │   ├── filterbar           #   Filter bar
│   │   ├── activitybar         #   Activity bar
│   │   ├── bottompanel         #   Bottom panel (terminal/output/problems)
│   │   ├── rightpanel          #   Right properties panel
│   │   └── panels/
│   │       └── sidebarpanels   #   Sidebar panels (project/DBC/config/device)
│   └── utils/
│       └── canutils            #   Formatting & utility functions
├── resources/
│   ├── resources.qrc           # Qt resource collection
│   └── styles/
│       └── default.qss         # Global stylesheet (VS Code style)
└── scripts/
    └── build.py                # Python build script
```

## Installation

### Prerequisites

- [Qt 6.8+](https://www.qt.io/download-open-source) (select MinGW component during installation)
- [CMake 3.21+](https://cmake.org/download/)
- [MinGW 13+](https://www.mingw-w64.org/) or MSVC 2022

### Build Steps

```bash
# 1. Configure
cmake -B build -S . -G "MinGW Makefiles" \
  -DCMAKE_PREFIX_PATH="your_qt_path/6.8.3/mingw_64" \
  -DCMAKE_CXX_COMPILER="your_qt_path/Tools/mingw1310_64/bin/g++.exe" \
  -DCMAKE_C_COMPILER="your_qt_path/Tools/mingw1310_64/bin/gcc.exe"

# 2. Build
cmake --build build

# 3. Deploy (Windows)
windeployqt build/bin/sin.exe

# 4. Run
./build/bin/sin.exe
```

> Alternatively, use the built-in Python build script: `python scripts/build.py all`

## Usage

1. **Launch** — Open sin; the interface is divided into a left sidebar, central editor area, right properties panel, and bottom output panel
2. **Load DBC** — Use `File → Open File` to load a `.dbc` signal definition file
3. **Load Recording** — Open a `.sin` recording file; messages populate the Trace list automatically
4. **Playback** — Click play in the left "Playback Control" collapsible section; adjust speed via the dropdown
5. **Filter** — Enter an expression in the filter bar (e.g., `id == 0x123`), or right-click a column header for per-column filtering
6. **View Signals** — Select a Trace message; the bottom Frame Info panel auto-displays decoded signal values
7. **Graphic Monitoring** — Double-click a signal in the DBC tree to add it to the Graphic view for real-time waveform plotting
8. **Project Management** — Create multiple analysis projects in the left "Project" panel for quick context switching

## Shortcuts

| Action | Method |
|--------|--------|
| Drag window | Hold and drag empty menu bar area |
| Maximize/Restore | Double-click empty menu bar area |
| Split tab | Right-click tab bar → "Split Right" / "Split Down" |
| Sort by column | Click column header |
| Filter by column | Right-click column header → "Filter..." |
| Quick ID filter | Right-click ID column header → select unique ID |
| Clear Trace | Tools menu → "Clear Trace" or type `clear` in terminal |

## Tech Stack

| Component | Version |
|-----------|---------|
| Qt | 6.8.3 (Widgets) |
| C++ | 17 |
| CMake | 3.21+ |
| Compiler | MinGW 13.1.0 / MSVC 2022 |
| Build System | CMake + MinGW Makefiles |
| UI Framework | QMainWindow + QDockWidget + QSplitter |
| Styling | QSS (VS Code-style dark theme) |

## Contributing

1. Fork this repository
2. Create a `Feat_xxx` branch
3. Commit your changes
4. Create a Pull Request

## Author

**Cai Kejie (Jake.cai)**

- GitHub: [https://github.com/JakeCai](https://github.com/JakeCai)
- Project: [https://github.com/JakeCai/sin](https://github.com/JakeCai/sin)
- Email: 929168503@qq.com

## Business Cooperation

For commercial licensing, custom development, technical support, or business partnerships, please contact:

- **Email**: 929168503@qq.com
- **WeChat**: 13368295840
- **GitHub Issues**: [https://github.com/JakeCai/sin/issues](https://github.com/JakeCai/sin/issues)

## License

This project is open-sourced under the [MIT License](LICENSE). For commercial use, please contact the author for licensing.
