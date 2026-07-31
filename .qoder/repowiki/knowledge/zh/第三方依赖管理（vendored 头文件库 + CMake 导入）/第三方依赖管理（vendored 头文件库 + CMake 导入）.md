---
kind: dependency_management
name: 第三方依赖管理（vendored 头文件库 + CMake 导入）
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/download_spdlog.py
    - scripts/download_json.py
    - scripts/download_concurrentqueue.py
    - scripts/download_qcustomplot.py
    - scripts/download_exprtk.py
---

本项目采用 **vendored 单头文件/纯头文件库** 策略，所有第三方依赖以源码形式直接放入 `third_party/` 目录，通过 CMake 的 INTERFACE IMPORTED 目标引入，不使用包管理器或系统包管理器。具体方式如下：

1. **依赖下载脚本**：`scripts/` 目录下提供多个 Python 脚本（`download_spdlog.py`、`download_json.py`、`download_concurrentqueue.py`、`download_qcustomplot.py`、`download_exprtk.py`），通过 `urllib.request.urlretrieve` 从 GitHub/GitHub Raw 拉取特定版本的源码到 `third_party/` 对应子目录。例如 spdlog v1.14.1 以 zip 形式下载并解压。

2. **CMake 集成**：`third_party/Dependencies.cmake` 将 vendored 库声明为 INTERFACE IMPORTED 目标，仅设置 `INTERFACE_INCLUDE_DIRECTORIES`，因为所有依赖都是纯头文件库（spdlog、nlohmann/json、concurrentqueue、qcustomplot、exprtk）。Qt6 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets)` 查找系统安装的 Qt。

3. **构建系统集成**：根 `CMakeLists.txt` 在 `add_subdirectory(src)` 之前 include `third_party/Dependencies.cmake`；`src/CMakeLists.txt` 中通过 `target_include_directories(sin_core PUBLIC ...)` 和 `target_link_libraries(... PRIVATE qcustomplot)` 将依赖暴露给模块。每个依赖的存在性通过 `if(EXISTS ...)` 条件判断，保证未下载时仍可配置。

4. **依赖清单与版本锁定**：依赖版本由下载脚本中的 URL 硬编码控制（如 `v1.14.1.zip`、`master/concurrentqueue.h`），没有统一的 `lockfile` 或 `go.mod`/`package.json` 类文件。dbcppp 同时提供源码目录 `third_party/dbcppp_src/` 和预编译 zip `third_party/dbcppp.zip`，说明该库需要编译而非纯头文件。

5. **构建产物**：最终可执行文件位于 `bin/sin.exe`，构建日志散落在根目录多个 `build_*.log` 文件中，无统一构建缓存或增量依赖追踪机制。