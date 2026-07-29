---
kind: build_system
name: CMake + Python 构建脚本的 Qt6 桌面应用构建系统
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
---

本项目采用 CMake 作为核心构建系统，配合自研 Python 构建脚本 `scripts/build.py` 提供统一的开发体验。整体架构如下：

**1. 构建工具链与配置**
- CMake 最低版本要求 3.21，项目根目录 `CMakeLists.txt` 定义项目名称、版本号（0.1.0）、C++17 标准，并启用 Qt 自动化处理（AUTOMOC/AUTOUIC/AUTORCC）。
- 通过 `find_package(Qt6 REQUIRED COMPONENTS Widgets)` 查找 Qt6，并使用 `qt_standard_project_setup()` 启用 Qt 标准项目设置。
- 子目录 `src/CMakeLists.txt` 按模块组织源文件（core/models/utils/ui），使用 `qt_add_executable` 生成可执行目标 `sin`，并通过 `target_link_libraries` 链接 `Qt6::Widgets`。
- Windows 平台通过 `WIN32_EXECUTABLE TRUE` 属性以 GUI 模式运行（不弹出控制台窗口）。

**2. Python 构建脚本**
`scripts/build.py` 提供完整的命令行接口，支持以下命令：
- `configure`：CMake 配置，支持 Debug/Release/RelWithDebInfo/MinSizeRel 四种构建类型
- `build`：增量编译，自动检测是否需要先执行 configure
- `run`：运行程序（自动触发编译）
- `debug`：GDB 调试
- `clean/rebuild`：清理或重新构建
- `deploy`：调用 `windeployqt` 部署 Qt 运行时依赖
- `all`：完整流程（配置+编译+部署+运行）
- `status/open`：环境状态检查与打开输出目录

脚本通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 或命令行参数 `--qt-dir`、`--mingw-dir`、`--cmake-dir` 配置工具路径，默认指向 MinGW 64 位工具链和 Qt 6.8.3。

**3. 构建约定与约束**
- 构建产物统一输出到 `build/bin/` 目录（通过 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 设置）
- 资源文件通过 Qt 资源系统管理（`resources/resources.qrc`）
- 安装规则将可执行文件安装到 `bin` 目录
- 项目结构严格分层：core（核心逻辑）、models（Qt 数据模型）、utils（工具函数）、ui（界面组件）
- 未包含 CI/CD 配置文件（如 GitHub Actions、Jenkinsfile 等），也未发现 Dockerfile 或跨平台打包脚本
- 当前构建脚本主要针对 Windows + MinGW 环境设计（硬编码路径和 windeployqt 调用）