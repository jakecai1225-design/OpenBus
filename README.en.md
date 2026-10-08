# OpenBus

<p align="center">
  <strong>A professional, free, and open-source desktop suite for bus, protocol, and network analysis</strong><br/>
  Open architecture · Extensible drivers & plugins · Built for everyday engineering work
</p>

<p align="center">
  <a href="README.md">中文</a>
  &nbsp;·&nbsp;
  <a href="http://sin.org.cn/">Website / App Store</a>
  &nbsp;·&nbsp;
  <a href="http://sin.org.cn/docs">Docs</a>
  &nbsp;·&nbsp;
  <a href="http://sin.org.cn/download">Download</a>
  &nbsp;·&nbsp;
  <a href="http://sin.org.cn/market">Marketplace</a>
  &nbsp;·&nbsp;
  <a href="https://gitee.com/jake_cai/sin">Gitee</a>
  &nbsp;·&nbsp;
  <a href="https://github.com/jakecai1225-design/OpenBus">GitHub</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-1.10.4-blue" alt="Version">
  <img src="https://img.shields.io/badge/Qt-6-green" alt="Qt 6">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue" alt="C++17">
  <img src="https://img.shields.io/badge/license-MIT-yellow" alt="License">
  <img src="https://img.shields.io/badge/platform-Windows%20x64-lightgrey" alt="Windows">
</p>

<p align="center">
  <img src="http://sin.org.cn/screenshots/07-trace.png" alt="OpenBus Trace" width="880">
</p>

---

## Overview

**OpenBus** is a desktop workbench for bus, protocol, and field-network analysis. It covers live capture and log playback, Trace inspection, signal waveforms, DBC decoding, multi-vendor device access, and higher-level workflows such as diagnostics, calibration, and log conversion—extended on demand through an open driver and plugin model.

The project is released under the **MIT License** for individuals, teams, and commercial integration. We aim for something solid enough for day-to-day bring-up and log analysis, and open enough for hardware vendors, tool authors, and enthusiasts to grow the ecosystem together.

Typical uses include automotive and EV electronics, industrial control and robotics, teaching labs, and protocol research—anywhere **CAN / CAN FD** and related upper-layer analysis matter. The shipping release targets **Windows 10/11 x64**. The source tree also includes backends such as SocketCAN for Linux-oriented builds.

### What we optimize for

| | |
|--|--|
| **Professional** | Trace, Graphic, filters, decode, and log I/O follow familiar analysis habits and real engineering workloads |
| **Reliable** | The host is built with Qt / C++; capture, playback, and view refresh are designed for long sessions and large logs |
| **Open & free** | Source, documentation, and the official extension catalog are publicly available; MIT permits study, modification, and commercial use |
| **Extensible** | Drivers (`.odp`) and tool plugins (`.opk`) install hot; protocol and domain features evolve as suites |
| **Multi-vendor** | Several common backends ship built-in, with room to add more adapters through the marketplace |

A typical session: connect hardware or open a log → filter and locate frames in Trace → decode with DBC → compare waveforms in Graphic → install diagnostics or calibration extensions when needed.

---

## Who it is for

- **Automotive / EV / Tier suppliers** — CAN / CAN FD bring-up, bench and road-test logs, DBC signal checks  
- **Industrial & robotics** — field capture, period and jitter analysis, device wiring checks  
- **Teaching & research** — full demos with the built-in simulator; source suitable for courses and derivatives  
- **Protocol analysis** — filters, bookmarks, hex dump, multi-log comparison  
- **Ecosystem partners** — teams that want to publish drivers or protocol tools into a shared marketplace  

---

## Features

### Recording, playback & logs

- **Live capture** from CAN interfaces — CAN 2.0A/B and CAN FD  
- **Timestamp-accurate playback** with variable speed (~0.1×–10×) and seek  
- **Import**: BLF / ASC / CSV / PCAP / TRC and other common formats  
- **Export**: ASC / CSV / BLF for reports and downstream tools  
- **Built-in simulator** to exercise filters, decode, and views without hardware  

### Trace view

- Wireshark-style columns: No., time, delta, channel, direction, ID, DLC, data, flags, count, …  
- Column sort, header funnel filters, quick filters (Rx-only / Tx-only / unique IDs)  
- Expression filters (e.g. `id == 0x123 && dlc > 4`)  
- Overlay mode, automatic / manual row coloring, bookmarks  
- Frame detail panel and hex dump (offset / hex / ASCII)  
- Search, navigation, and statistics (counts, rates, error frames, bus load, …)  

### DBC & signal analysis

- Load standard `.dbc` files; browse Message → Signal in the sidebar  
- Decode the selected Trace frame (signed / unsigned, endianness)  
- Double-click a signal to plot it in Graphic during live or playback sessions  

### Graphic

- Multi-signal time-domain overlays with per-channel display options  
- Wheel zoom, pan, cursor / caliper measurement  
- Linked with Trace for list-and-waveform debugging  

### Projects & UI

