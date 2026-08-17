---
kind: dependency_management
name: C++/Qt 项目的第三方依赖管理（vendored + CMake INTERFACE）
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - .gitignore
---

## 1. 使用的系统与方法

本项目采用 **源码级 vendoring（内联第三方库）** 配合 **CMake** 进行依赖声明与构建，不使用任何包管理器（如 vcpkg、Conan、NuGet、npm 等），也没有 lockfile。所有第三方头文件/源码直接存放在仓库根目录的 `third_party/` 子目录下，由顶层 `CMakeLists.txt` 通过 `include(third_party/Dependencies.cmake)` 统一引入。

- 构建系统：CMake 3.21+，C++17，启用 Qt6 自动处理（AUTOMOC/AUTOUIC/AUTORCC）。
- 依赖发现：`find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 查找系统安装的 Qt6；其余依赖全部来自本地 `third_party/`。
- 无私有注册表或网络下载步骤，构建完全离线可复现（只要 `third_party/` 已存在）。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层工程入口，声明 Qt6 依赖并 include `third_party/Dependencies.cmake` |
| `third_party/Dependencies.cmake` | 集中定义第三方目标：spdlog（INTERFACE）、qcustomplot（STATIC）、vector_blf（可选 subdirectory） |
| `src/CMakeLists.txt` | 将 spdlog、nlohmann_json、concurrentqueue、pugixml 的头路径以 PUBLIC 形式暴露给 `openbus_core`，并按需链接 qcustomplot、zlib、vector_blf、pugixml |
| `third_party/` | 各第三方库的源码/头文件存放目录 |
| `.gitignore` | 排除 `build/`、`tools/`、编译产物，但 **不排除** `third_party/`，表明 vendored 源码应随仓库提交 |

## 3. 架构与约定

### 3.1 第三方库分类与接入方式

| 第三方库 | 类型 | 接入方式 | 说明 |
|---|---|---|---|
| Qt6 | 系统库 | `find_package(Qt6 ...)` | 要求开发者环境已安装 Qt6，不在仓库中提供 |
| spdlog | 仅头文件 | `add_library(spdlog INTERFACE IMPORTED)`，设置 `INTERFACE_INCLUDE_DIRECTORIES` | 零链接，仅暴露 include 路径 |
| nlohmann/json | 单头文件 | 直接在 `src/CMakeLists.txt` 中将 `third_party/nlohmann_json` 加入 `target_include_directories` | 无需额外目标，`#include <json.hpp>` 即可使用 |
| concurrentqueue | 仅头文件 | 同上，按条件检查 `concurrentqueue.h` 是否存在后加入 include 路径 | 用于 `utils/message_queue.h` |
| pugixml | 仅头文件 | 同上，供 `arxml_importer.cpp` 解析 ARXML |
| qcustomplot | 源码库 | 在 `Dependencies.cmake` 中以 `add_library(qcustomplot STATIC ...)` 显式编译 `qcustomplot.cpp`，并链接 `Qt6::Widgets`、`Qt6::PrintSupport` | 仅在 UI 层 PRIVATE 链接 |
| vector_blf | CMake 子项目 | 若存在 `third_party/vector_blf/CMakeLists.txt`，则 `add_subdirectory(...)` 引入，并通过 `HAS_VECTOR_BLF` 宏开关控制 BLF 支持 |
| zlib | 系统库 | MinGW 自带 `libz.a`，通过 `target_link_libraries(openbus_core PRIVATE z)` 链接，用于 BLF 解压 |
| Windows SDK (psapi) | 平台库 | `if(WIN32) target_link_libraries(openbus_ui PRIVATE psapi)` |

### 3.2 依赖传播策略

- `openbus_core` 是核心静态库，其 `target_include_directories` 使用 `PUBLIC` 将 spdlog、nlohmann_json、concurrentqueue、pugixml 的 include 路径透传给上层，使 `core/logging.h`、`core/appconfig.h` 等公共头可直接 `#include <spdlog/spdlog.h>`、`<nlohmann/json.hpp>`。
- `openbus_ui` 通过 `PRIVATE` 链接 qcustomplot 和 Qt6::Svg，避免将绘图依赖泄漏到 core 层。
- 最终可执行目标 `openbus` 仅 PRIVATE 链接 `openbus_ui`，形成清晰的依赖分层。

### 3.3 运行时依赖（非编译期）

- `driver/` 目录包含 ZLG CAN 设备的 DLL（`zlgcan.dll`、`CANDevCore.dll` 等）及 XML 设备配置。构建脚本通过 `POST_BUILD` 命令将整个 `driver/` 目录拷贝到 exe 输出目录，确保运行时能找到驱动 DLL。
- 这些 DLL 属于硬件厂商 SDK，不属于通用第三方库，但仍遵循“随应用分发”的模式。

## 4. 约定与约束

1. **所有第三方头文件/源码必须放入 `third_party/<name>/`**，并在 `Dependencies.cmake` 或 `src/CMakeLists.txt` 中显式声明 include 路径或 add_library，禁止隐式依赖系统全局路径。
2. **新增第三方库时**：
   - 纯头文件库 → 在 `src/CMakeLists.txt` 中添加条件性 `target_include_directories`（检查文件存在后再添加，保证跨平台可构建）。
   - 需要编译的库 → 在 `third_party/Dependencies.cmake` 中用 `add_library` 定义目标，并在 `src/CMakeLists.txt` 中 `target_link_libraries` 链接。
   - 带 CMake 的子项目 → 使用 `add_subdirectory` 方式引入（参考 vector_blf）。
3. **Qt6 必须在构建环境中预先安装**，因为项目未 vendoring Qt，也不使用包管理器拉取。
4. **`.gitignore` 明确排除了 `build/`、`tools/`、`*.dll`、`*.a` 等构建产物，但未排除 `third_party/`**，意味着 vendored 源码应当纳入版本控制。
5. **BLF 支持是可插拔的**：只有当 `third_party/vector_blf/CMakeLists.txt` 存在时才启用，否则代码通过 `HAS_VECTOR_BLF` 宏回退到内置实现（见 `blf.cpp`）。
6. **Windows 下驱动 DLL 必须与 exe 同目录**：构建后通过 `copy_directory` 自动复制，安装时也通过 `install(DIRECTORY ... FILES_MATCHING PATTERN "*.dll")` 打包。
7. **未使用任何锁文件或版本锁定机制**：依赖版本由 `third_party/` 目录中的源码快照决定，升级需手动替换对应目录内容。