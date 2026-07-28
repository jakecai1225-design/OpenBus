---
kind: dependency_management
name: 基于 CMake + Qt6 的依赖管理
category: dependency_management
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
---

本项目使用 CMake 作为构建与依赖管理系统，通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets)` 在根目录 `CMakeLists.txt` 中声明对 Qt6 的依赖。Qt 框架由系统或工具链（如 MSVC、MinGW 或 Qt Creator）提供，项目未使用包管理器（如 vcpkg、Conan）进行第三方库的版本锁定与分发，也未包含 vendored 源码或私有仓库配置。

依赖声明集中在两个 CMake 文件中：
- 根 `CMakeLists.txt`：定义项目元信息、C++17 标准、Qt 自动化处理（AUTOMOC/AUTOUIC/AUTORCC），并通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets)` 查找 Qt6 Widgets 模块。
- `src/CMakeLists.txt`：通过 `target_link_libraries(sin PRIVATE Qt6::Widgets)` 将 Qt6::Widgets 链接到可执行目标，并列出所有源文件。

项目未引入除 Qt6 之外的任何第三方库，DBC 解析等核心功能均为自实现（`core/dbcmanager.cpp` 等）。资源文件通过 Qt 的 `.qrc` 资源系统进行编译期打包。

约束与约定：
- C++ 标准固定为 C++17（`CMAKE_CXX_STANDARD 17`，`REQUIRED ON`，`EXTENSIONS OFF`）。
- 仅依赖 Qt6 Widgets 模块，未启用 Network、SerialPort 等其他 Qt 模块。
- 无锁文件（lockfile）、无包管理器配置文件、无 vendor 目录，依赖版本由宿主环境的 Qt6 安装决定。
- Windows 平台通过 `WIN32_EXECUTABLE TRUE` 属性以 GUI 模式运行，不弹出控制台窗口。