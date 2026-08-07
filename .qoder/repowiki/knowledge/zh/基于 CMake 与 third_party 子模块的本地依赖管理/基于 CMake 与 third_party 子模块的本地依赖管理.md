---
kind: dependency_management
name: 基于 CMake 与 third_party 子模块的本地依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - scripts/build.py
    - driver/zlgcan.h
    - driver/zlgcan.lib
    - resources/resources.qrc
---

## 1. 使用的系统/方法

本项目采用 **CMake + 源码级第三方库（vendored in `third_party/`）** 的方式管理依赖，不使用任何包管理器（如 vcpkg、Conan、NuGet、npm、pip 等），也没有 lockfile。所有第三方头文件/源码直接以 Git 子模块或手动拷贝的形式存放在仓库根目录的 `third_party/` 下，通过 CMake 的 `add_library` / `target_include_directories` / `target_link_libraries` 显式引入。

- 构建工具链：CMake 3.21+、MinGW g++、Ninja（可选，位于 `tools/ninja/`）。
- Qt 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 查找系统安装的 Qt6，路径由 `scripts/build.py` 通过 `-DCMAKE_PREFIX_PATH=<qt_dir>` 注入。
- 运行时部署使用 Qt 自带的 `windeployqt.exe`，由 `scripts/build.py deploy` 调用。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程入口，声明 C++17、Qt6、包含 `third_party/Dependencies.cmake` |
| `third_party/Dependencies.cmake` | 集中声明第三方库目标（spdlog、qcustomplot、vector_blf、nlohmann_json） |
| `src/CMakeLists.txt` | 将第三方库的头文件路径以 `PUBLIC` 暴露给 `sin_core`，并链接 `z`（BLF zlib 解压） |
| `scripts/build.py` | 统一配置/编译/部署脚本，维护 Qt/MingW/CMake/Ninja 路径与环境变量 |
| `driver/` | 厂商硬件驱动 DLL（ZLG CAN 系列等），构建后自动复制到 exe 同级目录 |
| `resources/resources.qrc` | Qt 资源文件，打包图标/QSS 等资源 |

## 3. 架构与约定

### 3.1 第三方库分类

在 `third_party/` 中按用途分目录存放，每个库对应一个 CMake 集成片段：

- **头文件-only 库**：`spdlog`、`nlohmann_json`、`concurrentqueue` —— 仅设置 `INTERFACE_INCLUDE_DIRECTORIES`，不产生二进制产物。
- **静态源码库**：`qcustomplot` —— 通过 `add_library(qcustomplot STATIC ...)` 将其 `.cpp` 编译进项目，并链接到 `Qt6::Widgets`、`Qt6::PrintSupport`。
- **可嵌入子项目**：`vector_blf` —— 若存在其自身的 `CMakeLists.txt`，则通过 `add_subdirectory(...)` 直接纳入构建树。
- **DBCP 解析库**：`dbcppp` / `dbcppp_src` —— 存在于仓库中但当前未被 `Dependencies.cmake` 启用（可能为预留或按需启用）。

### 3.2 依赖传播方式

`src/CMakeLists.txt` 将第三方头文件路径以 `PUBLIC` 形式添加到 `sin_core` 目标上，使得 UI 层无需感知具体第三方库位置即可使用 DBC、JSON、日志等功能。这种“核心库持有依赖”的模式避免了 UI 层直接耦合第三方实现。

### 3.3 平台相关依赖

- Windows 平台通过 `WIN32_EXECUTABLE TRUE` 隐藏控制台窗口。
- BLF 文件解析需要 `zlib`，通过 `target_link_libraries(sin_core PRIVATE z)` 链接 MinGW 自带的 `libz.a`。
- 驱动 DLL（`driver/*.dll`）在 `POST_BUILD` 阶段被复制到输出目录，安装时也一并打包。

### 3.4 构建脚本中的依赖发现

`scripts/build.py` 集中管理外部工具路径（Qt、MinGW、CMake、Ninja），并通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 覆盖默认值。它还会检测 `tools/ninja/ninja.exe` 是否存在来决定使用 Ninja 还是 MinGW Makefiles 生成器。

## 4. 约定与约束

- **无包管理器**：仓库中没有 `package.json`、`go.mod`、`vcpkg.json`、`conanfile.*` 等依赖清单；所有第三方代码必须手动放入 `third_party/<name>/` 并同步更新 CMake 集成。
- **版本锁定靠 Git 子模块/快照**：由于是 vendored 源码，依赖版本由提交到仓库的具体源码快照决定，升级需手动替换 `third_party/` 下的内容并验证构建。
- **Qt 版本固定**：通过 `CMAKE_PREFIX_PATH` 指向特定 Qt 安装路径（默认 `C:/Qt/6.8.3/mingw_64`），避免不同机器上的 Qt 版本差异导致构建不一致。
- **PCH 限制**：明确禁用 ccache（与 MinGW g++ 13 的 PCH 不兼容）和 LLD 链接器（Windows 上文件锁问题），这些限制在顶层 `CMakeLists.txt` 注释和 `scripts/build.py` 中均有说明。
- **驱动 DLL 强绑定**：`driver/` 下的 DLL 必须随 exe 一起分发，且 ZLG SDK 要求 `zlgcan.dll` 与 exe 同目录才能加载 USB 驱动。
- **可选依赖条件编译**：对 `qcustomplot`、`vector_blf` 等库使用 `if(EXISTS ...)` 判断后再集成，保证在未提供该第三方源码时仍可构建最小功能集。

## 5. 总结

该项目采用经典的 **vendored 源码 + CMake 手工集成** 模式，适合桌面端 C++/Qt 项目的跨平台分发需求——把全部依赖（包括头文件、静态库源码、厂商驱动 DLL）都固化在仓库内，使构建结果可复现且不依赖外部包管理器。缺点是升级第三方库需要人工操作，没有自动化版本检查机制。