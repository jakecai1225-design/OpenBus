---
kind: dependency_management
name: 基于 CMake 与 third_party 源码内联的 Qt/C++ 依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - third_party/spdlog/
    - third_party/nlohmann_json/
    - third_party/concurrentqueue/
    - third_party/pugixml/
    - third_party/qcustomplot/
    - third_party/vector_blf/
---

## 1. 使用的系统与方式

本项目采用 **CMake + 源码内联（vendored）第三方库** 的方式管理依赖，没有使用任何包管理器（如 vcpkg、Conan、NuGet、npm 等），也没有 lockfile。所有第三方头文件/源码直接以源码形式存放在仓库根目录的 `third_party/` 子目录下，由顶层 `CMakeLists.txt` 通过 `include(third_party/Dependencies.cmake)` 统一注册为 CMake target。

Qt6 通过系统安装的 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 获取，属于“外部依赖”；其余第三方库全部 vendored。

## 2. 关键文件

- `CMakeLists.txt`：项目根构建入口，声明 C++17、Qt6 查找、包含 `third_party/Dependencies.cmake`、添加 `src/` 子目录。
- `third_party/Dependencies.cmake`：集中声明第三方依赖的 CMake target（spdlog INTERFACE IMPORTED、qcustomplot STATIC、vector_blf 通过 `add_subdirectory` 引入）。
- `src/CMakeLists.txt`：定义 `sin_core`、`sin_ui` 两个静态库及最终可执行目标 `sin`，并通过 `target_include_directories` / `target_link_libraries` 将第三方头路径和库链接到对应层。
- `third_party/` 目录：实际存放 vendored 源码的位置。

## 3. 架构与约定

### 3.1 分层暴露依赖

依赖按使用范围分层暴露：
- `sin_core` 静态库通过 `PUBLIC` 的 `target_include_directories` 暴露 spdlog、nlohmann_json、concurrentqueue、pugixml 的头路径，使 core 层的公共头（如 `core/logging.h`、`core/appconfig.h`、`core/cansimulator.h`、`core/dbc/arxml_importer.h`）可以直接 `#include <spdlog/spdlog.h>`、`#include <nlohmann/json.hpp>`、`#include <concurrentqueue.h>`、`#include "pugixml.hpp"`。
- `sin_ui` 仅 PRIVATE 链接 qcustomplot 和 Qt6::Svg，因为 qcustomplot 只在 `graphicview.cpp` 中使用，且 `graphicview.h` 用前向声明避免传播依赖。
- 最终可执行目标 `sin` 只 PRIVATE 链接 `sin_ui`，形成 `sin → sin_ui → sin_core → 第三方头/库` 的单向依赖链。

### 3.2 各第三方库的集成方式

| 库 | 位置 | 集成方式 | 说明 |
|---|---|---|---|
| spdlog | `third_party/spdlog/` | INTERFACE IMPORTED target，仅设置 include 目录 | 纯头文件库 |
| nlohmann/json | `third_party/nlohmann_json/` | 仅通过 `target_include_directories` 暴露单头文件 | 单头文件库，无需链接 |
| concurrentqueue | `third_party/concurrentqueue/` | 仅 include 目录暴露 | 单头文件并发队列 |
| pugixml | `third_party/pugixml/` | 条件 include 目录 + 可选 pugixml target 链接 | 用于 ARXML 导入 |
| qcustomplot | `third_party/qcustomplot/` | 编译为 STATIC library，并链接 Qt6::Widgets、Qt6::PrintSupport | 需编译 `.cpp` 的源码库 |
| vector_blf | `third_party/vector_blf/` | 条件 `add_subdirectory` 引入其 CMakeLists.txt | GPL-3.0，支持 BLF 读写 |
| zlib | 系统/MinGW 自带 | `target_link_libraries(sin_core PRIVATE z)` | BLF 解压使用 |
| Qt6 | 系统安装 | `find_package(Qt6 ...)` | 框架依赖 |
| ZLG CAN 驱动 DLL | `driver/` | POST_BUILD 拷贝到输出目录 | 运行时二进制，非编译期依赖 |

### 3.3 条件加载策略

所有第三方依赖都使用 `if(EXISTS ...)` 或 `if(TARGET ...)` 包裹，确保当某个 vendored 目录缺失时构建不会失败（便于最小化 checkout）。例如：
- `if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")`
- `if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/qcustomplot/qcustomplot.h" AND EXISTS "...")`
- `if(TARGET pugixml)`

### 3.4 预编译头（PCH）隔离依赖

`src/CMakeLists.txt` 为 `sin_core` 和 `sin_ui` 分别配置了精简的 PCH 头列表，将常用 Qt 和 STL 头提前编译，减少第三方头对编译时间的影响。

## 4. 约定与约束

- **源码内联优先**：除 Qt6 和系统 zlib 外，所有第三方库以源码形式随仓库分发，不依赖远程包管理器。
- **头文件路径集中管理**：第三方 include 路径集中在 `src/CMakeLists.txt` 中通过 `target_include_directories` 暴露，业务代码不应直接写死 `${CMAKE_SOURCE_DIR}/third_party/...`。
- **按需 PRIVATE 链接**：qcustomplot 等仅在 UI 层使用的库以 `PRIVATE` 链接，避免污染下游依赖图。
- **条件存在性检查**：新增第三方库时应遵循现有模式，用 `if(EXISTS ...)` 包裹，保证部分 vendoring 场景下仍可构建。
- **运行时二进制独立处理**：ZLG 驱动 DLL 不属于编译期依赖，通过 `POST_BUILD` 命令复制到输出目录，安装时也单独 `install(DIRECTORY ...)`。
- **无版本锁定机制**：仓库未使用 lockfile 或版本标签来固定第三方库版本，更新 vendored 源码需手动替换 `third_party/` 下的内容。
- **许可证标注**：`third_party/Dependencies.cmake` 中对 vector_blf 明确标注来源 URL 与 GPL-3.0 许可，便于合规追踪。