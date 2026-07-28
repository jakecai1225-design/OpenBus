---
kind: dependency_management
name: 依赖管理 — CMake + Qt6 外部依赖发现与链接
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - resources/resources.qrc
---

本项目为基于 Qt6 的 C++ 桌面应用，依赖管理完全通过 CMake 完成，未使用任何包管理器（如 vcpkg、Conan、pkg-config 等）或 vendoring 策略。所有第三方库依赖集中在顶层 `CMakeLists.txt` 中声明，由 CMake 在构建时自动查找并链接。

**使用的系统与工具**
- 构建系统：CMake 3.21+，通过 `cmake_minimum_required(VERSION 3.21)` 强制最低版本。
- 语言标准：C++17，通过 `CMAKE_CXX_STANDARD` 及 `REQUIRED ON` 强制启用。
- 第三方框架：Qt6 Widgets，通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets)` 发现，并使用 `qt_standard_project_setup()` 启用 Qt 标准项目设置。
- Qt 自动化：`CMAKE_AUTOMOC/AUTOUIC/AUTORCC` 全部开启，无需手动处理 MOC/UIC/RCC。

**关键文件与位置**
- 顶层 `CMakeLists.txt`：定义项目元信息、C++ 标准、Qt6 查找、子目录包含与安装规则。
- `src/CMakeLists.txt`：声明源文件分组（core/models/utils/ui）、创建可执行目标 `sin`、链接 `Qt6::Widgets`、配置头文件搜索路径及 Windows GUI 属性。
- `resources/resources.qrc`：Qt 资源文件，被直接嵌入到可执行文件中，不视为外部依赖。

**架构与约定**
- 依赖发现采用 CMake 原生 `find_package` 机制，要求构建环境已正确安装 Qt6 并通过 Qt 的 CMake 模块暴露给 CMake。
- 没有 lockfile、vendor 目录或私有仓库配置；依赖版本由构建环境中安装的 Qt6 版本决定，而非由仓库锁定。
- 所有源码按功能分层组织（core/models/utils/ui），但依赖声明仅集中在 CMake 脚本中，代码层无显式依赖导入逻辑。

**约束与约定**
- C++ 标准固定为 C++17，禁止使用扩展（`CMAKE_CXX_EXTENSIONS OFF`）。
- 输出二进制统一放置于 `${CMAKE_BINARY_DIR}/bin`。
- Windows 平台下通过 `WIN32_EXECUTABLE TRUE` 属性生成 GUI 程序，不弹出控制台窗口。
- 安装规则将可执行文件安装到 `bin` 目录。
- 未发现任何 `.gitignore` 中对依赖缓存或包管理器文件的排除规则，也未见 `go.mod`、`package.json`、`vcpkg.json`、`conanfile.*` 等依赖清单文件。