- VS Code–style layout: activity bar, collapsible sidebar, splittable editors, dockable panels  
- `.openbusproj` persistence for channels, DBC, and layout; quick multi-project switching  
- Built-in Data Window, I/O Graph, and coloring-rule tools  
- Command terminal (`help` / `sim` / `record` / `play` / `filter`, …)  
- Light / dark themes; frameless window still supports Windows snap and resize  

---

## Hardware & drivers

Analysis stays in the host; device differences are handled by the driver layer so many interface cards and open adapters can coexist.

### Built-in backends

Registered at startup (enumeration depends on vendor runtimes installed on the machine):

| Driver ID | Description |
|-----------|-------------|
| **simulator** | OpenBus simulator (always available) |
| **zlg** | ZLG USBCAN / USBCANFD and related adapters |
| **peak** | PEAK System PCAN |
| **kvaser** | Kvaser CANLIB |
| **vector** | Vector XL |
| **tongxing** | TongXing / TOSUN |
| **ixxat** | IXXAT VCI |
| **intrepid** | Intrepid |
| **candle** | Candle / GS_USB (CANable, candleLight, …) |
| **busmust** | BUSMUST USB-CAN(FD) |
| **slcan** | SLCAN serial protocol (adapters / CANable / USBtin / ESP32, …) |
| **socketcan** | Linux SocketCAN (source builds) |

### Marketplace driver packages (`.odp`)

