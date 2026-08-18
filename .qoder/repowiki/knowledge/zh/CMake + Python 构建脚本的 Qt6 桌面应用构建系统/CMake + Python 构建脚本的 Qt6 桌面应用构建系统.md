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

项目采用 **CMake 3.21+** 作为核心构建系统，配合自研的 **Python 构建脚本 `scripts/build.py`** 封装配置、编译、运行、调试、部署等开发工作流。Qt6（Widgets/PrintSupport/Svg）通过 `find_package(Qt6 REQUIRED ...)` 查找并使用 `qt_standard_project_setup()` 启用标准项目设置。

- C++ 标准固定为 **C++17**（`CMAKE_CXX_STANDARD 17`，`REQUIRED ON`，关闭扩展）。
- 自动处理 Qt 元对象：`CMAKE_AUTOMOC/AUTOUIC/AUTORCC` 全部开启。
- 生成器优先使用本地内置的 **Ninja**（`tools/ninja/ninja.exe`），回退到 **MinGW Makefiles**；Windows MinGW 下强制使用默认 `ld.bfd` 链接器以避免文件锁问题。
- 明确不使用 `ccache`（与 MinGW g++ 13 的 PCH 不兼容会静默崩溃）和 LLD 链接器（Windows 上文件锁问题）。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程定义（版本 0.1.0）、Qt6 查找、第三方依赖包含、子目录添加、安装规则 |
| `src/CMakeLists.txt` | 源文件组织、目标定义（静态库 + 可执行）、PCH、驱动 DLL 拷贝、安装规则 |
| `scripts/build.py` | 统一入口：configure / build / run / debug / clean / rebuild / deploy / all / status / open |
| `third_party/Dependencies.cmake` | 第三方库聚合（spdlog 接口库、qcustomplot 静态库、vector_blf 子目录） |
| `resources/resources.qrc` | Qt 资源文件（图标、样式表） |
| `driver/` | ZLG CAN 硬件驱动 DLL 集合，编译后复制到输出目录 |
| `build/` | 独立 out-of-source 构建目录 |

## 3. 架构与约定

### 3.1 分层静态库拆分
`src/CMakeLists.txt` 将源码按层拆分为三个变量并分别构建：
- `openbus_core`（STATIC）— core/models/utils 层，仅依赖 Qt Core 相关头，不包含 Widget，用于加速重编译。
- `openbus_ui`（STATIC）— ui 层，依赖 `openbus_core` 和 Qt Widgets/Svg/qcustomplot。
- `openbus`（可执行）— 仅链接 `openbus_ui`，入口 `src/main.cpp`。

这种拆分使 UI 代码改动不会触发 core 层重编译，提升增量编译速度。

### 3.2 预编译头（PCH）
两个静态库分别声明精简的 `target_precompile_headers`：core 层只包含非 Widget 的 Qt 头（QObject/QString/QVariant/QList/QHash/QVector...），UI 层包含完整 Widget 头列表（QWidget/QMainWindow/QVBoxLayout...）。这显著缩短大型 Qt 项目的编译时间。

### 3.3 第三方依赖管理
所有第三方库集中在 `third_party/`：
- 单头文件库（spdlog、nlohmann_json、pugixml、concurrentqueue）以 INTERFACE IMPORTED 或 include 路径暴露。
- 源码库（qcustomplot）直接 `add_library(STATIC ...)` 编译进工程。
- vector_blf 通过 `add_subdirectory` 集成其自身 CMakeLists。
- 通过 `if(EXISTS ...)` 条件判断是否启用可选依赖（如 pugixml、vector_blf）。

### 3.4 驱动与运行时依赖
- Windows 下通过 `POST_BUILD` 命令将整个 `driver/` 目录复制到 `bin/` 输出目录，确保 `zlgcan.dll` 等驱动 DLL 与 exe 同目录。
- 提供 `install(DIRECTORY ... FILES_MATCHING PATTERN "*.dll")` 规则打包驱动。
- 部署阶段调用 `windeployqt` 复制 Qt 运行时，并手动补充 qcustomplot 静态库传递依赖的 `Qt6PrintSupport.dll`。

### 3.5 构建流程封装
`scripts/build.py` 提供统一 CLI：
- 自动检测 Ninja → 使用 `-G Ninja`，否则回退 `MinGW Makefiles`。
- 通过环境变量 `SIN_QT_DIR` / `SIN_MINGW_DIR` / `SIN_CMAKE_DIR` 覆盖默认路径（默认指向 `C:/Qt/6.8.3/mingw_64` 等）。
- `build` 命令在首次运行时自动执行 configure；编译前自动 `taskkill /F /IM openbus.exe` 释放文件锁。
- `deploy` 命令调用 `windeployqt` 并补全缺失的 `Qt6PrintSupport.dll`。
- `debug` 命令通过 GDB 启动可执行文件。

## 4. 约定与约束

- **out-of-source 构建**：构建产物统一输出到根级 `build/` 目录，源码树保持干净。
- **跨平台限定**：当前构建脚本与 CMake 逻辑针对 **Windows + MinGW + Qt6** 环境设计（`WIN32_EXECUTABLE TRUE`、`windeployqt`、`psapi` 等均为 Windows 专用）。
- **Qt 模块最小化**：仅请求 `Widgets`、`PrintSupport`、`Svg` 三个组件，避免引入不必要的 Qt 依赖。
- **资源内嵌**：图标、样式表通过 `resources.qrc` 编译进二进制，无需外部资源目录。
- **插件系统**：Python 插件位于 `plugins/`，每个插件含 `main.py` + `plugin.json`，由运行时加载（与 C++ 构建解耦）。
- **无 CI/CD**：仓库中未发现 `.github`、`.gitlab` 等 CI 配置文件，构建主要面向本地开发。
- **版本号**：工程版本在顶层 `CMakeLists.txt` 中定义为 `VERSION 0.1.0`，未看到自动化版本注入机制。
