---
kind: build_system
name: CMake + Qt6 构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - src/main.cpp
    - resources/resources.qrc
---

本项目采用 CMake 3.21+ 作为顶层构建系统，结合 Qt6 的自动化处理机制（MOC/UIC/RCC）构建 Sin 桌面应用。整体结构简洁清晰，遵循 Qt 官方推荐的项目组织方式。

**构建工具与版本要求**
- CMake 最低版本：3.21
- C++ 标准：强制 C++17，禁用扩展
- Qt 组件：仅依赖 Qt6::Widgets
- 使用 `qt_standard_project_setup()` 启用 Qt 标准项目设置

**工程结构与职责划分**
- 根目录 `CMakeLists.txt`：定义项目名称、版本（0.1.0）、语言、全局编译选项、Qt 自动化开关、输出目录（`CMAKE_BINARY_DIR/bin`），并添加 `src` 子目录
- `src/CMakeLists.txt`：通过 `qt_add_executable(sin ...)` 声明可执行目标，链接 `Qt6::Widgets`，配置 Windows GUI 程序属性（不弹出控制台窗口），并定义安装规则
- `resources/resources.qrc`：Qt 资源文件，将 `styles/default.qss` 样式表打包进二进制

**构建流程与约定**
- 源码位于 `src/` 目录，包含 `main.cpp` 和 `ui/mainwindow.{h,cpp,ui}`
- 通过 `CMAKE_AUTOMOC/AUTOUIC/AUTORCC ON` 自动处理 Qt 元对象、UI 文件和资源文件
- 头文件搜索路径指向当前源目录，为后续扩展 core/models/services/utils 模块预留了 include 目录添加位置
- 输出产物统一放置在构建目录的 `bin/` 子目录下
- 安装规则将 sin 可执行文件安装到 `bin` 目录

**平台相关处理**
- Windows 下通过 `WIN32_EXECUTABLE TRUE` 属性使程序以 GUI 模式运行，避免控制台窗口弹出

**资源管理**
- 使用 Qt Resource System（`.qrc`）内嵌样式表，运行时通过 `:/styles/default.qss` 路径加载
- 主程序在启动时动态读取并应用 QSS 样式

**未发现的构建特性**
- 未发现 CI/CD 配置文件（如 GitHub Actions、Jenkinsfile 等）
- 未发现 Dockerfile 或容器化构建脚本
- 未发现交叉编译配置或跨平台构建脚本
- 未发现单元测试框架集成或测试目标定义
- 未发现打包发布脚本或安装包生成配置