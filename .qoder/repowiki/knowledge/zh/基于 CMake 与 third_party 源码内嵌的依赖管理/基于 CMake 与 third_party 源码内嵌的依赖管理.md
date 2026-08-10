---
kind: dependency_management
name: 基于 CMake 与 third_party 源码内嵌的依赖管理
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

本项目采用 **CMake + 源码级 vendoring（third_party 目录）** 的方式管理第三方依赖，没有使用任何包管理器（如 vcpkg、Conan、NuGet 等）或远程依赖解析工具。所有第三方库以源码形式直接拷贝到 `third_party/` 目录下，通过 CMake 的 `add_library` / `target_include_directories` / `target_link_libraries` 显式集成。

- 构建系统：CMake 3.21+，C++17，Qt6（Widgets / PrintSupport / Svg）。
- 依赖发现：`find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 查找系统安装的 Qt；其余依赖全部来自本地 `third_party/`。
- 无 lockfile、无私有仓库配置、无版本锁定文件——依赖版本由提交到仓库的源码快照决定。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层 CMake 入口，声明 Qt6 查找并 include `third_party/Dependencies.cmake` |
| `third_party/Dependencies.cmake` | 集中声明第三方目标：spdlog（INTERFACE IMPORTED）、qcustomplot（STATIC）、vector_blf（subdirectory） |
| `src/CMakeLists.txt` | 将 sin_core、sin_ui 两个静态库链接到 Qt 及第三方头路径，定义 PCH，并在 Windows POST_BUILD 阶段复制 driver DLL |
| `.gitignore` | 忽略 build/、tools/、*.dll 等产物，但保留 third_party 源码 |
| `driver/` | ZLG CAN SDK 的二进制驱动 DLL（zlgcan.dll 等），编译后复制到 exe 同级目录 |

## 3. 架构与约定

### 3.1 源码内嵌策略
`third_party/` 下按库名分目录存放源码：
- `spdlog/` — 仅暴露头文件，通过 `INTERFACE_INCLUDE_DIRECTORIES` 暴露给 sin_core。
- `nlohmann_json/` — 单头文件库，直接 include 路径指向该目录。
- `concurrentqueue/` — 单头文件并发队列，同样通过 include path 暴露。
- `pugixml/` — XML 解析，被 arxml_importer 使用。
- `qcustomplot/` — 图表库，作为 STATIC 库编译 qcustomplot.cpp，并链接 Qt6::Widgets、Qt6::PrintSupport。
- `vector_blf/` — Vector BLF 读写库（GPL-3.0），通过 `add_subdirectory` 引入其自身 CMakeLists。
- `dbcppp/`、`dbcppp_src/`、`dbcppp.zip` — DBC 解析相关源码/压缩包（存在于 tree 中，但当前 Dependencies.cmake 未将其加入构建）。

### 3.2 分层链接
- `sin_core`（核心静态库）：公开包含 spdlog、nlohmann_json、concurrentqueue、pugixml 的头路径，并通过 `target_link_libraries(sin_core PRIVATE z)` 链接 zlib（MinGW 自带 libz.a），用于 BLF 解压。
- `sin_ui`（UI 静态库）：公开依赖 sin_core，私有链接 qcustomplot 和 Qt6::Svg。
- `sin`（可执行文件）：仅链接 sin_ui，形成 `sin → sin_ui → sin_core → Qt/zlib/third_party` 的单向依赖链。

### 3.3 运行时二进制分发
`driver/` 下的 ZLG SDK DLL（zlgcan.dll、CANDevice.dll 等）在 POST_BUILD 阶段通过 `cmake -E copy_directory` 复制到输出目录，安装规则也包含这些 DLL。这是**二进制分发而非源码集成**的例外情况。

## 4. 约定与约束

- **所有第三方头文件必须位于 `third_party/<lib>/include` 或对应目录**：CMake 通过 `EXISTS` 检查后再添加 include 路径，缺失时静默跳过（例如 spdlog、nlohmann_json、concurrentqueue、pugixml 均用 `if(EXISTS ...)` 保护）。
- **可选依赖通过 `if(TARGET <name>)` 或 `if(EXISTS ...)` 条件启用**：qcustomplot、vector_blf、pugixml 均以存在性检测包裹，保证在没有完整 third_party 时项目仍可部分构建。
- **Qt 是唯一的系统级依赖**：通过 `find_package(Qt6 REQUIRED ...)` 强制要求系统已安装 Qt6，其他依赖不依赖系统包管理器。
- **无版本锁定机制**：依赖版本完全由 git 提交的源码快照决定；更新依赖需要手动替换 third_party 目录中的源码。
- **许可证标注**：vector_blf 在 Dependencies.cmake 中以注释标注 GPL-3.0，提示合规注意。
- **构建产物隔离**：`.gitignore` 明确忽略 build/、tools/、*.dll、*.exe 等，确保只有源码形式的依赖进入版本控制。