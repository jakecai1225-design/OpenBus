---
kind: dependency_management
name: 依赖管理 — CMake + 第三方源码内嵌（third_party）+ Python 下载脚本
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - scripts/build.py
    - scripts/download_spdlog.py
    - scripts/download_json.py
    - scripts/download_qcustomplot.py
    - scripts/download_concurrentqueue.py
    - scripts/download_exprtk.py
---

## 1. 系统/工具概览
- 构建系统：CMake 3.21+，配合 Qt6 的 find_package 与 qt_standard_project_setup。
- 依赖获取：无包管理器（如 vcpkg/conan），所有第三方库以源码形式直接放入 `third_party/` 目录，通过 Python 脚本从 GitHub Releases 下载并解压。
- 依赖声明：集中在 `third_party/Dependencies.cmake`，使用 INTERFACE IMPORTED 或 add_library STATIC 暴露头文件路径与链接目标；顶层 `CMakeLists.txt` 通过 include 引入。
- 部署：Windows 下通过 `windeployqt` 自动收集 Qt DLL，静态库 qcustomplot 对 Qt6PrintSupport 的传递依赖由 build.py 手动补充复制。

## 2. 关键文件与位置
- 顶层依赖入口：`CMakeLists.txt`（find_package(Qt6)）、`third_party/Dependencies.cmake`（spdlog、nlohmann/json、qcustomplot、vector_blf 的 target 定义）
- 子模块依赖引用：`src/CMakeLists.txt`（sin_core/sin_ui 通过 target_include_directories 和 target_link_libraries 引用 third_party 头文件与库）
- 依赖下载脚本：`scripts/download_spdlog.py`、`scripts/download_json.py`、`scripts/download_qcustomplot.py`、`scripts/download_concurrentqueue.py`、`scripts/download_exprtk.py`
- 构建/部署编排：`scripts/build.py`（configure/build/run/debug/clean/rebuild/deploy/all/status/open）
- 第三方源码目录：`third_party/{spdlog,nlohmann_json,qcustomplot,vector_blf,concurrentqueue,dbcppp,dbcppp_src}/`

## 3. 架构与约定
- 源码内嵌策略：第三方库以“只读源码”形式随仓库分发，不依赖外部包管理器。每个库在 `third_party/` 下有独立子目录，头文件位于各自 include 或根目录。
- CMake 抽象层：`third_party/Dependencies.cmake` 为每个依赖创建统一 target（如 spdlog INTERFACE IMPORTED、qcustomplot STATIC、vector_blf 通过 add_subdirectory），屏蔽具体源码结构差异。
- 分层链接：`src/CMakeLists.txt` 将 sin_core（核心逻辑）与 sin_ui（界面）拆分为两个静态库，UI 层 PRIVATE 依赖 qcustomplot，core 层 PUBLIC 暴露 spdlog/nlohmann_json/concurrentqueue 的头文件路径，避免 UI 层感知这些实现细节。
- 可选依赖保护：大量 `if(EXISTS ...)` 判断确保缺少某个 third_party 目录时仍可配置（如 nlohmann_json、concurrentqueue、vector_blf），保持最小可用构建。
- 构建加速：启用 PCH（target_precompile_headers）与 Ninja 生成器（优先检测 `tools/ninja/ninja.exe`），减少编译时间。

## 4. 约定与约束
- 版本锁定方式：通过 Python 下载脚本中的固定 URL/tag 锁定版本（例如 spdlog v1.14.1），而非 lockfile；更新依赖需修改对应脚本中的版本号与 URL。
- 许可证合规：vector_blf 明确标注 GPL-3.0 来源与用途，需在发布时遵守相应许可。
- 平台特定处理：Windows MinGW 环境下禁用 ccache（与 PCH 不兼容）与 LLD 链接器（文件锁问题），默认使用 ld.bfd。
- 运行时依赖：Qt 依赖通过 windeployqt 自动收集，但静态库 qcustomplot 对 Qt6PrintSupport 的传递依赖需要 build.py 中 cmd_deploy 手动复制 DLL。
- 环境变量覆盖：SIN_QT_DIR、SIN_MINGW_DIR、SIN_CMAKE_DIR 可覆盖默认工具路径，便于多环境切换。
- 依赖发现顺序：CMake 配置阶段按 `third_party/` 存在性动态决定包含哪些依赖，未提供的库不会导致配置失败。
