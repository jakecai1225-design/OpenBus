---
kind: dependency_management
name: CMake + third_party 源码级依赖管理（Qt6、spdlog、nlohmann_json、QCustomPlot、vector_blf）
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - .gitignore
---

## 1. 使用的系统/方法

本项目采用 **CMake 3.21+** 作为构建系统，并通过将第三方库以**源码形式直接放入 `third_party/` 目录**（vendoring）的方式进行依赖管理。没有使用包管理器（如 vcpkg、Conan、FetchContent），也没有 lockfile；所有第三方头文件和源码随仓库一起提交。

- Qt6：通过 CMake 的 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 查找系统已安装的 Qt6，属于**外部依赖**，不 vendored。
- 其他第三方库全部 vendored：spdlog、nlohmann/json、concurrentqueue、qcustomplot、vector_blf、dbcppp（源码与 zip 同时存在）。
- 驱动 DLL（ZLG SDK、CAN 设备驱动）放在 `driver/` 目录，通过 CMake `POST_BUILD` 命令复制到输出目录，属于二进制运行时依赖。

## 2. 关键文件

- `CMakeLists.txt`（根）：声明项目、Qt6 查找、`include(third_party/Dependencies.cmake)`、添加 `src/` 子目录。
- `third_party/Dependencies.cmake`：集中定义第三方目标——
  - `spdlog`：`INTERFACE IMPORTED` 库，仅设置 `INTERFACE_INCLUDE_DIRECTORIES`。
  - `qcustomplot`：条件编译为 `STATIC` 库（需 `qcustomplot.cpp` 存在时），链接 `Qt6::Widgets`、`Qt6::PrintSupport`。
  - `vector_blf`：若其自带 `CMakeLists.txt` 则通过 `add_subdirectory` 引入。
- `src/CMakeLists.txt`：实际消费依赖——
  - `sin_core` 静态库通过 `target_include_directories(... PUBLIC ...)` 暴露 spdlog、nlohmann_json、concurrentqueue 的头路径。
  - `sin_ui` 静态库通过 `target_link_libraries(... PRIVATE qcustomplot Qt6::Svg)` 链接 QCustomPlot。
  - 通过 `if(EXISTS ...)` 条件判断第三方源码是否存在再启用对应 include path，使仓库在未下载完整 third_party 时仍可部分配置。
  - 通过 `POST_BUILD copy_directory` 将 `driver/` 下的 DLL 复制到可执行文件同级目录。
- `.gitignore`：忽略 `build/`、`tools/`、构建日志等，但**未忽略 `third_party/`**，表明 vendor 源码应纳入版本控制。

## 3. 架构与约定

- **分层依赖注入**：`third_party/Dependencies.cmake` 只负责“把第三方源码变成 CMake 目标”，不包含业务逻辑；`src/CMakeLists.txt` 负责“哪些目标需要哪些依赖”。
- **头文件-only 库优先**：spdlog、nlohmann/json、concurrentqueue 均以头文件形式引入，无需单独编译产物，降低链接复杂度。
- **可选依赖模式**：所有第三方路径均用 `if(EXISTS ...)` 包裹，允许开发者在缺少某 vendor 源码时仍能配置工程（例如 CI 中可能只拉取部分依赖）。
- **Qt 作为平台依赖**：Qt6 通过 `find_package` 获取，不在仓库内 vendored；构建环境必须预先安装 Qt6。
- **二进制驱动分离**：`driver/` 中的 DLL 不参与 CMake 编译，仅在构建后复制，保持源码树与运行时部署物分离。

## 4. 约定与约束

- 新增第三方库应遵循以下模式：
  1. 将源码放入 `third_party/<lib>/`；
  2. 在 `third_party/Dependencies.cmake` 中以 `INTERFACE IMPORTED`（纯头文件库）或 `add_library(... STATIC ...)`（含 .cpp 的库）方式注册；
  3. 在 `src/CMakeLists.txt` 中通过 `target_include_directories` / `target_link_libraries` 按需引入；
  4. 使用 `if(EXISTS ...)` 保护路径，保证缺失 vendor 时仍可配置。
- 版本锁定由**提交到仓库的源码快照**实现（即 vendoring 本身充当 lockfile），而非通过版本号声明。
- 无私有包管理器或代理配置；所有依赖来自公开 GitHub 仓库或本地源码。
- 构建工具链要求：CMake ≥ 3.21、Qt6（Widgets/PrintSupport/Svg）、MinGW 环境下默认使用 `ld.bfd` 链接器（见根 CMakeLists 注释说明）。