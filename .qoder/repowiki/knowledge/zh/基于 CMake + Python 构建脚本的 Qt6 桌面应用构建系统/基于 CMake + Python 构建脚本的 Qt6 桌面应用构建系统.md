---
kind: build_system
name: 基于 CMake + Python 构建脚本的 Qt6 桌面应用构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - resources/resources.qrc
---

## 1. 构建系统与工具链

本项目使用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 构建脚本** `scripts/build.py` 封装配置、编译、运行、调试、部署等完整工作流。目标平台为 **Windows**，工具链为 **MinGW (g++/gcc 13)** 与 **Qt6**（Widgets / PrintSupport / Svg）。

- C++ 标准固定为 **C++17**，关闭扩展 (`CMAKE_CXX_EXTENSIONS OFF`)。
- 启用 Qt 自动化：`CMAKE_AUTOMOC`、`CMAKE_AUTOUIC`、`CMAKE_AUTORCC`。
- 链接器强制使用默认 `ld.bfd`（避免 LLD 在 Windows 上的文件锁问题）；明确不使用 ccache（与 MinGW g++ 13 的 PCH 不兼容，会静默崩溃）。
- 输出目录统一为 `${CMAKE_BINARY_DIR}/bin`，可执行文件名为 `sin.exe`。

## 2. 关键文件与职责

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义、Qt6 查找、第三方依赖引入、子目录注册 |
| `src/CMakeLists.txt` | 源文件分组、静态库拆分、PCH、最终可执行目标、驱动 DLL 拷贝 |
| `third_party/Dependencies.cmake` | 第三方头文件/静态库的 CMake 接口声明（spdlog、qcustomplot、vector_blf） |
| `scripts/build.py` | 构建编排脚本：configure / build / run / debug / clean / rebuild / deploy / all / status / open |
| `tools/ninja/ninja.exe` | 可选的并行构建调度器（优先于 MinGW Makefiles） |
| `resources/resources.qrc` | Qt 资源清单（图标、样式表等） |

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将源码按层次拆分为三个目标：
- `sin_core`（STATIC）：core + models + utils，暴露公共头给 UI 层。
- `sin_ui`（STATIC）：ui 层组件，依赖 `sin_core` 和 Qt6::Widgets。
- `sin`（可执行）：仅链接 `sin_ui`，入口为 `src/main.cpp`。

这种拆分使“改 UI 代码不会触发 core 重编译”，提升增量编译速度。

### 3.2 预编译头（PCH）
通过 `target_precompile_headers()` 分别针对 core 和 ui 层维护精简头列表：
- core 层仅包含无 Widget 的轻量 Qt 头（QObject、QString、QVariant 等），加速非 UI 模块编译。
- ui 层包含完整 Widget 头集（QWidget、QMainWindow、QTabWidget 等）。

### 3.3 第三方依赖管理
- 所有第三方源码位于 `third_party/`，通过 `Dependencies.cmake` 以 INTERFACE IMPORTED 或 STATIC LIBRARY 形式暴露。
- spdlog、nlohmann/json、concurrentqueue、pugixml 以头文件形式直接 include。
- qcustomplot 作为静态库单独编译并链接到 `sin_ui`。
- vector_blf 通过 `add_subdirectory` 集成其自身 CMakeLists。

### 3.4 驱动 DLL 自动部署
构建后通过 `POST_BUILD` 命令将整个 `driver/` 目录复制到 `sin.exe` 同级目录，确保 ZLG SDK 的 `zlgcan.dll` 等运行时 DLL 与可执行文件同目录。安装规则也同步复制 `*.dll`。

### 3.5 构建脚本 `scripts/build.py`
提供统一的命令行入口，支持以下子命令：
- `configure`：检测 Ninja / MinGW Makefiles 生成器，设置 `CMAKE_PREFIX_PATH`、编译器路径、构建类型。
- `build`：增量编译，首次自动调用 configure；编译前自动 `taskkill /F sin.exe` 释放文件锁。
- `run`：若可执行不存在则先 build，再启动。
- `debug`：通过 GDB 启动，支持传递参数。
- `deploy`：调用 `windeployqt` 打包 Qt 运行时，并手动补拷 `Qt6PrintSupport.dll`（qcustomplot 静态库的传递依赖）。
- `rebuild` / `clean` / `all` / `status` / `open`。

环境变量覆盖默认工具路径：`SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR`。

## 4. 约定与约束

- **构建类型**：仅支持 `Debug`、`Release`、`RelWithDebInfo`、`MinSizeRel`，默认 Debug。
- **生成器优先级**：优先使用本地 `tools/ninja/ninja.exe`，未找到时回退到 `MinGW Makefiles`。
- **禁止使用的工具**：c++ 注释中明确要求不使用 ccache（与 PCH 不兼容）、不使用 LLD（Windows 文件锁问题）。
- **Qt 版本锁定**：通过 `find_package(Qt6 REQUIRED ...)` 及默认 `SIN_QT_DIR=C:/Qt/6.8.3/mingw_64` 锁定 Qt6.8.3。
- **MinGW 路径要求**：脚本会将 `cmake/bin`、`mingw/bin`、`qt/bin` 前置到 PATH，否则 `cc1plus.exe` 找不到运行时 DLL（libstdc++、libwinpthread 等）导致静默崩溃。
- **资源与样式**：所有图标、样式表通过 `resources.qrc` 嵌入，无需外部资源文件。
- **安装规则**：`install(TARGETS sin RUNTIME DESTINATION bin)` 配合 driver DLL 的目录拷贝，构成最小可分发产物。

## 5. CI / 发布

仓库中未发现 GitHub Actions / GitLab CI / Dockerfile 等持续集成配置，也未见版本发布脚本。版本信息集中在根 `CMakeLists.txt` 的 `project(... VERSION 0.1.0)` 中，由 CMake 管理。