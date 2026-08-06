---
kind: dependency_management
name: CMake + third_party 源码内嵌依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - scripts/build.py
    - scripts/download_spdlog.py
    - scripts/download_json.py
    - scripts/download_concurrentqueue.py
    - scripts/download_qcustomplot.py
    - scripts/download_exprtk.py
---

本项目采用 CMake 作为构建系统，通过 `third_party/` 目录内嵌第三方库源码的方式管理依赖，不使用包管理器（如 vcpkg、Conan）或远程依赖解析。具体策略如下：

**1. 依赖获取与版本锁定**
- 所有第三方库以源码形式直接存放在 `third_party/` 目录下，包括 spdlog、nlohmann/json、concurrentqueue、qcustomplot、exprtk、dbcppp 等。
- 每个库提供独立的 Python 下载脚本（`scripts/download_*.py`），从 GitHub raw 或 Release 页面拉取指定版本的源码。例如 `download_spdlog.py` 固定下载 `v1.14.1`，`download_json.py` 从 `develop` 分支拉取单头文件。
- 部分库为单头文件库（json.hpp、concurrentqueue.h、exprtk.hpp），直接拷贝到对应目录；spdlog 则解压 zip 归档后重命名目录。
- 没有 lockfile 或版本清单文件，版本信息硬编码在下载脚本中。

**2. CMake 集成方式**
- `third_party/Dependencies.cmake` 集中声明第三方依赖的 CMake 目标：
  - spdlog、nlohmann_json 通过 `INTERFACE IMPORTED` 库暴露 include 路径，无需编译。
  - qcustomplot 作为静态库编译并链接 Qt6::Widgets。
- `src/CMakeLists.txt` 通过 `target_include_directories` 和 `target_link_libraries` 将依赖引入 sin_core 和 sin_ui 两个静态库，使用 `PUBLIC`/`PRIVATE` 区分可见性。
- Qt6 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)` 查找，路径由 `build.py` 的 `-DCMAKE_PREFIX_PATH` 传入。

**3. 构建与部署流程**
- `scripts/build.py` 统一封装 CMake 配置、编译、运行、调试、部署（windeployqt）全流程，自动检测 Ninja/ccache/lld 加速工具。
- Windows 平台通过 `bin/` 目录分发 Qt 运行时 DLL（Qt6Core.dll、Qt6Gui.dll 等）及 MinGW 运行时（libgcc_s_seh-1.dll、libstdc++-6.dll、libwinpthread-1.dll），由 `windeployqt` 自动生成。

**4. 设计约定与约束**
- 第三方库仅通过头文件包含使用，不引入动态链接（除 Qt 运行时外），保证可执行文件自包含。
- 依赖头文件路径通过 CMake 的 `target_include_directories` 传递，避免全局污染。
- 下载脚本需手动运行以更新依赖，无自动化依赖检查机制。
- 项目未使用 Git Submodule，第三方源码直接提交至仓库。