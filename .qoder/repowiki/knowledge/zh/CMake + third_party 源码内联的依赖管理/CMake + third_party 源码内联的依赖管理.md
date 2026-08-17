---
kind: dependency_management
name: CMake + third_party 源码内联的依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - third_party/Dependencies.cmake
    - third_party/spdlog/include/spdlog/spdlog.h
    - third_party/nlohmann_json/json.hpp
    - third_party/qcustomplot/qcustomplot.cpp
    - third_party/vector_blf/CMakeLists.txt
    - src/core/candevice_zlg.cpp
    - src/core/file_import/blf_importer.cpp
---

## 1. 使用的系统/方法

本项目采用 **CMake 3.21+** 作为构建与依赖管理系统，所有第三方 C/C++ 库以 **源码形式直接内联到 `third_party/` 目录**（即 vendoring），不依赖外部包管理器（如 vcpkg、Conan、NuGet 等）。Qt6 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` 由系统或 Qt 安装提供。Python 插件位于 `plugins/`，仅使用标准库，无 `requirements.txt` / `pyproject.toml`。

## 2. 关键文件

- `CMakeLists.txt`（根）：声明项目版本、C++17、Qt6 查找、包含 `third_party/Dependencies.cmake`。
- `src/CMakeLists.txt`：定义 `openbus_core`、`openbus_ui` 两个静态库及 `openbus` 可执行目标；集中列出头文件 include 路径、链接依赖、PCH 预编译头列表；将 `driver/` 下的 DLL 在 post-build 阶段拷贝到输出目录。
- `third_party/Dependencies.cmake`：为每个 vendored 库创建 CMake target 或 INTERFACE 目标。
- `third_party/`：实际存放第三方源码的目录。

## 3. 架构与约定

### 3.1 Vendoring 策略
所有第三方头文件或源码直接放入 `third_party/<lib>/`，构建时通过相对路径引用：

| 依赖 | 存放位置 | 引入方式 | 备注 |
|---|---|---|---|
| spdlog | `third_party/spdlog/include` | `target_include_directories(... PUBLIC ${CMAKE_SOURCE_DIR}/third_party/spdlog/include)` | 单头/头文件库，INTERFACE IMPORTED target |
| nlohmann/json | `third_party/nlohmann_json/json.hpp` | 同上 | 单头文件，无需链接 |
| concurrentqueue | `third_party/concurrentqueue/concurrentqueue.h` | 同上 | 单头文件 |
| pugixml | `third_party/pugixml/pugixml.hpp` | 同上 | 用于 ARXML 导入 |
| qcustomplot | `third_party/qcustomplot/{qcustomplot.h, cpp}` | 显式 `add_library(qcustomplot STATIC ...)` | 需编译并链接 Qt6::Widgets、PrintSupport |
| vector_blf | `third_party/vector_blf/` | `add_subdirectory(...)` 子工程 | GPL-3.0，可选启用（`HAS_VECTOR_BLF`） |

### 3.2 分层链接模型
- `openbus_core`（核心静态库）：公开暴露 spdlog、nlohmann_json、concurrentqueue、pugixml 的 include 路径给 UI 层（PUBLIC），同时 PRIVATE 链接 zlib (`z`) 和可选的 `vector_blf`。
- `openbus_ui`（UI 静态库）：PUBLIC 依赖 `openbus_core` 和 `Qt6::Widgets`，PRIVATE 链接 `qcustomplot` 和 `Qt6::Svg`。
- `openbus`（可执行文件）：仅 PRIVATE 链接 `openbus_ui`，形成单向依赖链，避免 UI 改动触发 core 重编译。

### 3.3 平台相关依赖
- Windows 下额外链接 `psapi`（进程资源监控）。
- BLF 解析需要 zlib；MinGW 自带 `libz.a`，直接使用 `z` 目标名。
- 驱动 DLL（ZLG SDK、Vector 等）不通过 CMake 管理，而是放在 `driver/`，构建后通过 `add_custom_command(TARGET openbus POST_BUILD ...)` 复制到 exe 同级目录。

### 3.4 Python 插件
`plugins/*/plugin.json` + `main.py` 是运行时动态加载的 Python 脚本，无包管理声明；插件之间相互独立，通过 JSON 元数据描述。

## 4. 约定与约束

- **禁止使用全局包管理器**：仓库中不存在 `package.json`、`go.mod`、`Cargo.toml`、`vcpkg.json` 等任何包清单文件；所有 C/C++ 依赖必须以内联源码形式加入 `third_party/`。
- **新增第三方库的约定**：
  1. 将源码放入 `third_party/<name>/`；
  2. 在 `third_party/Dependencies.cmake` 中添加对应的 target（INTERFACE 或 STATIC）；
  3. 在 `src/CMakeLists.txt` 中通过 `target_link_libraries` 或 `target_include_directories` 引用；
  4. 若为可选功能，使用 `if(EXISTS ...)` 条件包裹并在编译期定义宏（如 `HAS_VECTOR_BLF`）控制分支。
- **Qt 版本锁定**：根 CMakeLists 固定要求 `Qt6`，并通过 `qt_standard_project_setup()` 启用标准配置。
- **C++ 标准锁定**：强制 `CMAKE_CXX_STANDARD 17`，关闭扩展，确保跨编译器一致性。
- **PCH 白名单**：`src/CMakeLists.txt` 中分别维护 core/UI 层的预编译头列表，新增公共头需同步更新，否则无法享受 PCH 加速。
- **驱动二进制隔离**：`driver/` 目录中的 `.dll/.lib` 不被纳入 CMake 构建流程，仅在 post-build 阶段复制；这意味着它们不参与版本化追踪之外的依赖检查，属于“随附二进制”而非构建依赖。
- **许可证标注**：`third_party/` 中各库保留其原始 LICENSE（如 vector_blf 标注 GPL-3.0），分发时需遵守各自许可条款。