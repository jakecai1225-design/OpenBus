---
kind: build_system
name: CMake + Qt6 + Python 脚本的混合构建与插件分发系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - drivers/CMakeLists.txt
    - drivers/zlg/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/driver_tool.py
    - scripts/plugin_tool.py
    - resources/resources.qrc
---

## 1. 构建系统与工具链

- **构建系统**：CMake 3.21+，顶层 `CMakeLists.txt` 声明项目 `openbus`（版本 0.1.0），使用 C++17、关闭扩展，启用 `AUTOMOC/AUTOUIC/AUTORCC`。
- **编译器/工具链**：Windows MinGW (g++ 13.1 x64)，通过 `scripts/build.py` 自动发现 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 环境变量或默认路径；生成器优先 Ninja（`tools/ninja/ninja.exe`），回退 MinGW Makefiles。
- **Qt 依赖**：`find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)`，并通过 `qt_standard_project_setup()` 设置标准布局。
- **第三方库**：`third_party/Dependencies.cmake` 集中管理——spdlog、nlohmann/json 为头文件库，qcustomplot 编译为静态库 `qcustomplot`，vector_blf/pugixml/concurrentqueue 以源码形式集成。
- **输出目录**：可执行产物统一输出到 `build/bin/openbus.exe`；驱动插件输出到 `build/bin/drivers/<id>/driver_<id>.dll`。

## 2. 核心构建架构

- **分层静态库**：`src/CMakeLists.txt` 将源码拆分为三个目标：
  - `openbus_core`（core + models + utils）— 数据结构、录制/回放、DBC、过滤引擎、设备管理、文件格式 I/O、插件宿主等核心逻辑，作为静态库供 UI 复用。
  - `openbus_ui`（ui 层）— 所有 Qt 界面组件，链接 `openbus_core` 和 qcustomplot/Qt6::Svg/Qt6::Network。
  - `openbus` 可执行 — 仅包含 `main.cpp` + `resources.qrc`，链接 `openbus_ui`。
- **预编译头（PCH）**：`target_precompile_headers` 在 core 层使用精简 Qt 头列表（无 Widget），UI 层使用完整 Widget 头列表，显著缩短增量编译时间。
- **Post-build 拷贝**：若存在 `driver/` 目录，构建后自动将整个 `driver/` 复制到 exe 同级目录（ZLG SDK 的 `zlgcan.dll` 必须与 exe 同目录才能找到 USB 驱动）。
- **安装规则**：`install(TARGETS openbus RUNTIME DESTINATION bin)`，并安装 `driver/*.dll`。

## 3. 驱动插件构建（原生 .odp）

- 每个厂商驱动（`drivers/zlg`、`drivers/peak`、`drivers/kvaser`）独立 `CMakeLists.txt`，使用 `qt_add_plugin()` 生成 Qt 动态插件 DLL。
- 每个驱动 target 将自身后端源码（如 `candevice_zlg.*`）一并编入，输出到 `build/bin/drivers/<id>/`，与 `.odp` 包内布局一致。
- ABI 契约要求：与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17）。
- 打包流程由 `scripts/driver_tool.py` 完成：`pack` 生成 ZIP（`.odp`），内含 `driver.json` + `driver_<id>.dll` + 可选 `icon.svg` / `assets/` / `vendor/`，自动生成 `CHECKSUMS.sha256`；`install` 解压到 `<drivers_dir>/<id>/` 并校验 checksums；`uninstall` 写 `.uninstall` 标记以便下次启动清理。

## 4. Python 插件构建（G9 .opk）

- Python 插件位于 `plugins/<name>/`，每个插件含 `plugin.json`（name/version/main/icon）和 `main.py`。
- `scripts/plugin_tool.py` 提供 pack/install/uninstall/validate 命令，打包为 ZIP（`.opk`），排除 `__pycache__` 和 `.pyc/.pyo`。
- 安装时原子移动到 `<plugins_dir>/<name>/`，禁止覆盖已存在插件。

## 5. 构建脚本与工作流

`scripts/build.py` 是统一入口，支持子命令：
- `configure`：检测工具链 → 选择 Ninja/MinGW Makefiles → 调用 CMake 配置（`-B build -S .`）。
- `build`：增量编译（首次自动 configure），自动终止正在运行的 `openbus.exe` 避免文件锁。
- `run` / `debug`：运行或 GDB 调试。
- `deploy`：调用 `windeployqt` 部署 Qt 运行时，并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库传递依赖）。
- `all`：完整流程（configure + build + deploy + run）。
- `clean` / `rebuild` / `status` / `open`。

## 6. 约定与约束

- **不使用 ccache**：与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃（注释明确说明）。
- **不使用 LLD 链接器**：在 Windows 上会导致文件锁问题，使用默认 `ld.bfd`。
- **Qt 资源**：通过 `resources/resources.qrc` 嵌入图标、样式（`default.qss`、`theme.qss`）。
- **驱动 DLL 部署**：构建后 `driver/` 目录整体拷贝至 exe 同级目录，保证 ZLG SDK 运行时可加载。
- **插件/驱动包格式**：均为 ZIP；驱动包强制 SHA256 校验（`CHECKSUMS.sha256`），插件包通过 `plugin.json` 清单校验。
- **构建类型**：Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug。
- **并行构建**：通过 `-j<N>` 参数传递给底层构建系统（Ninja 或 make）。