- Distributed as `.odp` (ZIP): `driver.json` + native library + `CHECKSUMS.sha256`  
- Download, verify, install, and hot-load from the in-app **Marketplace** (usually without restart)  
- Disable, uninstall, or install offline from a local file  
- Official index: [`http://sin.org.cn/market/market.json`](http://sin.org.cn/market/market.json)  

To add a driver, implement a backend under `drivers/<id>/`, wire CMake, and pack with `scripts/driver_tool.py`. See the driver notes under `doc/`.

---

## Plugins & protocol extensions

The host provides shared analysis and device management. Diagnostics, calibration, log conversion, assisted analysis, and similar capabilities ship as plugins.

| Topic | Detail |
|-------|--------|
| Runtime | Tool plugins run out-of-process (Python + PyQt6) and talk to the host over a bridge |
| Package | `.opk` (`plugin.json` + scripts and assets) |
| Install UX | Same Marketplace as drivers: search, detail, enable / disable, uninstall |
| Catalog | Domain suites are allowlisted to keep the default product surface clear |

Suites currently available include UDS, OBD, J1939, CANopen, DBC Studio, EDS Studio, A2L Studio, XCP Studio, AUTOSAR Suite, EtherCAT Suite, Log Converter, AI Agent, and more.

Local development helpers:

```bash
python scripts/plugin_tool.py pack-suites
python scripts/make_market.py
```

Design notes: `doc/` (plugin architecture and domain-suite docs).

---

## Website & App Store

The product site and extension catalog live at **[sin.org.cn](http://sin.org.cn/)** (OpenBus App Store). The desktop **Marketplace** and the web market share the same catalog data.

| Page | URL | Purpose |
|------|-----|---------|
| Home | [http://sin.org.cn/](http://sin.org.cn/) | Product overview and navigation |
| Download | [http://sin.org.cn/download](http://sin.org.cn/download) | Windows installer |
| Docs | [http://sin.org.cn/docs](http://sin.org.cn/docs) | Online user manual |
| Manual PDFs | [ZH](http://sin.org.cn/docs/downloads/openbus-user-manual.pdf) · [EN](http://sin.org.cn/docs/downloads/openbus-user-manual.en.pdf) | Offline reading |
| Updates | [http://sin.org.cn/updates](http://sin.org.cn/updates) | Release notes |
| Marketplace | [http://sin.org.cn/market](http://sin.org.cn/market) | Browse drivers and plugins |
| Hardware | [http://sin.org.cn/hardwares](http://sin.org.cn/hardwares) | Device / compatibility overview |
| Submit an extension | [http://sin.org.cn/market/submit](http://sin.org.cn/market/submit) | Guidance for publishing drivers / plugins |
| Community | [http://sin.org.cn/community](http://sin.org.cn/community) | Community and contribution entry points |
| Feedback | [http://sin.org.cn/feedback](http://sin.org.cn/feedback) | Suggestions and problem reports |
| Blog | [http://sin.org.cn/blog](http://sin.org.cn/blog) | Usage and development articles |
| Market index (JSON) | [http://sin.org.cn/market/market.json](http://sin.org.cn/market/market.json) | Catalog source for the desktop client |

After installing the desktop app, open **Marketplace** in the sidebar for the same catalog experience.

---

## Get started

1. Download the installer from the [download page](http://sin.org.cn/download), or build from source  
2. Run `openbus.exe`, start from Welcome, or use **File → Open** for DBC / logs  
3. Configure a channel, connect hardware, or enable the simulator to learn the UI  
4. Analyze in Trace / Graphic; install drivers or domain suites from the Marketplace when needed  

Full walkthrough: [online docs](http://sin.org.cn/docs).

---

## Open source, feedback & contributing

We welcome use, feedback, and contributions.

### Report issues and ideas

| Channel | URL |
|---------|-----|
| Gitee Issues (preferred; matches the main repo) | [gitee.com/jake_cai/sin/issues](https://gitee.com/jake_cai/sin/issues) |
| GitHub Issues | [github.com/jakecai1225-design/OpenBus/issues](https://github.com/jakecai1225-design/OpenBus/issues) |
| Website feedback form | [sin.org.cn/feedback](http://sin.org.cn/feedback) |
| Community page | [sin.org.cn/community](http://sin.org.cn/community) |
| Email | 929168503@qq.com |

For bugs, please include OpenBus version, OS, reproduction steps, and relevant log formats or screenshots when possible. Feature requests can describe the scenario and expected behavior in an Issue.

### Source code and pull requests

| Host | Repository |
|------|------------|
| Gitee (day-to-day collaboration) | [gitee.com/jake_cai/sin](https://gitee.com/jake_cai/sin) |
| GitHub (mirror) | [github.com/jakecai1225-design/OpenBus](https://github.com/jakecai1225-design/OpenBus) |

Suggested flow:

1. Fork and create a branch  
2. Build and verify under MSYS2  
3. Open a pull request with motivation and test notes  
4. For larger behavioral or architectural changes, start with an Issue  

User-manual sources live in `doc/user_manual/`; contributor design notes in `doc/`.

### Develop and publish drivers / plugins

1. Read the driver and plugin docs under `doc/`, and study `drivers/*` / `plugins/*`  
2. Pack with `scripts/driver_tool.py` / `scripts/plugin_tool.py` into `.odp` / `.opk`  
3. Generate a local index with `scripts/make_market.py` and verify install in the client  
4. Follow [Submit an extension](http://sin.org.cn/market/submit), or contact maintainers via Issue / email  
5. Once published, users can find the package on the [Marketplace](http://sin.org.cn/market) and in the desktop Marketplace  

---

## Build from source

### Requirements

Use the **MSYS2 UCRT64** toolchain. Mixing the Qt online installer or other MinGW distributions is not recommended.

| Component | Requirement |
|-----------|-------------|
| OS | Windows 10/11 x64 (shipping and primary validation target) |
| Shell | [MSYS2](https://www.msys2.org/) UCRT64 |
| Build | CMake ≥ 3.21, Ninja, C++17 |
| Qt | Qt 6 (`mingw-w64-ucrt-x86_64-qt6-*`) |
| Also | zlib, ZeroMQ / cppzmq, libusb; Python, PyQt6, PyZMQ for plugin work |

### Install dependencies

In a UCRT64 shell, from the repository root:

```bash
bash scripts/setup_msys2.sh
python scripts/build.py status
```

Optional env vars (bash paths):

```bash
export SIN_MSYS2_ROOT=/c/msys64
export SIN_MSYS2_ENV=ucrt64
export SIN_QT_DIR=/ucrt64
export SIN_MINGW_DIR=/ucrt64
```

### Configure, build, run

```bash
python scripts/build.py configure --clean --build-type Release
python scripts/build.py build -j8
python scripts/build.py deploy
python scripts/build.py run
```

Full pipeline: `python scripts/build.py all`

Dev profile in a separate directory:

```bash
python scripts/build.py configure --build-type Dev --build-dir build-dev
python scripts/build.py build --build-dir build-dev -j8
python scripts/build.py run --build-dir build-dev
```

Treat `scripts/build.py` as the source of truth. Avoid ccache and casual LLD switches for this project.

### Layout (brief)

```
sin/
├── src/           # host (core / ui / models …)
├── drivers/       # vendor and protocol drivers
├── plugins/       # Python domain suites
├── market/        # local market index and packages
├── resources/     # assets and styles
├── scripts/       # build, pack, and market tools
├── doc/           # contributor docs and user-manual sources
└── dist/          # package outputs
```

---

## Packaging & deployment

### Portable ZIP

```bash
python scripts/build.py package --version 1.10.4
```

Artifacts are under `dist/`. Keep the folder layout intact and run `openbus.exe`.

### Windows installer (NSIS)

Install [NSIS 3](https://nsis.sourceforge.io/), or point `NSIS` / `MAKENSIS` at `makensis.exe`:

```bash
python scripts/build.py package --version 1.10.4 --installer
```

The setup wizard supports multiple UI languages. See `python scripts/build.py package -h` for more options.

### Marketplace & release

```bash
python scripts/make_market.py
python scripts/driver_tool.py pack --help
python scripts/plugin_tool.py pack-suites
```

The client can use the official [`market.json`](http://sin.org.cn/market/market.json) or a local `market/` tree during development.

Before a release, confirm a clean `status`, smoke-test the Release build, keep the version aligned with About (**1.10.4**), and verify marketplace checksums.

---

## Author

**Jake Cai (蔡可杰)** · 929168503@qq.com  

See also the [community page](http://sin.org.cn/community) and the source links above.

---

## License

Released under the [MIT License](LICENSE). You may use, modify, and distribute the software, including for commercial purposes, provided the copyright and permission notice are retained.
