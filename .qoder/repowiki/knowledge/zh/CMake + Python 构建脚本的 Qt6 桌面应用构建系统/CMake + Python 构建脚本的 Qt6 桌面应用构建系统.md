---
kind: build_system
name: CMake + Python 构建脚本的 Qt6 桌面应用构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
    - third_party/Dependencies.cmake
    - resources/resources.qrc
---

## 1. 使用的系统与工具

- **构建系统**: CMake 3.21+，项目根 `CMakeLists.txt` 声明 `cmake_minimum_required(VERSION 3.21)`。
- **编译器/工具链**: Windows 平台使用 MinGW (g++/gcc)，默认路径通过环境变量 `SIN_MINGW_DIR` 覆盖（默认 `C:/Qt/Tools/mingw1310_64`）；CMake 默认 `C:/tools/cmake-3.30.3-windows-x86_64`；Qt6 默认 `C:/Qt/6.8.3/mingw_64`。
- **生成器**: 优先检测本地 `tools/ninja/ninja.exe` 并使用 Ninja 生成器；未找到时回退到 `MinGW Makefiles`。
- **打包/部署**: 通过 `windeployqt` 部署 Qt 运行时依赖；驱动 DLL 通过 `add_custom_command(TARGET ... POST_BUILD)` 拷贝至输出目录。
- **版本管理**: 顶层 `project(sin VERSION 0.1.0 ...)` 定义版本号，无独立版本文件。

## 2. 关键文件与位置

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程配置：C++17、Qt 自动处理 (AUTOMOC/AUTOUIC/AUTORCC)、find Qt6 Widgets/PrintSupport/Svg、包含第三方依赖、添加 src 子目录、安装规则 |
| `src/CMakeLists.txt` | 核心构建逻辑：源文件分组为 `SRC_CORE` / `SRC_MODELS` / `SRC_UI` / `SRC_UTILS`；构建三个目标 `sin_core` (静态库)、`sin_ui` (静态库)、`sin` (可执行)；PCH 预编译头列表；Windows GUI 属性；驱动 DLL 拷贝与安装 |
| `scripts/build.py` | Python 构建入口：封装 configure/build/run/debug/clean/rebuild/deploy/all/status/open 子命令；自动探测并设置 PATH；调用 windeployqt；自动终止正在运行的 sin.exe 避免文件锁 |
| `third_party/Dependencies.cmake` | 第三方依赖聚合：spdlog (INTERFACE IMPORTED)、nlohmann/json (单头)、qcustomplot (静态库)、vector_blf (可选 add_subdirectory) |
| `resources/resources.qrc` | Qt 资源文件，由 AUTORCC 处理 |
| `driver/` | ZLG CAN 设备驱动 DLL 及配置文件，编译后整体拷贝到 exe 同级目录 |

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将源码按职责拆分为四个变量 (`SRC_CORE`、`SRC_MODELS`、`SRC_UI`、`SRC_UTILS`)，并构建两个中间静态库：
- `sin_core`：数据结构、录制/回放、DBC、过滤引擎、设备抽象等核心逻辑，对外暴露 PUBLIC include 路径以便 UI 层引用公共头。
- `sin_ui`：所有界面组件，仅 PRIVATE 链接 qcustomplot 和 Qt6::Svg（因为 graphicview.cpp 才用 qcustomplot，且其 .h 使用前向声明）。
- `sin` 可执行目标仅链接 `sin_ui`，形成 `sin → sin_ui → sin_core` 的单向依赖链，使修改 UI 代码时只重编译 UI 库与最终链接，不触发 core 重编译。

### 3.2 PCH (预编译头)
- `sin_core` 使用精简 Qt 头集合（不含 QWidget），加速核心层编译。
- `sin_ui` 使用完整 Qt Widget 头集合。
- 顶层注释明确说明“不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）”、“不使用 LLD 链接器（在 Windows 上会导致文件锁问题）”，因此构建脚本也未启用这些加速手段。

### 3.3 第三方依赖策略
- 全部以源码形式内嵌在 `third_party/`：spdlog、nlohmann_json、pugixml、concurrentqueue、dbcppp、qcustomplot、vector_blf。
- `Dependencies.cmake` 通过 `INTERFACE IMPORTED` 或 `add_library(... STATIC)` 暴露给上层，无需外部包管理器。
- 条件式存在性检查 (`if(EXISTS ...)`) 保证部分依赖缺失时仍可配置。

### 3.4 构建流程与产物
- 构建输出统一位于 `build/bin/sin.exe`（由 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 控制）。
- 若检测到 `driver/` 目录，会在 `POST_BUILD` 阶段将整个目录拷贝到 exe 输出目录，确保 `zlgcan.dll` 等驱动 DLL 与可执行文件同目录。
- `install()` 规则将 `sin` 安装到 `bin`，并将 `driver/*.dll` 一并安装。

### 3.5 开发工作流
`scripts/build.py` 提供统一入口：
- `configure`：校验环境 (cmake/g++/gcc/windeployqt/gdb)，选择 Ninja 或 MinGW Makefiles 生成器，设置 `CMAKE_PREFIX_PATH`、编译器、构建类型。
- `build`：增量编译，首次运行自动触发 configure；支持 `-j` 并行；编译前自动 `taskkill /F /IM sin.exe` 释放文件锁。
- `run` / `debug`：启动程序或 GDB。
- `deploy`：调用 `windeployqt` 并手动补充 `Qt6PrintSupport.dll`（qcustomplot 静态库对 Qt6PrintSupport 的传递依赖未被 windeployqt 识别）。
- `all`：一键完成 configure → build → deploy → run。
- 路径通过环境变量 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 覆盖。

## 4. 约定与约束

- **C++ 标准**：强制 C++17 (`CMAKE_CXX_STANDARD 17`，`REQUIRED ON`，关闭扩展)。
- **Qt 自动化**：开启 AUTOMOC/AUTOUIC/AUTORCC，`.ui`、`.moc`、`.qrc` 由 CMake 自动生成。
- **生成器选择**：优先 Ninja（`tools/ninja/ninja.exe`），不存在则回退 MinGW Makefiles；Ninja 被注释为“编译调度快 2-3x”。
- **禁止项**：明确禁用 ccache 与 LLD 链接器（与 MinGW g++ 13 PCH 不兼容 / Windows 文件锁问题），该约束同时体现在顶层 CMakeLists.txt 注释与 build.py 注释中。
- **Windows GUI 模式**：`WIN32_EXECUTABLE TRUE`，不弹出控制台窗口。
- **驱动依赖**：ZLG SDK 的 `zlgcan.dll` 必须与 exe 在同一目录才能找到 USB 驱动，因此构建脚本强制在 POST_BUILD 阶段拷贝整个 `driver/` 目录。
- **构建目录隔离**：所有构建产物位于 `build/` 目录，源码树干净。
- **Qt 版本锁定**：通过固定默认路径 `C:/Qt/6.8.3/mingw_64` 锁定 Qt 版本，需通过环境变量切换。
- **并行构建**：默认使用 `os.cpu_count()` 作为 `-j` 参数，Ninja 与 MinGW Makefiles 均支持。
- **资源文件**：UI 图标、样式表通过 `resources.qrc` 嵌入，无需外部资源目录。