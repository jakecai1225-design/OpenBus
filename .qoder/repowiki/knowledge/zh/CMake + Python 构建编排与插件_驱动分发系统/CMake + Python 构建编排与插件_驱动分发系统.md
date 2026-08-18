---
kind: build_system
name: CMake + Python 构建编排与插件/驱动分发系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - drivers/CMakeLists.txt
    - scripts/build.py
    - scripts/driver_tool.py
    - scripts/plugin_tool.py
---

## 1. 构建系统与工具链

- **顶层 CMake 工程**：`CMakeLists.txt`（`cmake_minimum_required(VERSION 3.21)`，`project(openbus VERSION 0.1.0)`），统一声明 Qt6、C++17、AUTOMOC/AUTOUIC/AUTORCC，并通过 `add_subdirectory(src)`、`add_subdirectory(drivers)` 组织子工程。
- **编译器与生成器**：通过 `scripts/build.py` 自动定位 MinGW g++/gcc、CMake、Ninja（或回退到 MinGW Makefiles）；默认路径由环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 覆盖。生成器优先 Ninja，否则使用 `MinGW Makefiles` 并显式传入 `CMAKE_MAKE_PROGRAM`。
- **Qt 依赖**：`find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg Network)`，并通过 `qt_standard_project_setup()` 启用 Qt 标准项目设置。
- **输出目录**：所有可执行文件输出到 `${CMAKE_BINARY_DIR}/bin`（即 `build/bin`）。
- **链接器约束**：注释明确禁止使用 ccache（与 MinGW g++ 13 PCH 不兼容会静默崩溃）和 LLD（Windows 上导致文件锁问题），强制使用默认 `ld.bfd`。

## 2. 目标分层与静态库拆分

`src/CMakeLists.txt` 将源码按层拆分为三个 CMake 目标：
- `openbus_core`（STATIC）：核心逻辑（设备抽象、录制回放、DBC/ARXML、文件格式 I/O、过滤引擎、插件宿主等），对外暴露 PUBLIC include 路径以便 UI 层复用头。
- `openbus_ui`（STATIC）：全部 Qt UI 组件，仅 PRIVATE 依赖 qcustomplot、Qt6::Svg、Qt6::Network；改 UI 代码不会触发 core 重编译。
- `openbus`（可执行）：入口 `main.cpp` + `resources.qrc`，链接 `openbus_ui`；在 Windows 下以 GUI 程序方式运行（`WIN32_EXECUTABLE TRUE`）。

每个目标均配置了 `target_precompile_headers` 的预编译头列表，core 层使用精简 Qt 头（无 Widget），UI 层使用完整 Widget 头集合，以缩短编译时间。

## 3. 第三方依赖管理

`third_party/Dependencies.cmake` 集中声明外部库：
- 头文件型：spdlog（INTERFACE IMPORTED）、nlohmann/json（单头文件）、pugixml（条件链接）。`src/CMakeLists.txt` 通过 `if(EXISTS ...)` 探测后追加 include 路径。
- 源码编译型：qcustomplot（作为 STATIC 库加入，并链接 Qt6::Widgets、Qt6::PrintSupport）。
- 子工程型：vector_blf（若存在则 `add_subdirectory`），并定义 `HAS_VECTOR_BLF` 宏供业务开关。
- 系统库：zlib（MinGW 自带 `libz.a`，BLF 解析需要）。

## 4. 驱动插件构建与分发（.odp）

- `drivers/CMakeLists.txt` 为每个厂商（zlg、peak、kvaser）创建独立子目录，约定每个驱动输出 `driver_<id>.dll`，并附带 `driver.json` 清单。
- 构建产物输出到 `build/bin/drivers/<id>/`，与 `.odp` 包内结构一致；ABI 契约要求与主程序同 Qt 版本 + 同编译器（MinGW 13.1 x64, C++17）。
- `scripts/driver_tool.py` 提供 pack/install/uninstall/validate 四个命令，打包为 ZIP（`.odp`），自动生成 `CHECKSUMS.sha256`，安装时两次校验（解压后 + 落盘后），支持原子 move（临时目录前缀 `.odp_` 避免被扫描识别）。
- 主程序构建时通过 `POST_BUILD` 命令将 `driver/` 下的 ZLG SDK DLL（如 `zlgcan.dll`）复制到 exe 同级目录，确保运行时能找到 USB 驱动。

## 5. Python 插件构建与分发（.opk）

- `plugins/<name>/` 下每个插件包含 `plugin.json`（name/version/main/icon.svg）和 Python 入口 `main.py`。
- `scripts/plugin_tool.py` 提供 pack/install/uninstall/validate，打包为 ZIP（`.opk`），安装时同样采用临时目录 + 原子 move，排除 `__pycache__`、`.pyc`、`.pyo`。
- 插件通过宿主进程 sin API（`sdk/sin/`）与 C++ 引擎通信，由 `PluginManager` 动态加载。

## 6. 构建脚本工作流

`scripts/build.py` 提供统一 CLI：
- `configure`：检测环境 → 选择 Ninja/MinGW Makefiles → 调用 CMake 配置（`-B build -S .`），支持 `--build-type Debug|Release|RelWithDebInfo|MinSizeRel`。
- `build`：增量编译，首次自动 configure；编译前自动 `taskkill /F /IM openbus.exe` 释放文件锁。
- `run/debug/clean/rebuild/deploy/all/status/open`：一键部署 Qt 依赖（windeployqt + 手动补 `Qt6PrintSupport.dll`）、GDB 调试、打开资源管理器。
- 并行度通过 `-j` 传递到 CMake 底层构建系统。

## 7. 安装规则

- `install(TARGETS openbus RUNTIME DESTINATION bin)`：顶层与 `src/CMakeLists.txt` 均声明 install 规则。
- 驱动 DLL 通过 `install(DIRECTORY "${CMAKE_SOURCE_DIR}/driver/" DESTINATION bin FILES_MATCHING PATTERN "*.dll")` 一并安装。

## 8. 约定与约束

- **构建类型**：仅允许 Debug / Release / RelWithDebInfo / MinSizeRel。
- **Qt 路径**：默认 `C:/Qt/6.8.3/mingw_64`，可通过 `--qt-dir` 或 `SIN_QT_DIR` 覆盖。
- **MinGW 路径**：默认 `C:/Qt/Tools/mingw1310_64`，需保证 `PATH` 中 `mingw_bin` 排在前面，否则 `cc1plus.exe` 找不到 `libstdc++-6.dll` 等 DLL。
- **Ninja 优先**：若 `tools/ninja/ninja.exe` 存在则自动使用，否则回退到 MinGW Makefiles。
- **PCH**：core 与 ui 分别维护独立的预编译头列表，禁止混用。
- **驱动 ABI**：必须与主程序同 Qt 版本 + 同编译器，否则 QPluginLoader 无法加载。
- **插件清单**：驱动必须含 `driver.json`（id/version/devices），插件必须含 `plugin.json`（name/version/main），且 name/id 需匹配正则 `^[a-z][a-z0-9-]*$`。
- **安全校验**：驱动包安装前后均校验 `CHECKSUMS.sha256`，防止传输损坏。
- **资源拷贝**：ZLG SDK DLL 在 POST_BUILD 阶段复制到输出目录，确保与 exe 同目录。