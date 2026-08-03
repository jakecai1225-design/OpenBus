---
kind: dependency_management
name: 第三方依赖管理（CMake + 源码内嵌 + Python 下载脚本）
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/download_spdlog.py
    - scripts/download_json.py
    - scripts/download_concurrentqueue.py
    - scripts/download_qcustomplot.py
    - scripts/download_exprtk.py
---

本项目采用 **CMake 作为构建系统**，通过 `third_party/` 目录以**源码内嵌（vendoring）**方式管理所有第三方依赖，配合若干 Python 下载脚本完成依赖的获取与版本锁定。整体策略如下：

1. **依赖声明与发现**
   - 根 `CMakeLists.txt` 使用 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)` 查找 Qt6，并通过 `include(${CMAKE_SOURCE_DIR}/third_party/Dependencies.cmake)` 引入第三方依赖配置。
   - `third_party/Dependencies.cmake` 集中定义各库的 CMake 目标：spdlog 通过 `INTERFACE IMPORTED` 仅暴露 include 路径；nlohmann/json 为单头文件直接 `#include "json.hpp"`；qcustomplot 作为静态库编译并链接 Qt6::Widgets、Qt6::PrintSupport；vector_blf 通过 `add_subdirectory` 子工程方式集成。

2. **源码内嵌与版本控制**
   - 所有第三方源码均位于 `third_party/<lib>/` 目录下，随仓库一起提交，确保可复现构建。
   - 部分库提供专用下载脚本固定版本：`download_spdlog.py` 明确抓取 v1.14.1 的 zip 包并解压到 `third_party/spdlog`；`download_json.py` 从 nlohmann/json develop 分支拉取单头文件；`download_concurrentqueue.py` 抓取 moodycamel::ConcurrentQueue 的单头文件；`download_qcustomplot.py` 和 `download_exprtk.py` 分别拉取对应头文件。
   - `dbcppp.zip` 与 `dbcppp_src/` 并存，表明 dbcppp 可能通过 zip 分发后再解压到源码目录。

3. **构建与部署自动化**
   - `scripts/build.py` 是统一的构建入口，支持 configure / build / run / debug / clean / rebuild / deploy / all / status / open 等子命令。
   - 自动检测并使用 Ninja、ccache、LLD 等加速工具（若存在于 `tools/` 目录），通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 覆盖默认路径。
   - 部署阶段调用 `windeployqt.exe` 打包 Qt 运行时，并手动补充 qcustomplot 静态库对 Qt6PrintSupport.dll 的传递依赖。

4. **约定与约束**
   - 所有第三方依赖必须放在 `third_party/` 下，并在 `Dependencies.cmake` 中显式声明其 include 路径或编译规则。
   - 头文件型库（spdlog、nlohmann/json、concurrentqueue、exprtk）以单头文件或仅 include 的方式集成，避免额外链接步骤。
   - 需要编译的库（qcustomplot、vector_blf）在 `Dependencies.cmake` 中以 `add_library` 或 `add_subdirectory` 形式注册。
   - 依赖版本由下载脚本中的 URL/tag 固定，而非通过包管理器解析，保证离线可构建。

5. **未使用的包管理器**
   - 仓库中未发现 `go.mod`、`package.json`、`vcpkg.json`、`conanfile.py`、`Cargo.toml` 等现代依赖管理清单，说明项目完全依赖手工 vendoring + CMake 组合，未采用任何远程包管理器。