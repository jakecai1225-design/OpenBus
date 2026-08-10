---
kind: dependency_management
name: 基于 CMake 的第三方源码级依赖管理（third_party 子模块 + INTERFACE IMPORTED）
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - driver/zlgcan.h
    - driver/dll_cfg.ini
---

## 1. 使用的系统/方法

本项目采用 **CMake + Qt6** 构建，所有第三方依赖以 **源码形式直接放入 `third_party/` 目录**（即 vendoring），通过 CMake 目标暴露给主工程。没有使用任何包管理器（如 vcpkg、Conan、NuGet、npm 等），也没有 lockfile 或私有仓库配置。

- 头文件-only 库：spdlog、nlohmann/json、concurrentqueue、pugixml —— 仅通过 `target_include_directories` 暴露 include 路径。
- 源码静态库：qcustomplot 由 `third_party/qcustomplot/qcustomplot.cpp` 编译为 `STATIC` 目标 `qcustomplot`，并链接 Qt6::Widgets / Qt6::PrintSupport。
- 可嵌入子项目：vector_blf 通过 `add_subdirectory(...)` 引入其自身的 CMakeLists.txt（条件判断是否存在）。
- 外部二进制驱动：`driver/` 下的 ZLG SDK DLL（zlgcan.dll、CANFDCOM.dll 等）在 POST_BUILD 阶段被复制到输出目录，与 sin.exe 同级。

## 2. 关键文件

- `CMakeLists.txt`（根）：声明 Qt6 依赖、`include(third_party/Dependencies.cmake)`、添加 `src/` 子目录。
- `third_party/Dependencies.cmake`：集中定义 spdlog（INTERFACE IMPORTED）、qcustomplot（STATIC）、vector_blf（add_subdirectory）三个第三方目标。
- `src/CMakeLists.txt`：将第三方 include 路径以 PUBLIC 方式注入 `sin_core` 目标；按条件链接 pugixml、zlib；定义 PCH 加速编译；将 driver 目录拷贝到输出目录。
- `driver/`：ZLG CAN/CANFD 硬件驱动的 DLL、头文件、设备 XML 配置文件集合。
- `third_party/dbcppp.zip`：dbcppp 源码压缩包（尚未解压到 `third_party/dbcppp_src/` 中，当前未启用）。

## 3. 架构与约定

- **源码级 vendoring**：每个第三方库独立一个子目录（spdlog、nlohmann_json、qcustomplot、vector_blf、concurrentqueue、pugixml），版本锁定在提交时的源码快照。
- **条件式发现**：所有第三方路径均用 `if(EXISTS ...)` 包裹，避免缺少某个库时构建失败（例如 qcustomplot 仅在头/源存在时才创建目标）。
- **分层暴露**：`sin_core` 作为核心静态库公开包含路径，UI 层通过链接 `sin_ui` → `sin_core` 间接获得第三方头文件，实现依赖隔离。
- **Qt 自动化**：开启 `CMAKE_AUTOMOC/AUTOUIC/AUTORCC`，资源文件通过 `resources.qrc` 集成。
- **PCH 预编译头**：core 和 ui 分别维护精简的预编译头列表，减少编译时间。
- **驱动分发策略**：`driver/` 目录在 POST_BUILD 阶段整体拷贝至 `bin/`，保证运行时能找到 zlgcan.dll 等动态库。

## 4. 约定与约束

- 新增第三方库应放在 `third_party/<name>/` 下，并在 `third_party/Dependencies.cmake` 中以 `INTERFACE IMPORTED` 或 `STATIC` 目标形式暴露。
- 头文件-only 库不创建链接目标，仅通过 `target_include_directories(sin_core PUBLIC ${CMAKE_SOURCE_DIR}/third_party/<name>)` 暴露。
- 需要编译的库（如 qcustomplot）必须提供 `if(EXISTS ...)` 保护，确保可选依赖缺失时仍可构建。
- vector_blf 等带自身 CMakeLists 的项目通过 `add_subdirectory` 引入，且同样受 EXISTS 保护。
- 外部二进制驱动统一放置在 `driver/`，并通过 `POST_BUILD copy_directory` 复制到输出目录；安装规则也包含该目录的 `.dll` 匹配。
- 构建脚本 `scripts/build.py` 用于触发构建流程（具体参数见该文件）。
- 项目未使用任何包管理器或锁文件，因此版本更新需手动替换 `third_party/` 下的源码快照并重新提交。