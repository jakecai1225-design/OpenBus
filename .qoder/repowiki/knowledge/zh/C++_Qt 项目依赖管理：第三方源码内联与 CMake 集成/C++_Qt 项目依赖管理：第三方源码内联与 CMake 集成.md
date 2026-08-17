---
kind: dependency_management
name: C++/Qt 项目依赖管理：第三方源码内联与 CMake 集成
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - sdk/sin/__init__.py
    - plugins/frame-counter/plugin.json
---

## 1. 使用的系统与方法

本项目采用 **CMake + Qt6** 构建，依赖管理策略为 **第三方源码直接内联（vendoring）** 至 `third_party/` 目录，通过 CMake 的 `add_library` / `INTERFACE IMPORTED` / `add_subdirectory` 等方式在构建时纳入编译。

- **包管理器**：未使用任何语言级包管理器（无 `go.mod`、`package.json`、`requirements.txt`、`pyproject.toml`）。Python 插件 SDK 仅依赖 PyQt6，且以可选导入方式加载（见 `sdk/sin/__init__.py` 中 try/except ImportError 处理）。
- **版本锁定**：无 lockfile。所有第三方库以固定快照形式存放在 `third_party/` 子目录中，版本号由源码提交决定。
- **私有仓库**：无私有注册表配置；vendor 目录即“私有源”。

## 2. 关键文件

- `CMakeLists.txt`（根）：声明 Qt6 查找、包含 `third_party/Dependencies.cmake`、设置 C++17。
- `src/CMakeLists.txt`：定义 `openbus_core`、`openbus_ui` 两个静态库及最终可执行目标 `openbus`，集中链接第三方头文件路径与库。
- `third_party/Dependencies.cmake`：统一声明第三方依赖（spdlog、qcustomplot、vector_blf）。
- `third_party/`：实际 vendor 目录，包含 spdlog、nlohmann_json、pugixml、concurrentqueue、qcustomplot、dbcppp/dbcppp_src、vector_blf。
- `driver/`：ZLG CAN 硬件驱动 DLL（zlgcan.dll 等），通过 CMake `POST_BUILD copy_directory` 复制到输出目录。
- `plugins/*/plugin.json`：Python 插件元数据，描述插件名称/版本/入口。

## 3. 架构与约定

### 3.1 第三方库分类与接入方式

| 依赖 | 类型 | 接入方式 | 说明 |
|---|---|---|---|
| Qt6 | 外部框架 | `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` | 通过系统/环境安装提供 |
| spdlog | 纯头文件库 | `INTERFACE IMPORTED` + `INTERFACE_INCLUDE_DIRECTORIES` | 零链接开销 |
| nlohmann/json | 单头文件 | 直接 include 路径指向 `third_party/nlohmann_json` | 无需额外目标 |
| qcustomplot | 源码库 | `add_library(qcustomplot STATIC ...)` 显式编译 `.cpp` | 仅当 `.h/.cpp` 存在时才添加 |
| vector_blf | 子工程 | `add_subdirectory(...)` 条件启用 | 需自带 `CMakeLists.txt` |
| pugixml | 头文件/库 | 同时支持 include path 和 `TARGET pugixml` 两种模式 |
| concurrentqueue | 头文件 | include path 暴露给 `openbus_core` |
| dbcppp | 源码库 | 位于 `third_party/dbcppp` / `dbcppp_src`，供 DBC 解析使用 |

### 3.2 分层依赖隔离

`src/CMakeLists.txt` 将依赖按层拆分：
- `openbus_core`（核心逻辑）：公开 Qt6::Widgets、zlib、可选 vector_blf/pugixml，并通过 `PUBLIC target_include_directories` 暴露 spdlog、nlohmann_json、concurrentqueue、pugixml 的头文件路径。
- `openbus_ui`（界面）：仅 PRIVATE 链接 `qcustomplot`、`Qt6::Svg`、Windows 下的 `psapi`，避免 UI 层污染 core 的依赖面。
- 最终 `openbus` 可执行目标只 PRIVATE 链接 `openbus_ui`，形成单向依赖链。

### 3.3 硬件驱动分发

`driver/` 目录存放 ZLG 厂商提供的二进制驱动 DLL（如 `zlgcan.dll`、`USBCANFD800U.dll` 等）及配套 XML 配置文件。构建脚本通过 `POST_BUILD` 命令将整个 `driver/` 目录拷贝到可执行文件同级目录，确保运行时能找到 USB 设备驱动。

### 3.4 Python 插件生态

- 插件位于 `plugins/<name>/`，每个插件含 `main.py` 和 `plugin.json` 元数据。
- 插件通过 `import sin` 调用内置 SDK（`sdk/sin/`），SDK 对 PyQt6 做可选导入，保证在无 GUI 环境下仍可运行。
- 插件本身不声明依赖清单，依赖由宿主程序（主进程）提供。

## 4. 约定与约束

- **vendor 优先**：所有第三方 C++ 库均以源码形式随仓库发布，不在 CI 或构建流程中从网络下载，保证离线可构建。
- **条件启用**：对非必需依赖（qcustomplot、vector_blf、pugixml）均使用 `if(EXISTS ...)` 或 `if(TARGET ...)` 包裹，缺失时不影响基础构建。
- **Qt 版本锁定**：根 `CMakeLists.txt` 要求 `cmake_minimum_required(VERSION 3.21)` 并固定使用 Qt6，未兼容 Qt5。
- **C++ 标准**：强制 C++17（`CMAKE_CXX_STANDARD 17`，`REQUIRED ON`，关闭扩展）。
- **预编译头（PCH）**：core 与 ui 分别维护精简的 PCH 头列表，加速增量编译。
- **驱动复制规则**：构建后自动复制 `driver/` 到输出目录，安装阶段也通过 `install(DIRECTORY ... FILES_MATCHING PATTERN "*.dll")` 打包 DLL。
- **Python 插件无依赖声明**：插件不维护 `requirements.txt`，PyQt6 作为可选模块被 SDK 捕获异常，因此插件部署依赖宿主环境已安装 PyQt6。

## 5. 缺失项

- 无 Python 依赖清单（`requirements.txt` / `pyproject.toml`），Python 侧依赖管理薄弱。
- 无 C++ 包管理器（vcpkg/conan/spm），升级第三方库需手动替换 `third_party/` 下源码。
- 无锁文件或版本标签机制，vendor 版本变更只能通过 git commit 追踪。
