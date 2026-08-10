---
kind: dependency_management
name: 基于 CMake 的第三方源码内联依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - third_party/Dependencies.cmake
    - src/CMakeLists.txt
    - .gitignore
    - driver/zlgcan.dll
    - driver/dll_cfg.ini
---

## 1. 使用的系统/方法

本项目采用 **CMake + 源码内联（vendored）+ INTERFACE IMPORTED 目标** 的方式管理第三方依赖，不使用任何包管理器（如 vcpkg、Conan、NuGet）。所有第三方库以源码形式直接存放在 `third_party/` 目录下，通过 CMake 配置暴露为头文件路径或静态库目标。

- 构建系统：CMake 3.21+，C++17，Qt6（Widgets / PrintSupport / Svg）。
- 依赖发现：根 `CMakeLists.txt` 调用 `include(${CMAKE_SOURCE_DIR}/third_party/Dependencies.cmake)` 集中声明第三方依赖；`src/CMakeLists.txt` 在 `sin_core` 和 `sin_ui` 两个静态库中按需引入对应 include 目录与链接库。
- 运行时驱动：`driver/` 下的 ZLG CAN SDK DLL（`zlgcan.dll`、`ZPSCANFD.dll` 等）通过 `POST_BUILD` 命令复制到输出目录，随可执行分发。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 项目入口，设置 Qt6 查找、包含 `third_party/Dependencies.cmake`、添加 `src/` 子目录 |
| `third_party/Dependencies.cmake` | 集中声明第三方依赖：spdlog（INTERFACE IMPORTED）、qcustomplot（STATIC）、vector_blf（add_subdirectory） |
| `src/CMakeLists.txt` | 定义 `sin_core`、`sin_ui` 两个静态库及最终 `sin` 可执行体；按模块引入 spdlog、nlohmann_json、concurrentqueue、pugixml 的头文件路径，并链接 z、qcustomplot、Qt6::Svg |
| `.gitignore` | 排除 `build/`、`tools/`、编译产物、日志等；未忽略 `third_party/`，说明源码依赖应随仓库提交 |
| `driver/` | 预编译的 ZLG SDK DLL 与配置文件，构建后拷贝到 exe 同级目录 |

## 3. 架构与约定

### 3.1 依赖分类与接入方式

| 依赖 | 类型 | 接入方式 | 备注 |
|---|---|---|---|
| Qt6 | 外部框架 | `find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Svg)` | 通过系统/环境变量提供 |
| spdlog | 头文件库 | `INTERFACE IMPORTED` 目标 + `target_include_directories(... PUBLIC ...)` | 仅暴露 include 路径 |
| nlohmann/json | 单头文件库 | 直接 include 路径，无目标 | 条件判断 `EXISTS ... json.hpp` |
| concurrentqueue | 头文件库 | 同上，条件 include 路径 | 用于 `utils/message_queue.h` |
| pugixml | 可选库 | 条件 include 路径 + 可选 `pugixml` 目标链接 | 仅 ARXML 导入使用 |
| qcustomplot | 源码库 | 编译 `qcustomplot.cpp` 为 STATIC 库，链接 Qt6::Widgets/PrintSupport | 仅 UI 层使用，PRIVATE 链接 |
| vector_blf | 源码库 | `add_subdirectory(...)` 引入其自身 CMakeLists | GPL-3.0，条件检查存在性 |
| zlib | 系统库 | `target_link_libraries(sin_core PRIVATE z)` | MinGW 自带 libz.a，BLF 解压用 |
| ZLG SDK DLL | 二进制驱动 | `POST_BUILD copy_directory driver/ → $<TARGET_FILE_DIR:sin>` | 必须与 exe 同目录 |

### 3.2 分层隔离

- `sin_core` 静态库聚合核心逻辑，对外暴露公共头文件路径（spdlog、json、concurrentqueue、pugixml），使上层无需感知具体第三方位置。
- `sin_ui` 静态库仅 PRIVATE 依赖 qcustomplot 和 Qt6::Svg，避免将绘图库暴露给 core。
- 最终 `sin` 可执行体只链接 `sin_ui`，形成 `sin → sin_ui → sin_core → 第三方` 的单向依赖链。

### 3.3 条件编译与健壮性

所有第三方依赖均使用 `if(EXISTS "...")` 包裹，确保在未 vendored 的情况下仍可部分编译（例如缺少 pugixml 时跳过 ARXML 相关链接）。这使仓库在无完整 third_party 时仍能进行最小化构建。

## 4. 约定与约束

- **源码内联优先**：除 Qt6 和系统 zlib 外，所有第三方库以源码形式放入 `third_party/<name>/`，不通过包管理器下载。
- **集中声明**：第三方依赖的统一入口是 `third_party/Dependencies.cmake`，新增依赖应在此注册为目标或 include 路径。
- **头文件路径上推**：`sin_core` 通过 `PUBLIC target_include_directories` 把第三方 include 路径传播给依赖它的目标，使用者无需再手动指定路径。
- **私有 vs 公共链接**：qcustomplot、Qt6::Svg 对 UI 层是 PRIVATE 依赖，core 层不可见；zlib 对 core 是 PRIVATE，符合“谁用谁连”的原则。
- **驱动 DLL 随构建复制**：`driver/` 中的 DLL 不是编译产物，而是通过 `POST_BUILD` 命令复制到输出目录，安装规则也将其纳入 `bin/`。
- **构建工具本地化**：`tools/ninja/` 等构建辅助工具被 `.gitignore` 排除，属于本地下载物，不应提交到仓库。
- **版本锁定**：由于依赖以源码形式提交，版本由仓库中 `third_party/` 下各子目录的具体提交决定，没有 lockfile；升级需替换源码目录内容。
- **许可证合规**：`Dependencies.cmake` 中对 vector_blf 明确标注 GPL-3.0，提示使用者注意许可证兼容性。