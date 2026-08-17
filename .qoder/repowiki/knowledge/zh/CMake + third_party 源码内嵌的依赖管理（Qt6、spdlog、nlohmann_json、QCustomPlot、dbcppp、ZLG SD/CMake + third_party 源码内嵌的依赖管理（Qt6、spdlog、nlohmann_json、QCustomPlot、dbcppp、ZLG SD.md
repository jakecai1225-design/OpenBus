---
kind: dependency_management
name: CMake + third_party 源码内嵌的依赖管理（Qt6、spdlog、nlohmann_json、QCustomPlot、dbcppp、ZLG SDK）
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - driver/zlgcan.h
    - third_party/spdlog/include/spdlog/spdlog.h
    - third_party/nlohmann_json/nlohmann/json.hpp
    - third_party/qcustomplot/qcustomplot.h
    - third_party/vector_blf/CMakeLists.txt
    - third_party/pugixml/pugixml.hpp
    - third_party/concurrentqueue/concurrentqueue.h
    - third_party/dbcppp/CMakeLists.txt
---

## 1. 使用的系统/方法

本项目采用 **CMake 3.21+** 作为构建系统，并通过将第三方库以 **源码形式直接内嵌到 `third_party/` 目录** 的方式进行依赖管理。没有使用 vcpkg、Conan、NuGet、npm、pip 等包管理器；也没有 lockfile（如 `package-lock.json`、`go.sum`）。依赖版本通过提交到仓库的源码快照固定。

- 顶层 `CMakeLists.txt` 声明项目并 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)`，随后 `include(third_party/Dependencies.cmake)` 引入第三方依赖配置，再 `add_subdirectory(src)` 编译主工程。
- `src/CMakeLists.txt` 将代码组织为两个静态库 `openbus_core` 和 `openbus_ui`，最终链接为可执行目标 `openbus`。

## 2. 关键文件与位置

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层 CMake 入口：定义 Qt6 查找、包含 `third_party/Dependencies.cmake`、设置输出目录 `bin/` |
| `src/CMakeLists.txt` | 核心构建逻辑：声明源文件列表、创建 `openbus_core` / `openbus_ui` 静态库、链接 Qt6 及第三方库、PCH、驱动 DLL 拷贝 |
| `third_party/Dependencies.cmake` | 集中声明第三方 target：spdlog（INTERFACE IMPORTED）、qcustomplot（STATIC）、vector_blf（可选 add_subdirectory） |
| `third_party/` | 所有第三方源码根目录 |
| `driver/` | ZLG CAN 硬件 SDK（`zlgcan.dll`、`zlgcan.h`、`zlgcan.lib` 及若干设备驱动 DLL），通过 `POST_BUILD` 命令复制到 exe 同级目录 |
| `plugins/*/plugin.json` | Python 插件元数据（非 C++ 依赖，但体现运行时扩展点） |

## 3. 架构与约定

### 3.1 第三方库分类与接入方式

| 第三方库 | 类型 | 接入方式 | 备注 |
|---|---|---|---|
| Qt6 | 外部框架 | `find_package(Qt6 ...)` | 由开发者本地安装，CMake 查找 |
| spdlog | 头文件库 | INTERFACE IMPORTED target，仅暴露 include 路径 | 未编译源码，避免 fmt 等额外依赖 |
| nlohmann/json | 单头文件 | 直接 include，无 target 声明 | 放在 `third_party/nlohmann_json/` |
| QCustomPlot | 源码库 | 显式 `add_library(qcustomplot STATIC qcustomplot.cpp)` | 仅在 `graphicview.cpp` 使用，故对 UI 库 PRIVATE 链接 |
| vector_blf | 源码库 | 条件 `add_subdirectory(...)`，存在 `CMakeLists.txt` 时才启用 | 带 GPL-3.0 许可证注释 |
| pugixml | 头文件库 | 通过 `target_include_directories(openbus_core PUBLIC ...)` 暴露 | 被 `arxml_importer` 使用 |
| concurrentqueue | 头文件库 | 同上，通过 include path 暴露 | 用于 `utils/message_queue.h` |
| dbcppp | 源码库（含子模块） | 仓库中存在完整源码，但当前未被本项目的 CMake 引入 | 位于 `third_party/dbcppp/`，其自身 `CMakeLists.txt` 要求 Boost/LibXml2 |
| ZLG SDK (`driver/`) | 预编译 DLL + 头文件 | 运行时动态加载，构建期通过 `copy_directory` 复制 DLL | 必须与 exe 同目录 |
| zlib | 系统库 | `target_link_libraries(openbus_core PRIVATE z)` | MinGW 自带 `libz.a` |

### 3.2 依赖可见性约定

- 只有 `openbus_core` 需要向下游暴露第三方 include 路径（因为 `core/logging.h`、`core/appconfig.h`、`core/cansimulator.h`、`core/dbc/arxml_importer.h` 等公共头会 `#include <spdlog/spdlog.h>`、`<nlohmann/json.hpp>`、`<concurrentqueue.h>`、`<pugixml.hpp>`）。因此这些 include 路径以 `PUBLIC` 附加到 `openbus_core`。
- `qcustomplot` 仅被 UI 层使用，且 `graphicview.h` 用前向声明，因此对 `openbus_ui` 是 `PRIVATE` 链接，避免污染上游。
- 所有第三方库的 include 路径都通过 `if(EXISTS "...")` 守卫，允许在缺少某第三方时仍能编译（如 `vector_blf`、`pugixml`、`concurrentqueue` 均如此处理）。

### 3.3 运行时依赖分发

- `driver/` 目录下的 ZLG 驱动 DLL 通过 `add_custom_command(TARGET openbus POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ...)` 在每次构建后自动复制到输出目录。
- `install()` 规则也将 `driver/*.dll` 一并安装到 `bin/`，保证安装包可用。

## 4. 约定与约束

1. **第三方源码必须放入 `third_party/<name>/`**：新增依赖应把源码解压到该目录，并在 `third_party/Dependencies.cmake` 中注册 target 或 expose include path。
2. **优先使用头文件库**：spdlog、nlohmann/json、concurrentqueue、pugixml 均以头文件形式引入，避免引入额外的构建产物和传递依赖。
3. **可选依赖统一用 `if(EXISTS ...)` 守卫**：当某个第三方缺失时，对应功能降级（如 `HAS_VECTOR_BLF` 宏控制 BLF 读写分支），而不是直接编译失败。
4. **Qt6 必须在系统环境中可用**：通过 `find_package(Qt6 REQUIRED ...)` 强制要求，不在仓库中携带 Qt 源码。
5. **ZLG SDK 二进制随仓库分发**：`driver/zlgcan.dll`、`driver/kerneldlls/*.dll` 等预编译 DLL 直接纳入版本控制，构建时复制到 exe 同级目录，运行时由程序动态加载。
6. **dbcppp 尚未集成**：虽然 `third_party/dbcppp/` 存在完整源码，但当前工程的 CMake 并未将其加入构建流程；如需启用需修改 `src/CMakeLists.txt` 并满足其 Boost/LibXml2 依赖。
7. **Python 插件**：`plugins/` 下的 Python 插件通过 `plugin.json` 描述，由应用运行时动态发现加载，不属于 C++ 依赖范畴。
8. **无锁文件/包管理器**：依赖版本完全由提交的源码快照决定，更新第三方库意味着手动替换 `third_party/` 下对应目录的内容并提交变更。