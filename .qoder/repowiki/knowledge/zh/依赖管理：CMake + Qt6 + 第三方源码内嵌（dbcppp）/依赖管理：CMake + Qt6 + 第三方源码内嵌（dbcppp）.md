---
kind: dependency_management
name: 依赖管理：CMake + Qt6 + 第三方源码内嵌（dbcppp）
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/dbcppp/CMakeLists.txt
    - third_party/dbcppp/src/libdbcppp/CMakeLists.txt
---

本仓库采用 CMake 作为唯一构建与依赖管理系统，未使用任何包管理器（如 vcpkg、Conan、pkg-config 等），所有第三方依赖通过 `find_package` 在系统环境中查找或由项目自身内嵌源码提供。

1. 系统与环境依赖
- Qt6：根 `CMakeLists.txt` 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)` 查找，要求 Qt6 已安装至系统路径；启用 `qt_standard_project_setup()` 并开启 AUTOMOC/AUTOUIC/AUTORCC 自动处理。
- Boost 1.72.0 与 LibXml2：由 `third_party/dbcppp/CMakeLists.txt` 中的 `find_package(Boost 1.72.0 REQUIRED COMPONENTS program_options)` 和 `find_package(LibXml2 REQUIRED)` 声明，需在构建机预装。
- 编译器标准：统一要求 C++17（`CMAKE_CXX_STANDARD 17`），禁用扩展（`CMAKE_CXX_EXTENSIONS OFF`）。

2. 第三方库策略：源码内嵌（vendoring）
- dbcppp（DBC 文件解析库）以完整源码形式存放在 `third_party/dbcppp/`，包含其自身的 CMake 工程结构（`src/libdbcppp`、`src/dbcppp`、`tests`、`examples`），并通过 `add_subdirectory` 集成到主构建。
- dbcppp 内部还 vendored 了 `libxmlmm`（位于 `third_party/dbcppp/third_party/libxmlmm/`），由顶层 CMake 直接编译为共享库 `libxmlmm`。
- 同时存在 `third_party/dbcppp_src/` 与 `third_party/dbcppp.zip`，表明源码可能从 zip 解压而来，但当前构建仅使用 `third_party/dbcppp/`。
- 主工程 `src/CMakeLists.txt` 中**未显式链接 libdbcppp**，说明 DBC 解析功能可能尚未在主工程中启用或仍通过动态加载方式引入。

3. 依赖声明与版本约束
- 无全局锁文件或版本锁定机制（无 `vcpkg.json`、`conan.lock`、`package-lock.json` 等）。
- 版本约束硬编码在 CMake 中：Qt6（无具体版本）、Boost 1.72.0、LibXml2（无版本）。升级需手动修改对应 `find_package` 调用。
- 无私有仓库或代理配置，完全依赖本地系统环境提供的库。

4. 构建输出与安装
- 可执行文件输出至 `${CMAKE_BINARY_DIR}/bin`。
- 安装规则将目标安装至 `bin` 目录，头文件安装至 `include/dbcppp`。
- Windows 下通过 `WIN32_EXECUTABLE TRUE` 生成 GUI 程序（不弹出控制台）。

5. 约定与限制
- 所有依赖必须预先安装在构建机系统中，无法通过单一命令拉取依赖。
- 第三方库升级需要手动更新源码并调整 CMake 配置。
- 缺乏依赖版本一致性保障，不同开发者/CI 环境可能因系统库版本差异导致构建不一致。