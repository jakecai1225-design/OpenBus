---
kind: build_system
name: CMake + Python 构建系统（Qt6/MinGW）
category: build_system
scope:
    - '**'
source_files:
    - CMakeLists.txt
    - src/CMakeLists.txt
    - scripts/build.py
    - resources/resources.qrc
---

本项目采用 **CMake 3.21+** 作为核心构建系统，配合 **Python 构建脚本** `scripts/build.py` 封装常用开发流程，基于 **Qt6 + MinGW (g++)** 工具链在 Windows 平台进行编译、部署与调试。整体构建体系围绕 CMake 分层配置与 Python 命令式包装展开。

### 1. 构建系统与工具链
- **CMake 版本要求**: ≥ 3.21，启用 C++17 标准（`CMAKE_CXX_STANDARD 17`），关闭编译器扩展。
- **Qt 自动化**: 开启 `AUTOMOC/AUTOUIC/AUTORCC`，通过 `qt_standard_project_setup()` 和 `qt_add_executable()` 集成 Qt6 Widgets 与 Charts 模块。
- **工具链**: 默认使用 MinGW g++/gcc 与 windeployqt，路径可通过环境变量 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 或命令行参数覆盖。
- **输出目录**: 可执行文件统一输出至 `build/bin/sin.exe`。

### 2. 核心构建文件
- **根 `CMakeLists.txt`**: 定义项目元信息（名称 sin、版本 0.1.0）、Qt 查找与子目录引入。
- **`src/CMakeLists.txt`**: 按 core/models/ui/utils 四层组织源文件列表，链接 Qt6::Widgets 与 Qt6::Charts，Windows 下以 GUI 程序方式运行（不弹出控制台）。
- **`scripts/build.py`**: 提供 configure/build/run/debug/clean/rebuild/deploy/all/status/open 等子命令，自动检测工具链、处理并行编译（`-j`）、调用 windeployqt 打包依赖。

### 3. 构建流程约定
- **增量构建**: `build` 命令会检查 `CMakeCache.txt`，未配置时自动回退到 `configure`。
- **构建类型**: 支持 Debug / Release / RelWithDebInfo / MinSizeRel，默认 Debug。
- **部署**: `deploy` 命令调用 `windeployqt` 将 Qt 运行时 DLL 复制到输出目录。
- **清理**: `clean` 直接删除 `build/` 目录；`rebuild` 组合 clean → configure → build。
- **资源管理**: Qt 资源文件 `resources/resources.qrc` 通过 AUTORCC 自动处理 QSS 样式表。

### 4. 约束与限制
- 当前构建脚本硬编码了 Windows 路径（如 `D:/Qt/6.8.3/mingw_64`），跨平台适配尚未实现。
- 未包含 CI/CD 流水线、交叉编译或包管理器（如 vcpkg/conan）集成，依赖手动安装 Qt6 与 MinGW。
- 第三方库 `third_party/dbcppp` 仅放置源码与压缩包，未在 CMake 中集成构建。