---
kind: dependency_management
name: 第三方依赖管理：CMake + vendored 源码策略
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/download_spdlog.py
    - scripts/download_json.py
    - scripts/download_qcustomplot.py
    - scripts/download_concurrentqueue.py
    - scripts/download_exprtk.py
---

本项目采用 CMake 作为构建系统，通过 `third_party/` 目录以 vendored（内联源码）方式管理所有第三方依赖，不使用包管理器（如 vcpkg、Conan、pkg-config），也没有 lockfile。依赖获取与版本锁定由 Python 脚本和 CMake 配置共同完成。

**依赖来源与下载机制**
- 每个第三方库对应一个 `scripts/download_*.py` 脚本，使用 `urllib.request` 从 GitHub raw 或 Release 页面直接下载源码到 `third_party/<lib>/` 目录。
- 已存在的脚本包括：`download_spdlog.py`（v1.14.1 固定版本 zip）、`download_json.py`（nlohmann/json 单头文件）、`download_qcustomplot.py`、`download_concurrentqueue.py`、`download_exprtk.py`。
- 这些脚本仅负责拉取源码，不处理版本冲突或更新检查；版本号硬编码在脚本 URL 中（如 spdlog 的 `v1.14.1.zip`）。

**CMake 集成方式**
- 根 `CMakeLists.txt` 通过 `include(${CMAKE_SOURCE_DIR}/third_party/Dependencies.cmake)` 统一引入依赖声明。
- `third_party/Dependencies.cmake` 中以 `INTERFACE IMPORTED` 目标或 `add_subdirectory` 形式注册各库：
  - `spdlog`：纯头文件库，仅设置 `INTERFACE_INCLUDE_DIRECTORIES`。
  - `nlohmann/json`：单头文件，无需额外配置。
  - `qcustomplot`：静态库，编译 `qcustomplot.cpp` 并链接 Qt6::Widgets、Qt6::PrintSupport。
  - `vector_blf`：若存在其 `CMakeLists.txt` 则通过 `add_subdirectory` 引入。
  - `dbcppp`：位于 `third_party/dbcppp/`，但当前 `Dependencies.cmake` 中未显式声明（可能通过其他方式引用）。
- 顶层 `CMakeLists.txt` 还通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)` 查找 Qt6 框架。

**依赖组织约定**
- 所有第三方源码集中放在 `third_party/<library_name>/` 子目录，按库名平铺，无嵌套结构。
- 头文件库（spdlog、json、concurrentqueue、exprtk）直接 include 对应路径；需要编译的库（qcustomplot、vector_blf）通过 CMake 目标暴露。
- 项目未使用 Git submodule 管理第三方代码，而是将源码直接提交到仓库（如 `third_party/dbcppp_src/` 与 `third_party/dbcppp/` 并存）。

**构建与部署中的依赖处理**
- `scripts/build.py` 是统一的构建入口，支持 configure/build/run/debug/clean/rebuild/deploy/all/status/open 等命令。
- 部署阶段调用 `windeployqt` 自动收集 Qt 运行时 DLL，同时手动补充 `Qt6PrintSupport.dll`（因 qcustomplot 为静态库，windeployqt 无法检测其传递依赖）。
- 构建工具链（Ninja、ccache、LLD）放置在 `tools/` 目录并通过 PATH 注入，属于可选加速组件而非强制依赖。

**约束与注意事项**
- 依赖版本由下载脚本中的 URL 固定，更新需手动修改脚本并重新运行。
- 没有统一的依赖清单文件或版本锁，跨机器复现依赖需确保 `third_party/` 目录完整。
- Qt 路径通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 或命令行参数覆盖默认值。
- 部分依赖（如 dbcppp）存在多份源码副本（`dbcppp/` 与 `dbcppp_src/`），需确认实际使用的来源。